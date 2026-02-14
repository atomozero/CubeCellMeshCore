// CubeCellMeshCore Simulator - Frontend

const API = '';
let ws = null;
let state = { nodes: {}, links: [], time_ms: 0, paused: true, speed: 1.0 };
let selectedNode = null;
let logFilter = 'all';
let logEntries = [];
let cliHistory = [];
let cliHistoryIdx = -1;
let animatedPackets = [];
let dragNode = null;
let dragOffset = { x: 0, y: 0 };
let contextTarget = null;

// Canvas
const canvas = document.getElementById('topo-canvas');
const ctx = canvas.getContext('2d');

// --- WebSocket ---
function connectWS() {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${proto}//${location.host}/ws`);
    ws.onmessage = (e) => {
        const data = JSON.parse(e.data);
        handleWSEvent(data);
    };
    ws.onclose = () => setTimeout(connectWS, 2000);
    ws.onerror = () => {};
}

function handleWSEvent(data) {
    if (data.type === 'state' && data.data) {
        state = data.data;
        updateUI();
    } else if (data.type === 'log') {
        addLog(data);
    } else if (data.type === 'packet_tx') {
        animatePacket(data);
    } else if (data.type === 'packet_rx') {
        // flash on receive
    }
}

// --- API Calls ---
async function apiPost(url, body = {}) {
    const r = await fetch(API + url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
    });
    return r.json();
}

async function apiGet(url) {
    const r = await fetch(API + url);
    return r.json();
}

async function apiDelete(url, body = {}) {
    const r = await fetch(API + url, {
        method: 'DELETE',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
    });
    return r.json();
}

// --- Controls ---
document.getElementById('btn-play').onclick = async () => {
    await apiPost('/api/control/play');
    document.getElementById('btn-play').classList.add('active');
    document.getElementById('btn-pause').classList.remove('active');
};

document.getElementById('btn-pause').onclick = async () => {
    await apiPost('/api/control/pause');
    document.getElementById('btn-pause').classList.add('active');
    document.getElementById('btn-play').classList.remove('active');
};

document.getElementById('btn-step').onclick = async () => {
    await apiPost('/api/control/step');
};

document.getElementById('btn-reset').onclick = async () => {
    await apiPost('/api/control/reset');
    logEntries = [];
    document.getElementById('log-content').innerHTML = '';
    document.getElementById('cli-output').innerHTML = '';
};

document.getElementById('speed-slider').oninput = async (e) => {
    const speed = parseFloat(e.target.value);
    document.getElementById('speed-val').textContent = speed.toFixed(1) + 'x';
    await apiPost('/api/control/speed', { speed });
};

document.getElementById('btn-load-scenario').onclick = async () => {
    const name = document.getElementById('scenario-select').value;
    await apiPost(`/api/scenario/${name}`);
    logEntries = [];
    document.getElementById('log-content').innerHTML = '';
};

document.getElementById('btn-add-node').onclick = async () => {
    const name = prompt('Node name:');
    if (!name) return;
    const type = prompt('Type (repeater/companion):', 'repeater');
    await apiPost('/api/nodes', { name, x: 400, y: 300, type: type || 'repeater' });
    const s = await apiGet('/api/state');
    state = s;
    updateUI();
};

// --- CLI ---
const cliInput = document.getElementById('cli-input');
cliInput.addEventListener('keydown', async (e) => {
    if (e.key === 'Enter') {
        const cmd = cliInput.value.trim();
        if (!cmd || !selectedNode) return;
        cliHistory.push(cmd);
        cliHistoryIdx = cliHistory.length;
        appendCli(`> ${cmd}`);
        cliInput.value = '';
        const result = await apiPost(`/api/nodes/${selectedNode}/command`, { cmd });
        if (result.result) appendCli(result.result);
    } else if (e.key === 'ArrowUp') {
        if (cliHistoryIdx > 0) {
            cliHistoryIdx--;
            cliInput.value = cliHistory[cliHistoryIdx];
        }
        e.preventDefault();
    } else if (e.key === 'ArrowDown') {
        if (cliHistoryIdx < cliHistory.length - 1) {
            cliHistoryIdx++;
            cliInput.value = cliHistory[cliHistoryIdx];
        } else {
            cliHistoryIdx = cliHistory.length;
            cliInput.value = '';
        }
        e.preventDefault();
    }
});

