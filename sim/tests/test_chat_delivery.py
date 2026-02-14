"""
Test: chat message delivery timing through repeater.

Scenario: CompanionA -- Repeater -- CompanionB

Verifica che quando A invia un messaggio chat (PLAIN) a B tramite il repeater,
il repeater lo inoltra SUBITO senza bisogno di un secondo messaggio.

Questo test riproduce il bug segnalato: "il messaggio arriva solo quando ne
invio un altro", per dimostrare se il problema è nel repeater o nell'app.
"""

import pytest
from sim.clock import VirtualClock
from sim.runner import SimRunner
from sim.node import SimRepeater, SimCompanion, TAG_FWD, TAG_PING
from sim.packet import (
    MCPacket, MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1,
)


class TestChatDeliveryTiming:
    """Verifica che il repeater inoltra immediatamente un messaggio singolo."""

    def setup_method(self):
        self.runner = SimRunner()
        self.alice = self.runner.add_companion("Alice", x=0, y=0)
        self.rep = self.runner.add_repeater("Relay", x=500, y=0)
        self.bob = self.runner.add_companion("Bob", x=1000, y=0)
        self.runner.set_link("Alice", "Relay", rssi=-70, snr=32)
        self.runner.set_link("Relay", "Bob", rssi=-70, snr=32)
        # Alice e Bob non si vedono direttamente

        # Time sync tutti
        self.alice.time_sync.set_time(1_700_000_000)
        self.rep.time_sync.set_time(1_700_000_000)
        self.bob.time_sync.set_time(1_700_000_000)

    def _build_chat_msg(self, src, dest_hash, text="Ciao!"):
        """Costruisce un pacchetto PLAIN simile a un messaggio chat."""
        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [src.identity.hash]
        # Formato: [dest_hash][src_hash][marker][text]
        pkt.payload = bytes([dest_hash, src.identity.hash, ord('D'), ord('P')]) + text.encode()
        # Registra nel packet cache del sender per evitare auto-echo
        pkt_id = pkt.get_packet_id()
        src.packet_cache.add_if_new(pkt_id)
        src.tx_queue.add(pkt)
        src.stats.tx_count += 1
        return pkt

    def test_single_message_delivered_without_second(self):
        """UN SOLO messaggio da Alice → Bob deve arrivare senza mandarne un secondo."""
        self._build_chat_msg(self.alice, self.bob.identity.hash, "Primo messaggio")

        # Esegui simulazione per 3 secondi (abbondante per 2 hop)
        self.runner.run(3000, tick_ms=10)

        # Verifica: il repeater ha fatto forward?
        assert self.rep.stats.fwd_count >= 1, \
            f"Repeater deve inoltrare il messaggio (fwd={self.rep.stats.fwd_count})"

        # Verifica: Bob ha ricevuto il ping/messaggio?
        bob_rx = [msg for _, msg in self.bob.log_history if "from" in msg]
        assert len(bob_rx) >= 1, \
            f"Bob deve ricevere il messaggio senza bisogno di un secondo invio. Log: {self.bob.log_history}"

    def test_single_message_repeater_forwards_immediately(self):
        """Il repeater deve accodare il forward entro il primo tick dopo la ricezione."""
        self._build_chat_msg(self.alice, self.bob.identity.hash, "Test timing")

        # Step 1: pacchetto in volo da Alice
        self.runner.run_step(10)

        # Step 2-30: attendi airtime (200ms) + un po' di margine
        for _ in range(30):
            self.runner.run_step(10)

        # A questo punto il repeater dovrebbe aver ricevuto e già accodato il forward
        fwd_logs = [msg for _, msg in self.rep.log_history if "[F]" in msg and "Q" in msg]
        assert len(fwd_logs) >= 1, \
            f"Repeater deve fare forward dopo un singolo messaggio. Log: {self.rep.log_history}"

    def test_two_messages_both_delivered(self):
        """Entrambi i messaggi devono arrivare a Bob, non solo il primo."""
        self._build_chat_msg(self.alice, self.bob.identity.hash, "Primo")
        self.runner.run(2000, tick_ms=10)

        bob_rx_after_first = len([msg for _, msg in self.bob.log_history if "from" in msg])
        assert bob_rx_after_first >= 1, "Bob deve ricevere il primo messaggio"

        self._build_chat_msg(self.alice, self.bob.identity.hash, "Secondo")
        self.runner.run(2000, tick_ms=10)

        bob_rx_after_second = len([msg for _, msg in self.bob.log_history if "from" in msg])
        assert bob_rx_after_second >= 2, \
            f"Bob deve ricevere entrambi i messaggi ({bob_rx_after_second} ricevuti)"

    def test_message_not_stuck_in_queue(self):
        """Verifica che il TxQueue del repeater si svuota dopo l'inoltro."""
        self._build_chat_msg(self.alice, self.bob.identity.hash, "Queue test")

        self.runner.run(3000, tick_ms=10)

        assert self.rep.tx_queue.count == 0, \
            f"TxQueue del repeater deve essere vuota (count={self.rep.tx_queue.count})"

    def test_delivery_latency(self):
        """Misura la latenza end-to-end: deve essere < 1 secondo (2 hop × airtime)."""
        send_time = self.runner.clock.millis()
        self._build_chat_msg(self.alice, self.bob.identity.hash, "Latency test")

        # Esegui a passi piccoli fino a quando Bob riceve
        max_steps = 500  # 5 secondi
        delivery_time = None
        for _ in range(max_steps):
            self.runner.run_step(10)
            bob_rx = [ts for ts, msg in self.bob.log_history if "from" in msg]
            if bob_rx:
                delivery_time = bob_rx[0]
                break

        assert delivery_time is not None, "Bob non ha ricevuto il messaggio entro 5s"
        latency = delivery_time - send_time
        assert latency < 1000, \
            f"Latenza troppo alta: {latency}ms (atteso <1000ms per 2 hop)"
        # Log latenza per debug
        print(f"\n  Latenza end-to-end: {latency}ms (2 hop)")


