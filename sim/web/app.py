"""
FastAPI backend for CubeCellMeshCore Simulator GUI.
REST API + WebSocket for real-time events.
"""

from __future__ import annotations
import asyncio
import json
import os
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, JSONResponse
from pydantic import BaseModel

from sim.runner import SimRunner

# Global runner instance
runner: SimRunner | None = None
ws_clients: list[WebSocket] = []
sim_task: asyncio.Task | None = None
sim_running: bool = False
sim_speed: float = 1.0


class AddNodeRequest(BaseModel):
    name: str
    x: float = 0.0
    y: float = 0.0
    type: str = "repeater"


class LinkRequest(BaseModel):
    node_a: str
    node_b: str
    rssi: int = -70
    snr: int = 32


class CommandRequest(BaseModel):
    cmd: str


class SpeedRequest(BaseModel):
    speed: float


def _setup_default_scenario(r: SimRunner):
    """Setup a default linear scenario."""
    a = r.add_repeater("RelayA", x=150, y=300)
    b = r.add_repeater("RelayB", x=400, y=300)
    c = r.add_repeater("RelayC", x=650, y=300)
    r.set_link("RelayA", "RelayB", rssi=-70, snr=32)
    r.set_link("RelayB", "RelayC", rssi=-75, snr=28)
    a.time_sync.set_time(1_700_000_000)


def _setup_star(r: SimRunner):
    a = r.add_repeater("Center", x=400, y=300)
    r.add_repeater("North", x=400, y=100)
    r.add_repeater("East", x=600, y=300)
    r.add_repeater("South", x=400, y=500)
    r.add_repeater("West", x=200, y=300)
    for name in ["North", "East", "South", "West"]:
        r.set_link("Center", name, rssi=-65, snr=36)
    a.time_sync.set_time(1_700_000_000)


def _setup_companion(r: SimRunner):
    c1 = r.add_companion("Comp1", x=100, y=300)
    ra = r.add_repeater("RepA", x=300, y=300)
    rb = r.add_repeater("RepB", x=500, y=300)
    c2 = r.add_companion("Comp2", x=700, y=300)
    r.set_link("Comp1", "RepA", rssi=-65, snr=36)
    r.set_link("RepA", "RepB", rssi=-70, snr=32)
    r.set_link("RepB", "Comp2", rssi=-65, snr=36)
    ra.time_sync.set_time(1_700_000_000)


SCENARIOS = {
    "linear": _setup_default_scenario,
    "star": _setup_star,
    "companion": _setup_companion,
}


async def broadcast_ws(data: dict):
    """Send data to all connected WebSocket clients."""
    msg = json.dumps(data, default=str)
    disconnected = []
    for ws in ws_clients:
        try:
            await ws.send_text(msg)
        except Exception:
            disconnected.append(ws)
    for ws in disconnected:
        ws_clients.remove(ws)


async def sim_loop():
    """Background simulation loop."""
    global sim_running
    while sim_running:
        events = runner.run_step(10)
        if events:
            for ev in events:
                await broadcast_ws(ev)
        # Send periodic state updates
        await broadcast_ws({'type': 'state', 'data': runner.get_state()})
        # Sleep based on speed: at speed=1x, 10ms sim = 10ms real
        delay = 0.01 / max(sim_speed, 0.1)
        await asyncio.sleep(delay)


@asynccontextmanager
async def lifespan(app: FastAPI):
    global runner
    runner = SimRunner()
    _setup_default_scenario(runner)
    yield
    global sim_running
    sim_running = False


