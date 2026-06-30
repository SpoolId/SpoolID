#pragma once
#include <Arduino.h>

// Creality RFID crypto. AES-128-ECB with two fixed embedded keys (reverse-engineered
// from the Creality firmware; see K2-RFID reference project):
//   mode 0 (u_key) -> derive the per-tag MIFARE "ekey": encrypt the UID replicated to
//                     16 bytes, take the first 6 bytes of the cipher.
//   mode 1 (d_key) -> encrypt a 16-byte spoolData chunk for a data block.
// The UID is the *plaintext* for the ekey derivation, NOT the AES key.
namespace crypto {

constexpr uint8_t AES_KEY_MODE = 0;   // u_key: UID -> ekey derivation
constexpr uint8_t AES_DATA_MODE = 1;  // d_key: spoolData block encryption
constexpr size_t EKEY_LEN = 6;

// Replicate a (4-byte) UID across a 16-byte buffer (the ekey-derivation plaintext).
void replicateUid(const uint8_t *uid, uint8_t uidLen, uint8_t out16[16]);

// AES-128-ECB encrypt one 16-byte block. `mode` selects the fixed key (u_key/d_key).
void aesEcb(const uint8_t in16[16], uint8_t out16[16], uint8_t mode);

// Derive the 6-byte MIFARE key from the UID (u_key mode).
void deriveEkey(const uint8_t *uid, uint8_t uidLen, uint8_t ekey[EKEY_LEN]);

// Encrypt one 16-char spoolData chunk into a 16-byte block (d_key mode).
void encryptChunk(const uint8_t in16[16], uint8_t out16[16]);

}  // namespace crypto
