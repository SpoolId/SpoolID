#include "creality_crypto.h"

#include <mbedtls/aes.h>
#include <string.h>

namespace crypto {
namespace {
// Fixed AES-128 keys embedded in the Creality firmware (from the K2-RFID reference).
// u_key: ekey derivation. d_key: spoolData block encryption.
const uint8_t U_KEY[16] = {113, 51, 98, 117, 94, 116, 49, 110, 113, 102, 90, 40, 112, 102, 36, 49};
const uint8_t D_KEY[16] = {72, 64, 67, 70, 107, 82, 110, 122, 64, 75, 65, 116, 66, 74, 112, 50};
}  // namespace

void replicateUid(const uint8_t *uid, uint8_t uidLen, uint8_t out16[16]) {
  if (uidLen == 0) {
    memset(out16, 0, 16);
    return;
  }
  for (size_t i = 0; i < 16; i++) {
    out16[i] = uid[i % uidLen];
  }
}

void aesEcb(const uint8_t in16[16], uint8_t out16[16], uint8_t mode) {
  const uint8_t *key = (mode == AES_DATA_MODE) ? D_KEY : U_KEY;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in16, out16);
  mbedtls_aes_free(&ctx);
}

void deriveEkey(const uint8_t *uid, uint8_t uidLen, uint8_t ekey[EKEY_LEN]) {
  uint8_t rep[16];
  uint8_t out[16];
  replicateUid(uid, uidLen, rep);
  aesEcb(rep, out, AES_KEY_MODE);  // encrypt replicated UID with u_key
  memcpy(ekey, out, EKEY_LEN);     // first 6 bytes => MIFARE key
}

void encryptChunk(const uint8_t in16[16], uint8_t out16[16]) {
  aesEcb(in16, out16, AES_DATA_MODE);
}

void decryptChunk(const uint8_t in16[16], uint8_t out16[16]) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_dec(&ctx, D_KEY, 128);
  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, in16, out16);
  mbedtls_aes_free(&ctx);
}

}  // namespace crypto
