"""Tests for MeshCrypto - AES-128-ECB, HMAC-SHA256 truncated."""

import pytest
from sim.crypto import (
    encrypt_ecb, decrypt_ecb, compute_hmac, verify_hmac,
    encrypt_then_mac, mac_then_decrypt,
    MC_AES_KEY_SIZE, MC_AES_BLOCK_SIZE, MC_CIPHER_MAC_SIZE,
)


class TestAESECB:
    def test_encrypt_decrypt_roundtrip(self):
        key = bytes(range(16))
        plaintext = b"Hello, MeshCore!"  # exactly 16 bytes

        ciphertext = encrypt_ecb(plaintext, key)
        decrypted = decrypt_ecb(ciphertext, key)

        assert decrypted[:len(plaintext)] == plaintext

    def test_encrypt_with_padding(self):
        key = bytes(range(16))
        plaintext = b"short"  # 5 bytes, will be zero-padded to 16

        ciphertext = encrypt_ecb(plaintext, key)
        assert len(ciphertext) == MC_AES_BLOCK_SIZE  # padded to 16

        decrypted = decrypt_ecb(ciphertext, key)
        # First 5 bytes match, rest is zero padding
        assert decrypted[:5] == plaintext
        assert decrypted[5:] == b'\x00' * 11

    def test_multi_block(self):
        key = bytes(range(16))
        plaintext = b"A" * 32  # exactly 2 blocks

        ciphertext = encrypt_ecb(plaintext, key)
        assert len(ciphertext) == 32

        decrypted = decrypt_ecb(ciphertext, key)
        assert decrypted == plaintext

    def test_ecb_blocks_independent(self):
        """ECB mode: identical blocks produce identical ciphertext."""
        key = bytes(range(16))
        block = b"A" * 16
        plaintext = block + block

        ciphertext = encrypt_ecb(plaintext, key)
        # In ECB, both blocks should encrypt to the same ciphertext
        assert ciphertext[:16] == ciphertext[16:]


class TestHMAC:
    def test_hmac_truncated_to_2_bytes(self):
        key = bytes(range(32))
        data = b"test data"

        mac = compute_hmac(key, data)
        assert len(mac) == MC_CIPHER_MAC_SIZE

    def test_hmac_verify(self):
        key = bytes(range(32))
        data = b"test data"

        mac = compute_hmac(key, data)
        assert verify_hmac(mac, key, data)

    def test_hmac_wrong_key_fails(self):
        key1 = bytes(range(32))
        key2 = bytes(range(1, 33))
        data = b"test data"

        mac = compute_hmac(key1, data)
        assert not verify_hmac(mac, key2, data)

    def test_hmac_wrong_data_fails(self):
        key = bytes(range(32))
        mac = compute_hmac(key, b"correct")
        assert not verify_hmac(mac, key, b"wrong")

    def test_hmac_deterministic(self):
        key = bytes(range(32))
        data = b"test"
        assert compute_hmac(key, data) == compute_hmac(key, data)


class TestEncryptThenMAC:
    def test_roundtrip(self):
        key = bytes(range(16))
        mac_key = bytes(range(32))
        plaintext = b"Hello MeshCore!"

        encrypted = encrypt_then_mac(plaintext, key, mac_key)

        # Format: [MAC:2][ciphertext]
        assert len(encrypted) >= MC_CIPHER_MAC_SIZE + MC_AES_BLOCK_SIZE

        decrypted = mac_then_decrypt(encrypted, key, mac_key)
        assert decrypted is not None
        assert decrypted[:len(plaintext)] == plaintext

    def test_mac_at_beginning(self):
        key = bytes(range(16))
        mac_key = bytes(range(32))

        encrypted = encrypt_then_mac(b"test", key, mac_key)

        # First 2 bytes are MAC, rest is ciphertext
        mac = encrypted[:MC_CIPHER_MAC_SIZE]
        ciphertext = encrypted[MC_CIPHER_MAC_SIZE:]

        # Verify MAC matches ciphertext
        assert verify_hmac(mac, mac_key, ciphertext)

    def test_tampered_mac_fails(self):
        key = bytes(range(16))
        mac_key = bytes(range(32))

        encrypted = encrypt_then_mac(b"test", key, mac_key)

        # Tamper with MAC
        tampered = bytes([encrypted[0] ^ 0xFF]) + encrypted[1:]
        result = mac_then_decrypt(tampered, key, mac_key)
        assert result is None

    def test_tampered_ciphertext_fails(self):
        key = bytes(range(16))
        mac_key = bytes(range(32))

        encrypted = encrypt_then_mac(b"test", key, mac_key)

        # Tamper with ciphertext
        tampered = encrypted[:3] + bytes([encrypted[3] ^ 0xFF]) + encrypted[4:]
        result = mac_then_decrypt(tampered, key, mac_key)
        assert result is None

    def test_too_short_input(self):
        assert mac_then_decrypt(b"\x00\x00", bytes(16), bytes(32)) is None
        assert mac_then_decrypt(b"", bytes(16), bytes(32)) is None

    def test_shared_secret_as_both_keys(self):
        """Firmware uses shared secret as both AES key and MAC key."""
        secret = bytes(range(32))
        plaintext = b"login data here"

        encrypted = encrypt_then_mac(plaintext, secret, secret)
        decrypted = mac_then_decrypt(encrypted, secret, secret)

        assert decrypted is not None
        assert decrypted[:len(plaintext)] == plaintext
