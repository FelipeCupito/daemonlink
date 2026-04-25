// ============================================================================
//  DaemonLink_CLI.cpp
//  Implementacion del shim CLI. Mantiene el contrato no bloqueante: el
//  hilo que llama (Marauder loop, core 1) regresa de inmediato, mientras
//  el trabajo NFC corre en una tarea FreeRTOS dedicada.
// ============================================================================
#include "DaemonLink_CLI.h"
#include "DaemonLink_NFC.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

// Instancia estatica del modulo NFC. Se construye una vez al cargar el
// firmware; begin() se difiere a DaemonLink_initCli() para garantizar
// que Wire/Serial ya esten inicializados.
DaemonLink_NFC g_nfc;

bool          g_init_ok  = false;   // PN532 respondio al firmware probe
volatile bool g_nfc_busy = false;   // hay una lectura NFC en curso

// Tarea worker: ejecuta la lectura bloqueante del PN532 sin congelar
// el dispatcher de Marauder. Se autodestruye al terminar.
void nfcReadTask(void* /*arg*/) {
    g_nfc.readMifareUID(2000);  // ventana de 2s para presentar el tag
    g_nfc_busy = false;
    vTaskDelete(nullptr);
}

void printDlHelp() {
    Serial.println(F("[SYS] DaemonLink commands:"));
    Serial.println(F("  nfc_read   - read Mifare UID via PN532 (async)"));
    Serial.println(F("  dl_help    - show this list"));
    Serial.println(F("[SYS] (Marauder native commands keep working: help, scanall, ...)"));
}

}  // namespace

void DaemonLink_initCli() {
    Serial.println(F("[SYS] DaemonLink CLI shim online"));

    if (g_nfc.begin()) {
        g_init_ok = true;
    } else {
        // begin() ya imprimio el motivo; dejamos el comando deshabilitado
        // pero NO abortamos: el resto de Marauder debe seguir funcionando.
        g_init_ok = false;
        Serial.println(F("[SYS] WARN: NFC unavailable, nfc_read disabled"));
    }
}

bool DaemonLink_handleCli(const String& input) {
    // Comparacion estricta: nunca debemos tragar comandos de Marauder.
    // Si en el futuro aceptamos argumentos (ej. "nfc_read --timeout 5000"),
    // cambiamos a startsWith() y parseamos aca.

    if (input == "nfc_read") {
        if (!g_init_ok) {
            Serial.println(F("[NFC] ERROR: module not initialized"));
            return true;
        }
        if (g_nfc_busy) {
            Serial.println(F("[NFC] BUSY: a read is already in progress"));
            return true;
        }

        g_nfc_busy = true;
        BaseType_t r = xTaskCreatePinnedToCore(
            nfcReadTask,
            "dl_nfc_read",
            4096,        // stack: PN532 + Serial buffer caben holgados
            nullptr,
            1,           // prioridad baja: por debajo del stack de RF
            nullptr,
            1            // core 1 (loop Arduino). Core 0 queda libre para WiFi/BT.
        );
        if (r != pdPASS) {
            g_nfc_busy = false;
            Serial.println(F("[NFC] ERROR: failed to spawn task"));
        } else {
            Serial.println(F("[NFC] dispatch OK (async, present tag now)"));
        }
        return true;
    }

    if (input == "dl_help") {
        printDlHelp();
        return true;
    }

    return false;  // no es nuestro -> Marauder sigue su flujo
}
