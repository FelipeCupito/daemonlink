// ============================================================================
//  DaemonLink_NFC.cpp
//  Salida en JSON minificado (una linea + '\n') para consumo de la PWA.
//  Memoria: el JsonDocument vive en el stack del caller (FreeRTOS task con
//  4-6 KB), heap interno se libera al salir de scope. Documentos pequeños
//  (<256 B), sin riesgo de OOM aun bajo presion.
// ============================================================================
#include "DaemonLink_NFC.h"
#include "DaemonLink_Json.h"
#include <Wire.h>

DaemonLink_NFC::DaemonLink_NFC()
    : _pn532(/*irq=*/-1, /*reset=*/-1),
      _ready(false),
      _fwVersion(0) {}

bool DaemonLink_NFC::begin() {
    Wire.begin(DAEMONLINK_PN532_SDA, DAEMONLINK_PN532_SCL);
    Wire.setClock(100000);  // 100 kHz estable; 400 kHz da timeouts en cableados largos.

    _pn532.begin();
    _fwVersion = _pn532.getFirmwareVersion();
    if (!_fwVersion) {
        DaemonLink::emitError("nfc", "PN532 not responding on I2C (SDA=8 SCL=9)");
        _ready = false;
        return false;
    }

    {
        JsonDocument d;
        d["source"]   = "nfc";
        d["event"]    = "ready";
        d["fw"]       = _fwVersion;       // entero — la PWA puede mostrarlo en hex
        d["sda"]      = DAEMONLINK_PN532_SDA;
        d["scl"]      = DAEMONLINK_PN532_SCL;
        DaemonLink::emitJson(d);
    }

    _pn532.SAMConfig();
    _ready = true;
    return true;
}

bool DaemonLink_NFC::readMifareUID(uint16_t timeout_ms) {
    if (!_ready) {
        DaemonLink::emitError("nfc", "module not initialized");
        return false;
    }

    {
        JsonDocument d;
        d["source"]     = "nfc";
        d["event"]      = "wait";
        d["timeout_ms"] = timeout_ms;
        DaemonLink::emitJson(d);
    }

    uint8_t uid[DAEMONLINK_NFC_UID_MAX] = {0};
    uint8_t uidLength = 0;

    bool found = _pn532.readPassiveTargetID(
        PN532_MIFARE_ISO14443A,
        uid,
        &uidLength,
        timeout_ms
    );

    if (!found) {
        JsonDocument d;
        d["source"] = "nfc";
        d["event"]  = "timeout";
        d["msg"]    = "no tag detected";
        DaemonLink::emitJson(d);
        return false;
    }

    // Format UID as "aa:bb:cc:dd" using a fixed-size stack buffer
    // (max 7 bytes -> 7*3 chars + NUL = 22).
    char uidStr[22];
    char* p = uidStr;
    for (uint8_t i = 0; i < uidLength; i++) {
        if (i) *p++ = ':';
        sprintf(p, "%02x", uid[i]);
        p += 2;
    }
    *p = '\0';

    const char* kind = "iso14443a";
    if      (uidLength == 4) kind = "mifare_classic";
    else if (uidLength == 7) kind = "mifare_ultralight_or_desfire";

    JsonDocument d;
    d["source"]   = "nfc";
    d["event"]    = "tag";
    d["type"]     = kind;
    d["uid"]      = uidStr;
    d["uid_len"]  = uidLength;
    DaemonLink::emitJson(d);
    return true;
}

// Sin uso desde la migracion a JSON, pero la dejamos por compatibilidad de la
// API publica de la clase.
void DaemonLink_NFC::printUIDHex(const uint8_t* uid, uint8_t length) {
    for (uint8_t i = 0; i < length; i++) {
        if (uid[i] < 0x10) Serial.print('0');
        Serial.print(uid[i], HEX);
        if (i < length - 1) Serial.print(':');
    }
}

