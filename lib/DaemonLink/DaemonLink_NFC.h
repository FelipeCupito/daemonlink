// ============================================================================
//  DaemonLink_NFC.h
//  Modulo NFC/RFID aislado para DaemonLink.
//  Hardware: PN532 V3 sobre I2C (SDA=GPIO8, SCL=GPIO9 en ESP32-S3).
//  Diseñado para ser invocado desde la CLI inyectada en Marauder
//  (ej. comando "nfc_read"), corriendo en su propia tarea de FreeRTOS
//  para no interferir con los modulos de RF/Wi-Fi.
// ============================================================================
#ifndef DAEMONLINK_NFC_H
#define DAEMONLINK_NFC_H

#include <Arduino.h>
#include <Adafruit_PN532.h>

// --- Pines I2C (segun stack de hardware del proyecto) ---
#ifndef DAEMONLINK_PN532_SDA
#define DAEMONLINK_PN532_SDA 8
#endif
#ifndef DAEMONLINK_PN532_SCL
#define DAEMONLINK_PN532_SCL 9
#endif

// El PN532 admite hasta 7 bytes de UID (Mifare Ultralight / DESFire).
#define DAEMONLINK_NFC_UID_MAX 7

class DaemonLink_NFC {
public:
    DaemonLink_NFC();

    // Inicializa el bus I2C en los pines configurados, verifica el firmware
    // del PN532 y deja el modulo listo para leer tags ISO14443A.
    // Devuelve true si el PN532 respondio correctamente.
    bool begin();

    // Espera (bloqueante hasta `timeout_ms`) un tag Mifare/ISO14443A,
    // imprime el UID por Serial.println() y devuelve true si lo leyo.
    bool readMifareUID(uint16_t timeout_ms = 1000);

    // Estado del modulo (true tras un begin() exitoso).
    bool isReady() const { return _ready; }

    // Version de firmware reportada por el PN532 (0 si no inicializo).
    uint32_t firmwareVersion() const { return _fwVersion; }

private:
    Adafruit_PN532 _pn532;
    bool           _ready;
    uint32_t       _fwVersion;

    static void printUIDHex(const uint8_t* uid, uint8_t length);
};

#endif  // DAEMONLINK_NFC_H
