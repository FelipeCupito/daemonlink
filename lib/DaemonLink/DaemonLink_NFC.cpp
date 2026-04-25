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
