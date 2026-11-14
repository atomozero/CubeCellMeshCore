// Host test: MeshCore multi-byte path encoding in src/mesh/Packet.h
#include <assert.h>
#include "../../src/mesh/Packet.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    MCPacket p;
    // FLOOD GROUP_TEXT, path_len 0x42 = 2 hops x 2-byte hashes (seen on air as "pL=66")
    uint8_t raw[] = {0x15, 0x42, 0xAA,0xA1, 0xBB,0xB1, 1,2,3,4,5,6,7,8,9,10};
    CHECK(p.deserialize(raw, sizeof(raw)));
    CHECK(p.pathHashSize() == 2 && p.pathHashCount() == 2 && p.pathByteLen() == 4);
    CHECK(p.payloadLen == 10 && p.payload[0] == 1);
    uint8_t out[255];
    CHECK(p.serialize(out, sizeof(out)) == sizeof(raw) && memcmp(out, raw, sizeof(raw)) == 0);

    // Flood append of our 2-byte hash
    const uint8_t me[2] = {0x48, 0x83};
    memcpy(&p.path[p.pathHashCount() * p.pathHashSize()], me, 2);
    p.setPathHashCount(p.pathHashCount() + 1);
    CHECK(p.pathLen == 0x43 && p.pathByteLen() == 6 && p.path[4] == 0x48 && p.path[5] == 0x83);

    // 3-byte hashes, 21 hops = 63 bytes: valid; 22 hops = 66 bytes: invalid
    CHECK(MCPacket::isValidPathLen(0x80 | 21));
    CHECK(!MCPacket::isValidPathLen(0x80 | 22));
    // hash size 4 (bits 11) reserved
    CHECK(!MCPacket::isValidPathLen(0xC1));
    // 1-byte legacy: 64 hops ok, path_len 65 (= 0x41: 1 hop x 2 bytes) is now valid
    CHECK(MCPacket::isValidPathLen(0x41) && MCPacket::pathByteLenOf(0x41) == 2);

    // Truncated packet (path claims 4 bytes, only 3 present) must be rejected
    uint8_t shortPkt[] = {0x15, 0x42, 1, 2, 3};
    CHECK(!p.deserialize(shortPkt, sizeof(shortPkt)));

    // Oversize payload must be rejected, not truncated
    uint8_t big[2 + 185]; memset(big, 0, sizeof(big)); big[0] = 0x15; big[1] = 0;
    CHECK(!p.deserialize(big, sizeof(big)));
    CHECK(p.deserialize(big, sizeof(big) - 1));   // 184 bytes payload OK

    // Direct peel with 2-byte hashes: [48 83][CC DD] -> [CC DD]
    uint8_t d[] = {0x16, 0x42, 0x48,0x83, 0xCC,0xDD, 9,9};
    CHECK(p.deserialize(d, sizeof(d)));
    uint8_t hs = p.pathHashSize(), n = p.pathHashCount();
    p.setPathHashCount(n - 1); memmove(p.path, &p.path[hs], (n - 1) * hs);
    CHECK(p.pathLen == 0x41 && p.path[0] == 0xCC && p.path[1] == 0xDD && p.getTotalSize() == 2 + 2 + 2);

    // Legacy 1-byte path still works
    uint8_t l[] = {0x11, 0x02, 0x10, 0x20, 7};
    CHECK(p.deserialize(l, sizeof(l)) && p.pathHashSize() == 1 && p.pathByteLen() == 2 && p.payloadLen == 1);

    printf(fails ? "%d FAILED\n" : "All path encoding tests passed\n", fails);
    return fails != 0;
}
