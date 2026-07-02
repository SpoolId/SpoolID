#include "rfid_writer.h"

#include <MFRC522.h>
#include <SPI.h>

#include "creality_crypto.h"
#include "logger.h"

namespace rfid {
namespace {

MFRC522 mfrc(PIN_SS, PIN_RST);
int g_buzzer = -1;
String g_pending;        // staged 48-char payload
WriteResult g_last;

constexpr byte TRAILER_BLOCK = 7;          // sector 1 trailer
constexpr byte DATA_BLOCKS[3] = {4, 5, 6};

MFRC522::MIFARE_Key g_defaultKey;  // FF x6

String toHex(const uint8_t *b, size_t n) {
  String s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) {
    if (b[i] < 0x10) s += '0';
    s += String(b[i], HEX);
  }
  s.toUpperCase();
  return s;
}

// Drive a square-wave tone so passive (piezo) buzzers actually sound; active
// buzzers work with this too. ~2.7 kHz is near a typical piezo's resonance.
void beep(uint16_t ms) {
  if (g_buzzer < 0) return;
  tone(g_buzzer, 2700);
  delay(ms);
  noTone(g_buzzer);
  digitalWrite(g_buzzer, LOW);
}

void beepError() {
  beep(120);
  delay(80);
  beep(120);
}

bool isClassic(MFRC522::PICC_Type t) {
  return t == MFRC522::PICC_TYPE_MIFARE_MINI || t == MFRC522::PICC_TYPE_MIFARE_1K ||
         t == MFRC522::PICC_TYPE_MIFARE_4K;
}

// Re-activate the card after a failed auth (which leaves the PCD in a bad state).
bool reselect() {
  mfrc.PICC_HaltA();
  mfrc.PCD_StopCrypto1();
  delay(5);
  return mfrc.PICC_IsNewCardPresent() && mfrc.PICC_ReadCardSerial();
}

// Authenticate sector 1 trailer, trying the default key first, then the derived
// ekey. Sets `encrypted` true when the default key fails but the ekey works.
bool authSector(const uint8_t *ekey, bool &encrypted) {
  encrypted = false;
  MFRC522::StatusCode st = (MFRC522::StatusCode)mfrc.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A, TRAILER_BLOCK, &g_defaultKey, &mfrc.uid);
  if (st == MFRC522::STATUS_OK) return true;

  LOG_D("default key auth failed (%s), trying ekey", mfrc.GetStatusCodeName(st));
  if (!reselect()) return false;

  MFRC522::MIFARE_Key k;
  memcpy(k.keyByte, ekey, crypto::EKEY_LEN);
  st = (MFRC522::StatusCode)mfrc.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                                                  TRAILER_BLOCK, &k, &mfrc.uid);
  if (st == MFRC522::STATUS_OK) {
    encrypted = true;
    return true;
  }
  LOG_D("ekey auth failed (%s)", mfrc.GetStatusCodeName(st));
  return false;
}

}  // namespace

void begin(int buzzerPin) {
  g_buzzer = buzzerPin;
  if (g_buzzer >= 0) {
    pinMode(g_buzzer, OUTPUT);
    digitalWrite(g_buzzer, LOW);
  }
  for (byte i = 0; i < 6; i++) g_defaultKey.keyByte[i] = 0xFF;
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
  mfrc.PCD_Init();
  LOG_I("RC522 ready (v=0x%02X)", mfrc.PCD_ReadRegister(MFRC522::VersionReg));
}

void testBeep(uint16_t ms) { beep(ms); }

void stage(const String &spoolData48) {
  g_pending = spoolData48;
  g_last = WriteResult{};  // reset result for the new job
  LOG_I("staged spoolData (%u chars)", g_pending.length());
}

bool hasPending() { return g_pending.length() == 48; }
void clearPending() { g_pending = ""; }
const WriteResult &last() { return g_last; }