// ============================================================================
//  Mifare Classic attacks (Phase I scaffolding)
//  Memoria: cada JsonDocument vive en el stack de la FreeRTOS task que
//  invoque estas funciones (los workers nfcNestedTask / nfcDumpTask se
//  crean con 6 KB de stack). Sin globals, sin static buffers.
// ============================================================================

namespace {

// Snapshot del UID de Mifare. UID corto (4B) cubre Classic 1K/4K, que es lo
// unico para lo que el ataque nested tiene sentido. Devuelve true si engancha
// un tag dentro del timeout.
bool waitForMifareClassic(Adafruit_PN532& pn532, uint8_t* uid, uint8_t* uid_len,
                          uint16_t timeout_ms) {
    return pn532.readPassiveTargetID(PN532_MIFARE_ISO14443A,
                                     uid, uid_len, timeout_ms);
}

void formatUidHex(const uint8_t* uid, uint8_t len, char* out /*>=22*/) {
    char* p = out;
    for (uint8_t i = 0; i < len; i++) {
        if (i) *p++ = ':';
        sprintf(p, "%02x", uid[i]);
        p += 2;
    }
    *p = '\0';
}

// Clave default Mifare Classic (FFFFFFFFFFFF). Se usa para el dump real del
// Sector 0 y como semilla para el ataque nested.
const uint8_t kDefaultKey[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

}  // namespace

bool DaemonLink_NFC::runNestedAttack() {
    if (!_ready) {
        DaemonLink::emitError("nfc", "module not initialized");
        return false;
    }

    // --- nested_start ---
    {
        JsonDocument d;
        d["source"] = "nfc";
        d["event"]  = "nested_start";
        d["msg"]    = "present a Mifare Classic tag";
        d["key"]    = "FFFFFFFFFFFF";
        DaemonLink::emitJson(d);
    }

    uint8_t uid[DAEMONLINK_NFC_UID_MAX] = {0};
    uint8_t uid_len = 0;
    if (!waitForMifareClassic(_pn532, uid, &uid_len, 4000)) {
        JsonDocument d;
        d["source"] = "nfc";
        d["event"]  = "nested_done";
        d["status"] = "timeout";
        d["msg"]    = "no Mifare Classic tag detected";
        DaemonLink::emitJson(d);
        return false;
    }
    if (uid_len != 4) {
        JsonDocument d;
        d["source"]  = "nfc";
        d["event"]   = "nested_done";
        d["status"]  = "unsupported";
        d["msg"]     = "tag is not Mifare Classic (UID length != 4)";
        d["uid_len"] = uid_len;
        DaemonLink::emitJson(d);
        return false;
    }

    char uid_str[22];
    formatUidHex(uid, uid_len, uid_str);

    // --- nested_progress (placeholder) -------------------------------------
    // El crypto1 real (recoleccion de pares de nonces, recuperacion de la
    // clave B desde la A) vive en una iteracion posterior. Por ahora
    // simulamos el flujo: 4 emisiones espaciadas para que la PWA muestre
    // progreso vivo, sin mentir sobre lo que esta pasando ('phase' en
    // texto deja explicito que es un esqueleto).
    const char* phases[] = { "auth_default_key", "collect_nonces",
                             "recover_key_b", "verify_key_b" };
    for (uint8_t i = 0; i < 4; ++i) {
        JsonDocument d;
        d["source"]  = "nfc";
        d["event"]   = "nested_progress";
        d["uid"]     = uid_str;
        d["phase"]   = phases[i];
        d["step"]    = (uint8_t)(i + 1);
        d["of"]      = 4;
        DaemonLink::emitJson(d);
        vTaskDelay(pdMS_TO_TICKS(400));
    }

    // --- nested_done (simulated) -------------------------------------------
    {
        JsonDocument d;
        d["source"]    = "nfc";
        d["event"]     = "nested_done";
        d["status"]    = "stub";
        d["msg"]       = "nested attack scaffolding — crypto1 not implemented yet";
        d["uid"]       = uid_str;
        d["recovered"] = "0x000000000000";  // placeholder
        DaemonLink::emitJson(d);
    }
    return true;
}

bool DaemonLink_NFC::dumpTag() {
    if (!_ready) {
        DaemonLink::emitError("nfc", "module not initialized");
        return false;
    }

    // --- dump_start ---
    {
        JsonDocument d;
        d["source"] = "nfc";
        d["event"]  = "dump_start";
        d["msg"]    = "present a Mifare Classic tag";
        d["key"]    = "FFFFFFFFFFFF";
        DaemonLink::emitJson(d);
    }

    uint8_t uid[DAEMONLINK_NFC_UID_MAX] = {0};
    uint8_t uid_len = 0;
    if (!waitForMifareClassic(_pn532, uid, &uid_len, 4000)) {
        JsonDocument d;
        d["source"] = "nfc";
        d["event"]  = "dump_done";
        d["status"] = "timeout";
        d["msg"]    = "no Mifare Classic tag detected";
        DaemonLink::emitJson(d);
        return false;
    }
    if (uid_len != 4) {
        JsonDocument d;
        d["source"]  = "nfc";
        d["event"]   = "dump_done";
        d["status"]  = "unsupported";
        d["msg"]     = "tag is not Mifare Classic 1K/4K";
        d["uid_len"] = uid_len;
        DaemonLink::emitJson(d);
        return false;
    }

    char uid_str[22];
    formatUidHex(uid, uid_len, uid_str);

    // --- Real authenticate + read of Sector 0 ------------------------------
    // Block 0 of Sector 0 contains the manufacturer block (UID + BCC + SAK +
    // ATQA + reserved). Reading it confirms the auth step worked.
    const uint8_t sector = 0;
    const uint8_t block  = 0;

    if (!_pn532.mifareclassic_AuthenticateBlock(uid, uid_len, block,
                                                /*keytype=*/0 /*=KEY_A*/,
                                                const_cast<uint8_t*>(kDefaultKey))) {
        JsonDocument d;
        d["source"] = "nfc";
        d["event"]  = "dump_done";
        d["status"] = "auth_failed";
        d["uid"]    = uid_str;
        d["sector"] = sector;
        d["msg"]    = "default key A did not authenticate";
        DaemonLink::emitJson(d);
        return false;
    }

    uint8_t data[16] = {0};
    bool read_ok = _pn532.mifareclassic_ReadDataBlock(block, data);

    char block_hex[33];
    {
        char* p = block_hex;
        for (uint8_t i = 0; i < 16; i++) { sprintf(p, "%02x", data[i]); p += 2; }
        *p = '\0';
    }

    // --- sector event (real data for sector 0) -----------------------------
    {
        JsonDocument d;
        d["source"] = "nfc";
        d["event"]  = "sector";
        d["uid"]    = uid_str;
        d["sector"] = sector;
        d["status"] = read_ok ? "ok" : "read_failed";
        if (read_ok) {
            JsonArray blocks = d["blocks"].to<JsonArray>();
            JsonObject b0 = blocks.add<JsonObject>();
            b0["block"] = block;
            b0["hex"]   = block_hex;
            b0["role"]  = "manufacturer";
        }
        DaemonLink::emitJson(d);
    }

    // --- dump_done: rest of the tag is pending -----------------------------
    {
        JsonDocument d;
        d["source"]      = "nfc";
        d["event"]       = "dump_done";
        d["status"]      = read_ok ? "partial" : "failed";
        d["uid"]         = uid_str;
        d["read_sectors"]   = read_ok ? 1 : 0;
        d["pending_sectors"]= 15;          // Mifare Classic 1K = 16 sectors total
        d["msg"]         = read_ok
                           ? "sector 0 dumped — sectors 1..15 pending in next iteration"
                           : "read failed even with valid auth";
        DaemonLink::emitJson(d);
    }
    return read_ok;
}
