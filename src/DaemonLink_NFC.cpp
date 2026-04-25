// ============================================================================
//  DaemonLink_NFC.cpp
//  Implementacion del modulo NFC. Mantiene la salida formateada con un prefijo
//  "[NFC]" para que el parser del frontend (PWA) pueda enrutar las lineas
//  por canal.
// ============================================================================
#include "DaemonLink_NFC.h"
#include <Wire.h>

// IRQ y RESET no estan cableados en este diseño -> -1 para ambos.
// Adafruit_PN532 con ese constructor usa el modo I2C sobre `Wire` por defecto.
DaemonLink_NFC::DaemonLink_NFC()
    : _pn532(/*irq=*/-1, /*reset=*/-1),
      _ready(false),
      _fwVersion(0) {}

bool DaemonLink_NFC::begin() {
    // Levantamos el bus I2C en los pines del stack de hardware (8/9).
    Wire.begin(DAEMONLINK_PN532_SDA, DAEMONLINK_PN532_SCL);
    Wire.setClock(100000);  // PN532 es estable a 100 kHz; 400k da timeouts.

    _pn532.begin();

    _fwVersion = _pn532.getFirmwareVersion();
    if (!_fwVersion) {
        Serial.println(F("[NFC] ERROR: PN532 no responde en I2C (SDA=8 SCL=9)"));
        _ready = false;
        return false;
    }

    Serial.print(F("[NFC] PN532 OK, firmware=0x"));
    Serial.println(_fwVersion, HEX);

    // SAM = Secure Access Module. Lo dejamos en modo "normal" sin pasarela.
    // Necesario antes de cualquier readPassiveTargetID().
    _pn532.SAMConfig();

    _ready = true;
    return true;
}

bool DaemonLink_NFC::readMifareUID(uint16_t timeout_ms) {
    if (!_ready) {
        Serial.println(F("[NFC] ERROR: modulo no inicializado"));
        return false;
    }

    uint8_t uid[DAEMONLINK_NFC_UID_MAX] = {0};
    uint8_t uidLength = 0;

    Serial.print(F("[NFC] Esperando tag ISO14443A (timeout="));
    Serial.print(timeout_ms);
    Serial.println(F("ms)..."));

    bool found = _pn532.readPassiveTargetID(
        PN532_MIFARE_ISO14443A,
        uid,
        &uidLength,
        timeout_ms
    );

    if (!found) {
        Serial.println(F("[NFC] No se detecto tag (timeout)"));
        return false;
    }

    Serial.print(F("[NFC] UID len="));
    Serial.print(uidLength);
    Serial.print(F(" bytes: "));
    printUIDHex(uid, uidLength);
    Serial.println();

    // Tipo aproximado segun longitud de UID.
    if (uidLength == 4) {
        Serial.println(F("[NFC] tipo=Mifare Classic (UID de 4 bytes)"));
    } else if (uidLength == 7) {
        Serial.println(F("[NFC] tipo=Mifare Ultralight / DESFire (UID de 7 bytes)"));
    }

    return true;
}

void DaemonLink_NFC::printUIDHex(const uint8_t* uid, uint8_t length) {
    for (uint8_t i = 0; i < length; i++) {
        if (uid[i] < 0x10) Serial.print('0');
        Serial.print(uid[i], HEX);
        if (i < length - 1) Serial.print(':');
    }
}
