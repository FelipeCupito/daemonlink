// ============================================================================
//  DaemonLink_CLI.cpp
//  Implementacion del shim CLI. Mantiene el contrato no bloqueante: el
//  hilo que llama (Marauder loop, core 1) regresa de inmediato, mientras
//  el trabajo (NFC, IR, etc.) corre en una tarea FreeRTOS dedicada.
// ============================================================================
#include "DaemonLink_CLI.h"
#include "DaemonLink_NFC.h"
#include "DaemonLink_IR.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

// Instancias estaticas de los modulos. Se construyen al cargar el
// firmware; begin() se difiere a DaemonLink_initCli() para garantizar
// que Wire/Serial/RMT ya esten inicializados.
DaemonLink_NFC g_nfc;
DaemonLink_IR  g_ir;

bool          g_nfc_ok   = false;   // PN532 respondio al firmware probe
bool          g_ir_ok    = false;   // receptor IR habilitado
volatile bool g_nfc_busy = false;   // hay una lectura NFC en curso
volatile bool g_ir_busy  = false;   // hay una captura IR en curso

// --- Tareas worker --------------------------------------------------------
// Ejecutan operaciones que pueden tardar varios segundos sin congelar el
// dispatcher de Marauder. Se autodestruyen al terminar.

void nfcReadTask(void* /*arg*/) {
    g_nfc.readMifareUID(2000);
    g_nfc_busy = false;
    vTaskDelete(nullptr);
}

void irCaptureTask(void* /*arg*/) {
    g_ir.capture(4000);          // ventana de 4s para apuntar el control
    g_ir_busy = false;
    vTaskDelete(nullptr);
}

void printDlHelp() {
    Serial.println(F("[SYS] DaemonLink commands:"));
    Serial.println(F("  nfc_read    - read Mifare UID via PN532 (async)"));
    Serial.println(F("  ir_capture  - capture & decode IR remote (async)"));
    Serial.println(F("  dl_help     - show this list"));
    Serial.println(F("[SYS] (Marauder native commands keep working: help, scanall, ...)"));
}

}  // namespace

void DaemonLink_initCli() {
    Serial.println(F("[SYS] DaemonLink CLI shim online"));

    // --- NFC ---
    if (g_nfc.begin()) {
        g_nfc_ok = true;
    } else {
        g_nfc_ok = false;
        Serial.println(F("[SYS] WARN: NFC unavailable, nfc_read disabled"));
    }

    // --- IR ---
    // begin() solo hace enableIRIn() y devuelve true; no hay protocolo de
    // detection de hardware (un VS1838B sin señal no genera trafico).
    if (g_ir.begin()) {
        g_ir_ok = true;
    } else {
        g_ir_ok = false;
        Serial.println(F("[SYS] WARN: IR receiver init failed, ir_capture disabled"));
    }
}

bool DaemonLink_handleCli(const String& input) {
    // Comparacion estricta: nunca debemos tragar comandos de Marauder.
    // Si en el futuro aceptamos argumentos (ej. "ir_capture --timeout 8000"),
    // cambiamos a startsWith() y parseamos aca.

    if (input == "nfc_read") {
        if (!g_nfc_ok) {
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

    if (input == "ir_capture") {
        if (!g_ir_ok) {
            Serial.println(F("[IR] ERROR: module not initialized"));
            return true;
        }
        if (g_ir_busy) {
            Serial.println(F("[IR] BUSY: a capture is already in progress"));
            return true;
        }

        g_ir_busy = true;
        BaseType_t r = xTaskCreatePinnedToCore(
            irCaptureTask,
            "dl_ir_cap",
            6144,        // stack mayor: resultToSourceCode arma un String grande
            nullptr,
            1,           // misma prioridad que NFC: por debajo del stack de RF
            nullptr,
            1            // core 1, mismo esquema que NFC
        );
        if (r != pdPASS) {
            g_ir_busy = false;
            Serial.println(F("[IR] ERROR: failed to spawn task"));
        } else {
            Serial.println(F("[IR] dispatch OK (async, point a remote)"));
        }
        return true;
    }

    if (input == "dl_help") {
        printDlHelp();
        return true;
    }

    return false;  // no es nuestro -> Marauder sigue su flujo
}