void poll() {
  if (!hasPending()) return;
  if (!mfrc.PICC_IsNewCardPresent() || !mfrc.PICC_ReadCardSerial()) return;

  WriteResult r;
  r.done = true;
  r.uid = toHex(mfrc.uid.uidByte, mfrc.uid.size);
  LOG_I("tag UID=%s", r.uid.c_str());

  MFRC522::PICC_Type type = mfrc.PICC_GetType(mfrc.uid.sak);
  if (!isClassic(type)) {
    r.error = String("unsupported tag: ") + mfrc.PICC_GetTypeName(type);
    LOG_E("%s", r.error.c_str());
    beepError();
    mfrc.PICC_HaltA();
    g_last = r;
    return;
  }

  uint8_t ekey[crypto::EKEY_LEN];
  crypto::deriveEkey(mfrc.uid.uidByte, mfrc.uid.size, ekey);

  if (!authSector(ekey, r.encrypted)) {
    r.error = "auth failed (default + ekey)";
    LOG_E("%s", r.error.c_str());
    beepError();
    mfrc.PICC_HaltA();
    mfrc.PCD_StopCrypto1();
    g_last = r;
    return;
  }

  // Encrypt each 16-char chunk (ASCII bytes) and write to blocks 4/5/6.
  bool wrote = true;
  for (int i = 0; i < 3; i++) {
    uint8_t plain[16], enc[16];
    for (int j = 0; j < 16; j++) plain[j] = (uint8_t)g_pending[i * 16 + j];
    crypto::encryptChunk(plain, enc);
    MFRC522::StatusCode st = mfrc.MIFARE_Write(DATA_BLOCKS[i], enc, 16);
    if (st != MFRC522::STATUS_OK) {
      r.error = String("write block ") + DATA_BLOCKS[i] + " failed: " + mfrc.GetStatusCodeName(st);
      LOG_E("%s", r.error.c_str());
      wrote = false;
      break;
    }
  }

  // Lock a fresh tag to its derived key (skip if it was already encrypted).
  if (wrote && !r.encrypted) {
    byte trailer[18];
    byte size = sizeof(trailer);
    MFRC522::StatusCode st = mfrc.MIFARE_Read(TRAILER_BLOCK, trailer, &size);
    if (st != MFRC522::STATUS_OK) {
      r.error = String("read trailer failed: ") + mfrc.GetStatusCodeName(st);
      LOG_E("%s", r.error.c_str());
      wrote = false;
    } else {
      memcpy(&trailer[0], ekey, 6);    // KEY_A
      memcpy(&trailer[10], ekey, 6);   // KEY_B (bytes 10-15); access bits 6-9 kept
      st = mfrc.MIFARE_Write(TRAILER_BLOCK, trailer, 16);
      if (st != MFRC522::STATUS_OK) {
        r.error = String("lock trailer failed: ") + mfrc.GetStatusCodeName(st);
        LOG_E("%s", r.error.c_str());
        wrote = false;
      } else {
        LOG_I("tag locked to derived key");
      }
    }
  }

  r.ok = wrote;
  if (r.ok) {
    LOG_I("write OK (encrypted=%d)", r.encrypted);
    beep(250);
    clearPending();
  } else {
    beepError();
  }
  mfrc.PICC_HaltA();
  mfrc.PCD_StopCrypto1();
  g_last = r;
}

bool dumpTag(String &outHex, String &uidHex, bool &encrypted) {
  if (!mfrc.PICC_IsNewCardPresent() || !mfrc.PICC_ReadCardSerial()) return false;
  uidHex = toHex(mfrc.uid.uidByte, mfrc.uid.size);

  uint8_t ekey[crypto::EKEY_LEN];
  crypto::deriveEkey(mfrc.uid.uidByte, mfrc.uid.size, ekey);
  if (!authSector(ekey, encrypted)) {
    mfrc.PICC_HaltA();
    mfrc.PCD_StopCrypto1();
    return false;
  }

  outHex = "";
  bool ok = true;
  for (int i = 0; i < 3; i++) {
    byte buf[18];
    byte size = sizeof(buf);
    if (mfrc.MIFARE_Read(DATA_BLOCKS[i], buf, &size) != MFRC522::STATUS_OK) {
      ok = false;
      break;
    }
    outHex += toHex(buf, 16);
  }
  mfrc.PICC_HaltA();
  mfrc.PCD_StopCrypto1();
  return ok;
}

bool readTag(String &data48, String &uidHex, bool &encrypted) {
  if (!mfrc.PICC_IsNewCardPresent() || !mfrc.PICC_ReadCardSerial()) return false;
  uidHex = toHex(mfrc.uid.uidByte, mfrc.uid.size);

  uint8_t ekey[crypto::EKEY_LEN];
  crypto::deriveEkey(mfrc.uid.uidByte, mfrc.uid.size, ekey);
  if (!authSector(ekey, encrypted)) {
    mfrc.PICC_HaltA();
    mfrc.PCD_StopCrypto1();
    return false;
  }

  data48 = "";
  bool ok = true;
  for (int i = 0; i < 3; i++) {
    byte buf[18];
    byte size = sizeof(buf);
    if (mfrc.MIFARE_Read(DATA_BLOCKS[i], buf, &size) != MFRC522::STATUS_OK) {
      ok = false;
      break;
    }
    uint8_t plain[16];
    crypto::decryptChunk(buf, plain);  // ciphertext -> 16 ASCII chars
    for (int j = 0; j < 16; j++) data48 += (char)plain[j];
  }
  mfrc.PICC_HaltA();
  mfrc.PCD_StopCrypto1();
  return ok && data48.length() == 48;
}

}  // namespace rfid