function appendCli(text) {
    const out = document.getElementById('cli-output');
    out.innerHTML += text.replace(/\n/g, '<br>') + '<br>';
    out.scrollTop = out.scrollHeight;
}

// --- Log ---
function addLog(data) {
    logEntries.push(data);
    if (logEntries.length > 500) logEntries.shift();

    if (logFilter !== 'all' && data.node !== logFilter) return;

    const logDiv = document.getElementById('log-content');
    const line = document.createElement('div');
    line.className = 'log-line';
    if (data.msg.includes('[OK]')) line.classList.add('type-ok');
    else if (data.msg.includes('[E]')) line.classList.add('type-err');
    else if (data.msg.includes('[P]')) line.classList.add('type-ping');
    else if (data.msg.includes('[F]') || data.msg.includes('[R]')) line.classList.add('type-fwd');

    const ts = String(data.ts).padStart(8, ' ');
    line.innerHTML = `<span class="ts">${ts}ms</span> <span class="node-name">${data.node}</span> ${escapeHtml(data.msg)}`;
    logDiv.appendChild(line);
    logDiv.scrollTop = logDiv.scrollHeight;
}

function escapeHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

// --- Update UI ---
function updateUI() {
    document.getElementById('sim-time').textContent = state.time_ms + 'ms';
    updateNodeList();
    updateDetail();
    updateLogTabs();
    drawTopology();
}

function updateNodeList() {
    const container = document.getElementById('node-list-items');
    container.innerHTML = '';
    for (const [name, info] of Object.entries(state.nodes || {})) {
        const div = document.createElement('div');
        div.className = 'node-item' + (name === selectedNode ? ' selected' : '');
        const dotClass = info.type === 'companion' ? 'companion' : 'repeater';
        div.innerHTML = `<div class="dot ${dotClass}"></div>
            <span>${name}</span>
            <span class="hash">${info.hash}</span>`;
        div.onclick = () => selectNode(name);
        container.appendChild(div);
    }
}

function selectNode(name) {
    selectedNode = name;
    document.getElementById('cli-node').textContent = name;
    updateUI();
}

function updateDetail() {
    const container = document.getElementById('detail-content');
    if (!selectedNode || !state.nodes[selectedNode]) {
        container.innerHTML = '<p style="color:var(--text-secondary);font-size:11px">Select a node</p>';
        return;
    }

    const n = state.nodes[selectedNode];
    let html = `
        <div class="detail-row"><span class="label">Name</span><span class="value">${n.name}</span></div>
        <div class="detail-row"><span class="label">Hash</span><span class="value">0x${n.hash}</span></div>
        <div class="detail-row"><span class="label">Type</span><span class="value">${n.type}</span></div>
        <div class="detail-row"><span class="label">Flags</span><span class="value">${n.flags}</span></div>
        <div class="detail-row"><span class="label">Time Sync</span><span class="value">${n.time_synced ? 'Yes' : 'No'}</span></div>
        <div class="detail-section"><h4>Statistics</h4>
            <div class="detail-row"><span class="label">RX</span><span class="value">${n.stats.rx}</span></div>
            <div class="detail-row"><span class="label">TX</span><span class="value">${n.stats.tx}</span></div>
            <div class="detail-row"><span class="label">FWD</span><span class="value">${n.stats.fwd}</span></div>
            <div class="detail-row"><span class="label">ERR</span><span class="value">${n.stats.err}</span></div>
            <div class="detail-row"><span class="label">ADV TX</span><span class="value">${n.stats.adv_tx}</span></div>
            <div class="detail-row"><span class="label">ADV RX</span><span class="value">${n.stats.adv_rx}</span></div>
        </div>`;

    if (n.seen_nodes && n.seen_nodes.length > 0) {
        html += '<div class="detail-section"><h4>Seen Nodes</h4>';
        for (const sn of n.seen_nodes) {
            html += `<div class="seen-node">${sn.hash} ${sn.name || '?'} rssi=${sn.rssi} pkt=${sn.pkt_count}</div>`;
        }
        html += '</div>';
    }

    if (n.neighbours && n.neighbours.length > 0) {
        html += '<div class="detail-section"><h4>Neighbours</h4>';
        for (const nb of n.neighbours) {
            html += `<div class="seen-node">${nb.hash.toString(16).padStart(2,'0').toUpperCase()} rssi=${nb.rssi}</div>`;
        }
        html += '</div>';
    }

    container.innerHTML = html;
}

