"""
MeshCore Crypto - AES-128-ECB + HMAC-SHA256 (truncated to 2 bytes).
Port of src/mesh/Crypto.h using pycryptodome.
"""

from __future__ import annotations
import hmac
import hashlib
from Crypto.Cipher import AES

MC_AES_KEY_SIZE = 16
MC_AES_BLOCK_SIZE = 16
MC_CIPHER_MAC_SIZE = 2
MC_SHARED_SECRET_SIZE = 32


def _zero_pad(data: bytes) -> bytes:
    """Pad data to AES block boundary with zeros."""
    padded_len = ((len(data) + MC_AES_BLOCK_SIZE - 1) // MC_AES_BLOCK_SIZE) * MC_AES_BLOCK_SIZE
    if padded_len == 0:
        padded_len = MC_AES_BLOCK_SIZE
    return data.ljust(padded_len, b'\x00')


def compute_hmac(key: bytes, data: bytes) -> bytes:
    """HMAC-SHA256 truncated to 2 bytes (MeshCore CIPHER_MAC_SIZE=2)."""
    full_mac = hmac.new(key, data, hashlib.sha256).digest()
    return full_mac[:MC_CIPHER_MAC_SIZE]


def verify_hmac(mac: bytes, key: bytes, data: bytes) -> bool:
    """Verify 2-byte truncated HMAC."""
    computed = compute_hmac(key, data)
    return hmac.compare_digest(mac, computed)


def encrypt_ecb(plaintext: bytes, key: bytes) -> bytes:
    """AES-128-ECB encrypt (block by block, no chaining)."""
    cipher = AES.new(key[:MC_AES_KEY_SIZE], AES.MODE_ECB)
    padded = _zero_pad(plaintext)
    return cipher.encrypt(padded)


def decrypt_ecb(ciphertext: bytes, key: bytes) -> bytes:
    """AES-128-ECB decrypt."""
    cipher = AES.new(key[:MC_AES_KEY_SIZE], AES.MODE_ECB)
    return cipher.decrypt(ciphertext)


def encrypt_then_mac(plaintext: bytes, key: bytes, mac_key: bytes) -> bytes:
    """
    Encrypt-then-MAC: output [MAC:2][ciphertext].
    key = first 16 bytes for AES, mac_key = full 32-byte shared secret for HMAC.
    """
    ciphertext = encrypt_ecb(plaintext, key)
    mac = compute_hmac(mac_key, ciphertext)
    return mac + ciphertext


def mac_then_decrypt(data: bytes, key: bytes, mac_key: bytes) -> bytes | None:
    """
    Verify MAC then decrypt. Input: [MAC:2][ciphertext].
    Returns plaintext or None if MAC fails.
    """
    if len(data) < MC_CIPHER_MAC_SIZE + MC_AES_BLOCK_SIZE:
        return None

    mac = data[:MC_CIPHER_MAC_SIZE]
    ciphertext = data[MC_CIPHER_MAC_SIZE:]

    if not verify_hmac(mac, mac_key, ciphertext):
        return None

    return decrypt_ecb(ciphertext, key)