class TestChatNoDirectLink:
    """Verifica che senza link diretto, i companion dipendono dal repeater."""

    def setup_method(self):
        self.runner = SimRunner()
        self.alice = self.runner.add_companion("Alice", x=0, y=0)
        self.rep = self.runner.add_repeater("Relay", x=500, y=0)
        self.bob = self.runner.add_companion("Bob", x=1000, y=0)
        self.runner.set_link("Alice", "Relay", rssi=-70, snr=32)
        self.runner.set_link("Relay", "Bob", rssi=-70, snr=32)
        for n in [self.alice, self.rep, self.bob]:
            n.time_sync.set_time(1_700_000_000)

    def test_without_repeater_no_delivery(self):
        """Senza repeater, Alice e Bob non possono comunicare."""
        self.runner.remove_link("Alice", "Relay")
        self.runner.remove_link("Relay", "Bob")

        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [self.alice.identity.hash]
        pkt.payload = bytes([self.bob.identity.hash, self.alice.identity.hash,
                             ord('D'), ord('P')]) + b"nope"
        self.alice.packet_cache.add_if_new(pkt.get_packet_id())
        self.alice.tx_queue.add(pkt)

        self.runner.run(3000, tick_ms=10)

        bob_rx = [msg for _, msg in self.bob.log_history if "from" in msg]
        assert len(bob_rx) == 0, "Bob non deve ricevere nulla senza link"

    def test_repeater_offline_no_delivery(self):
        """Se il repeater è offline (rimosso), il messaggio non arriva."""
        self.runner.remove_node("Relay")

        pkt = MCPacket()
        pkt.set_header(MC_ROUTE_FLOOD, MC_PAYLOAD_PLAIN, MC_PAYLOAD_VER_1)
        pkt.path = [self.alice.identity.hash]
        pkt.payload = bytes([self.bob.identity.hash, self.alice.identity.hash,
                             ord('D'), ord('P')]) + b"nope"
        self.alice.packet_cache.add_if_new(pkt.get_packet_id())
        self.alice.tx_queue.add(pkt)

        self.runner.run(3000, tick_ms=10)

        bob_rx = [msg for _, msg in self.bob.log_history if "from" in msg]
        assert len(bob_rx) == 0, "Bob non deve ricevere nulla senza repeater"