function updateLogTabs() {
    const tabs = document.getElementById('log-tabs');
    const existing = new Set();
    tabs.querySelectorAll('.log-tab').forEach(t => existing.add(t.dataset.filter));

    for (const name of Object.keys(state.nodes || {})) {
        if (!existing.has(name)) {
            const btn = document.createElement('button');
            btn.className = 'log-tab' + (logFilter === name ? ' active' : '');
            btn.dataset.filter = name;
            btn.textContent = name;
            btn.onclick = () => {
                logFilter = name;
                tabs.querySelectorAll('.log-tab').forEach(t => t.classList.remove('active'));
                btn.classList.add('active');
                rebuildLog();
            };
            tabs.appendChild(btn);
        }
    }

    // "All" tab
    tabs.querySelector('[data-filter="all"]').onclick = () => {
        logFilter = 'all';
        tabs.querySelectorAll('.log-tab').forEach(t => t.classList.remove('active'));
        tabs.querySelector('[data-filter="all"]').classList.add('active');
        rebuildLog();
    };
}

function rebuildLog() {
    const logDiv = document.getElementById('log-content');
    logDiv.innerHTML = '';
    for (const data of logEntries) {
        if (logFilter !== 'all' && data.node !== logFilter) continue;
        addLog(data);
    }
}

// --- Canvas Topology ---
function resizeCanvas() {
    const rect = canvas.parentElement.getBoundingClientRect();
    canvas.width = rect.width * devicePixelRatio;
    canvas.height = rect.height * devicePixelRatio;
    canvas.style.width = rect.width + 'px';
    canvas.style.height = rect.height + 'px';
    ctx.setTransform(devicePixelRatio, 0, 0, devicePixelRatio, 0, 0);
    drawTopology();
}

window.addEventListener('resize', resizeCanvas);
setTimeout(resizeCanvas, 100);

function drawTopology() {
    const w = canvas.width / devicePixelRatio;
    const h = canvas.height / devicePixelRatio;
    ctx.clearRect(0, 0, w, h);

    // Draw links
    for (const link of (state.links || [])) {
        const na = state.nodes[link.node_a];
        const nb = state.nodes[link.node_b];
        if (!na || !nb) continue;

        ctx.beginPath();
        ctx.moveTo(na.x, na.y);
        ctx.lineTo(nb.x, nb.y);

        // Color by RSSI
        const rssi = link.rssi;
        if (rssi > -80) ctx.strokeStyle = '#4ecca3';
        else if (rssi > -100) ctx.strokeStyle = '#ffc107';
        else ctx.strokeStyle = '#e94560';

        ctx.lineWidth = 2;
        ctx.stroke();

        // Label
        const mx = (na.x + nb.x) / 2;
        const my = (na.y + nb.y) / 2;
        ctx.fillStyle = '#a0a0a0';
        ctx.font = '9px monospace';
        ctx.textAlign = 'center';
        ctx.fillText(`${rssi}dBm`, mx, my - 6);
    }

    // Draw animated packets
    const now = performance.now();
    animatedPackets = animatedPackets.filter(p => now < p.endTime);
    for (const p of animatedPackets) {
        const t = (now - p.startTime) / (p.endTime - p.startTime);
        const x = p.x1 + (p.x2 - p.x1) * t;
        const y = p.y1 + (p.y2 - p.y1) * t;
        ctx.beginPath();
        ctx.arc(x, y, 5, 0, Math.PI * 2);
        ctx.fillStyle = p.color;
        ctx.fill();
    }

    // Draw nodes
    for (const [name, info] of Object.entries(state.nodes || {})) {
        const x = info.x;
        const y = info.y;
        const isSelected = name === selectedNode;
        const isRepeater = info.type === 'repeater';

        // Shape
        ctx.beginPath();
        if (isRepeater) {
            ctx.arc(x, y, 18, 0, Math.PI * 2);
        } else {
            ctx.rect(x - 14, y - 14, 28, 28);
        }

        ctx.fillStyle = isSelected ? '#e94560' : '#16213e';
        ctx.fill();
        ctx.strokeStyle = isSelected ? '#fff' : (isRepeater ? '#4ecca3' : '#7ec8e3');
        ctx.lineWidth = isSelected ? 3 : 2;
        ctx.stroke();

        // Label
        ctx.fillStyle = '#e0e0e0';
        ctx.font = '11px monospace';
        ctx.textAlign = 'center';
        ctx.fillText(name, x, y + 32);

        // Hash
        ctx.fillStyle = '#a0a0a0';
        ctx.font = '9px monospace';
        ctx.fillText(info.hash, x, y + 4);
    }

    if (animatedPackets.length > 0) {
        requestAnimationFrame(drawTopology);
    }
}

