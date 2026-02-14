"""
MeshCore Identity - Ed25519 keypair, hash, sign/verify.
Port of src/mesh/Identity.h using pynacl.
"""

from __future__ import annotations
from nacl.signing import SigningKey, VerifyKey
from nacl.exceptions import BadSignatureError


MC_PUBLIC_KEY_SIZE = 32
MC_PRIVATE_KEY_SIZE = 64  # orlp/ed25519 expanded key
MC_SIGNATURE_SIZE = 64
MC_NODE_NAME_MAX = 16


class Identity:
    """Ed25519 identity for a mesh node."""

    def __init__(self, name: str = "", signing_key: SigningKey | None = None):
        if signing_key is None:
            signing_key = SigningKey.generate()
        self._signing_key = signing_key
        self._verify_key = signing_key.verify_key
        self.public_key: bytes = bytes(self._verify_key)
        self.hash: int = self.public_key[0]  # first byte
        self.name: str = name or f"CC-{self.public_key[0]:02X}{self.public_key[1]:02X}{self.public_key[2]:02X}"
        self.flags: int = 0
        self.latitude: int = 0  # microdegrees (int32)
        self.longitude: int = 0  # microdegrees (int32)

    def sign(self, data: bytes) -> bytes:
        """Sign data, return 64-byte Ed25519 signature."""
        signed = self._signing_key.sign(data)
        return signed.signature  # 64 bytes

    @staticmethod
    def verify(public_key: bytes, data: bytes, signature: bytes) -> bool:
        """Verify Ed25519 signature."""
        try:
            vk = VerifyKey(public_key)
            vk.verify(data, signature)
            return True
        except (BadSignatureError, Exception):
            return False

    def set_location(self, lat: float, lon: float):
        from sim.packet import MC_FLAG_HAS_LOCATION
        self.latitude = int(lat * 1_000_000)
        self.longitude = int(lon * 1_000_000)
        if lat != 0.0 or lon != 0.0:
            self.flags |= MC_FLAG_HAS_LOCATION
        else:
            self.flags &= ~MC_FLAG_HAS_LOCATION

    def has_location(self) -> bool:
        from sim.packet import MC_FLAG_HAS_LOCATION
        return (self.flags & MC_FLAG_HAS_LOCATION) != 0