def create_app() -> FastAPI:
    app = FastAPI(title="CubeCellMeshCore Simulator", lifespan=lifespan)

    static_dir = os.path.join(os.path.dirname(__file__), "static")

    @app.get("/")
    async def index():
        return FileResponse(os.path.join(static_dir, "index.html"))

    app.mount("/static", StaticFiles(directory=static_dir), name="static")

    # --- REST API ---

    @app.get("/api/state")
    async def get_state():
        return runner.get_state()

    @app.get("/api/nodes")
    async def get_nodes():
        return runner.get_state()['nodes']

    @app.post("/api/nodes")
    async def add_node(req: AddNodeRequest):
        if req.name in runner.nodes:
            return JSONResponse({"error": "Node already exists"}, status_code=400)
        if req.type == "companion":
            runner.add_companion(req.name, req.x, req.y)
        else:
            runner.add_repeater(req.name, req.x, req.y)
        return {"ok": True, "name": req.name}

    @app.delete("/api/nodes/{name}")
    async def delete_node(name: str):
        runner.remove_node(name)
        return {"ok": True}

    @app.post("/api/nodes/{name}/command")
    async def node_command(name: str, req: CommandRequest):
        result = runner.inject_command(name, req.cmd)
        # Step once to process
        events = runner.run_step(10)
        for ev in events:
            await broadcast_ws(ev)
        return {"result": result}

    @app.post("/api/links")
    async def add_link(req: LinkRequest):
        runner.set_link(req.node_a, req.node_b, req.rssi, req.snr)
        return {"ok": True}

    @app.delete("/api/links")
    async def delete_link(req: LinkRequest):
        runner.remove_link(req.node_a, req.node_b)
        return {"ok": True}

    @app.post("/api/control/play")
    async def control_play():
        global sim_running, sim_task
        if not sim_running:
            sim_running = True
            runner.paused = False
            sim_task = asyncio.create_task(sim_loop())
        return {"ok": True, "running": True}

    @app.post("/api/control/pause")
    async def control_pause():
        global sim_running, sim_task
        sim_running = False
        runner.paused = True
        if sim_task:
            sim_task.cancel()
            try:
                await sim_task
            except asyncio.CancelledError:
                pass
            sim_task = None
        return {"ok": True, "running": False}

    @app.post("/api/control/step")
    async def control_step():
        events = runner.run_step(10)
        for ev in events:
            await broadcast_ws(ev)
        state = runner.get_state()
        await broadcast_ws({'type': 'state', 'data': state})
        return {"ok": True, "events": len(events)}

    @app.post("/api/control/reset")
    async def control_reset():
        global sim_running, sim_task
        sim_running = False
        if sim_task:
            sim_task.cancel()
            try:
                await sim_task
            except asyncio.CancelledError:
                pass
            sim_task = None
        runner.reset()
        _setup_default_scenario(runner)
        return {"ok": True}

    @app.post("/api/control/speed")
    async def control_speed(req: SpeedRequest):
        global sim_speed
        sim_speed = max(0.1, min(100.0, req.speed))
        runner.speed = sim_speed
        return {"ok": True, "speed": sim_speed}

    @app.get("/api/scenarios")
    async def list_scenarios():
        return {"scenarios": list(SCENARIOS.keys())}

    @app.post("/api/scenario/{name}")
    async def load_scenario(name: str):
        global sim_running, sim_task
        sim_running = False
        if sim_task:
            sim_task.cancel()
            try:
                await sim_task
            except asyncio.CancelledError:
                pass
            sim_task = None

        if name not in SCENARIOS:
            return JSONResponse({"error": f"Unknown scenario: {name}"}, status_code=400)

        runner.reset()
        SCENARIOS[name](runner)
        state = runner.get_state()
        await broadcast_ws({'type': 'state', 'data': state})
        return {"ok": True, "scenario": name}

    # --- WebSocket ---

    @app.websocket("/ws")
    async def websocket_endpoint(ws: WebSocket):
        await ws.accept()
        ws_clients.append(ws)
        try:
            # Send initial state
            await ws.send_text(json.dumps({'type': 'state', 'data': runner.get_state()}, default=str))
            while True:
                data = await ws.receive_text()
                # Handle incoming messages if needed
        except WebSocketDisconnect:
            pass
        finally:
            if ws in ws_clients:
                ws_clients.remove(ws)

    return app