function animatePacket(data) {
    const fromNode = state.nodes[data.from];
    if (!fromNode) return;

    const colors = { 4: '#7ec8e3', 2: '#4ecca3', 9: '#ff9800' };
    const color = colors[data.pkt_type] || '#888';

    for (const targetName of (data.targets || [])) {
        const toNode = state.nodes[targetName];
        if (!toNode) continue;
        const now = performance.now();
        animatedPackets.push({
            x1: fromNode.x, y1: fromNode.y,
            x2: toNode.x, y2: toNode.y,
            startTime: now,
            endTime: now + 400,
            color
        });
    }
    requestAnimationFrame(drawTopology);
}

// --- Canvas Interaction ---
canvas.addEventListener('mousedown', (e) => {
    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    for (const [name, info] of Object.entries(state.nodes || {})) {
        const dx = x - info.x;
        const dy = y - info.y;
        if (dx * dx + dy * dy < 400) {
            if (e.button === 0) {
                selectNode(name);
                dragNode = name;
                dragOffset = { x: dx, y: dy };
            }
            return;
        }
    }
});

canvas.addEventListener('mousemove', (e) => {
    if (!dragNode) return;
    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left - dragOffset.x;
    const y = e.clientY - rect.top - dragOffset.y;
    if (state.nodes[dragNode]) {
        state.nodes[dragNode].x = x;
        state.nodes[dragNode].y = y;
        drawTopology();
    }
});

canvas.addEventListener('mouseup', () => { dragNode = null; });

// Context menu
canvas.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    const rect = canvas.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    contextTarget = null;
    for (const [name, info] of Object.entries(state.nodes || {})) {
        const dx = x - info.x;
        const dy = y - info.y;
        if (dx * dx + dy * dy < 400) {
            contextTarget = name;
            break;
        }
    }

    if (contextTarget) {
        const menu = document.getElementById('context-menu');
        menu.style.display = 'block';
        menu.style.left = e.clientX + 'px';
        menu.style.top = e.clientY + 'px';
    }
});

document.addEventListener('click', () => {
    document.getElementById('context-menu').style.display = 'none';
});

document.querySelectorAll('.context-menu-item').forEach(item => {
    item.addEventListener('click', async () => {
        if (!contextTarget || !selectedNode) return;
        const action = item.dataset.action;
        const targetInfo = state.nodes[contextTarget];
        if (!targetInfo) return;

        if (action === 'ping') {
            await apiPost(`/api/nodes/${selectedNode}/command`, { cmd: `ping ${targetInfo.hash}` });
        } else if (action === 'trace') {
            await apiPost(`/api/nodes/${selectedNode}/command`, { cmd: `trace ${targetInfo.hash}` });
        } else if (action === 'advert') {
            await apiPost(`/api/nodes/${contextTarget}/command`, { cmd: 'advert' });
        } else if (action === 'remove') {
            await apiDelete(`/api/nodes/${contextTarget}`);
            const s = await apiGet('/api/state');
            state = s;
            updateUI();
        }
    });
});

// --- Init ---
connectWS();
apiGet('/api/state').then(s => { state = s; updateUI(); });
