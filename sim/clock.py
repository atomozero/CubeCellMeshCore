"""Virtual clock for deterministic simulation. Replaces millis()."""


class VirtualClock:
    def __init__(self):
        self._ms = 0

    def millis(self) -> int:
        return self._ms

    def advance(self, ms: int):
        self._ms += ms

    def advance_to(self, target_ms: int):
        if target_ms > self._ms:
            self._ms = target_ms

    def reset(self):
        self._ms = 0
