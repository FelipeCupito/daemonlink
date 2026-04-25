// ============================================================================
//  DaemonLink_CLI.cpp
//  Implementacion del shim CLI. Mantiene el contrato no bloqueante: el
//  hilo que llama (Marauder loop, core 1) regresa de inmediato, mientras
//  el trabajo (NFC, IR, etc.) corre en una tarea FreeRTOS dedicada.
// ============================================================================
#include "DaemonLink_CLI.h"
#include "DaemonLink_NFC.h"
#include "DaemonLink_IR.h"
#include "DaemonLink_FS.h"
#include "DaemonLink_Json.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

// Instancias estaticas de los modulos. Se construyen al cargar el
// firmware; begin() se difiere a DaemonLink_initCli() para garantizar
// que Wire/Serial/RMT ya esten inicializados.
DaemonLink_NFC g_nfc;
DaemonLink_IR  g_ir;
DaemonLink_FS  g_fs;

bool          g_nfc_ok   = false;   // PN532 respondio al firmware probe
bool          g_ir_ok    = false;   // receptor IR habilitado
bool          g_fs_ok    = false;   // LittleFS montada
volatile bool g_nfc_busy = false;   // hay una lectura NFC en curso
volatile bool g_ir_busy  = false;   // hay una captura/tx IR en curso

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

// Worker para transmision IR. Recibe el payload en heap (String*) — la
// String original vive solo durante la llamada a DaemonLink_handleCli().
void irSendTask(void* arg) {
    String* payload = static_cast<String*>(arg);
    g_ir.send(*payload);
    delete payload;
    g_ir_busy = false;
    vTaskDelete(nullptr);
}

// Worker para `ir_play <name>`: hace I/O sobre LittleFS + transmision.
// Compartimos g_ir_busy con capture/send para serializar el bus IR.
void irPlayTask(void* arg) {
    String* name = static_cast<String*>(arg);
    String payload = g_fs.loadIRPayload(*name);
    if (payload.length() > 0) {
        // Eco previo: la PWA puede correlacionar el play con el send.
        JsonDocument d;
        d["source"] = "ir";
        d["event"]  = "play";
        d["name"]   = name->c_str();
        DaemonLink::emitJson(d);
        g_ir.send(payload);
    }
    delete name;
    g_ir_busy = false;
    vTaskDelete(nullptr);
}

void printDlHelp() {
    JsonDocument d;
    d["source"] = "sys";
    d["event"]  = "help";
    JsonArray cmds = d["commands"].to<JsonArray>();

    auto add = [&](const char* name, const char* desc) {
        JsonObject c = cmds.add<JsonObject>();
        c["name"] = name;
        c["desc"] = desc;
    };
    add("nfc_read",   "read Mifare UID via PN532 (async)");
    add("ir_capture", "capture & decode IR remote (async)");
    add("ir_send",    "transmit IR: '<PROTO> <bits> <hex>' or 'raw <khz> <us,...>'");
    add("ir_save",    "ir_save <name> — persist last capture to LittleFS");
    add("ir_play",    "ir_play <name> — load saved payload from LittleFS and transmit");
    add("fs_list",    "list saved IR payloads as JSON");
    add("dl_help",    "show this list");

    d["note"] = "Marauder native commands keep working (help, scanall, ...)";
    DaemonLink::emitJson(d);
}

}  // namespace

void DaemonLink_initCli() {
    DaemonLink::emitInfo("sys", "DaemonLink CLI shim online");

    // --- NFC ---
    g_nfc_ok = g_nfc.begin();
    if (!g_nfc_ok) {
        DaemonLink::emitError("sys", "NFC unavailable, nfc_read disabled");
    }

    // --- IR ---
    // begin() devuelve true sin probe de hardware (un VS1838B sin señal
    // no genera trafico). Mantenemos el patron por simetria.
    g_ir_ok = g_ir.begin();
    if (!g_ir_ok) {
        DaemonLink::emitError("sys", "IR init failed, ir_capture/ir_send disabled");
    }

    // --- LittleFS ---
    g_fs_ok = g_fs.begin();
    if (!g_fs_ok) {
        DaemonLink::emitError("sys", "FS init failed, ir_save/ir_play/fs_list disabled");
    }
}

bool DaemonLink_handleCli(const String& input) {
    // Comparacion estricta: nunca debemos tragar comandos de Marauder.
    // Si en el futuro aceptamos argumentos (ej. "ir_capture --timeout 8000"),
    // cambiamos a startsWith() y parseamos aca.

    if (input == "nfc_read") {
        if (!g_nfc_ok) {
            DaemonLink::emitError("nfc", "module not initialized");
            return true;
        }
        if (g_nfc_busy) {
            DaemonLink::emitError("nfc", "BUSY — a read is already in progress");
            return true;
        }

        g_nfc_busy = true;
        BaseType_t r = xTaskCreatePinnedToCore(
            nfcReadTask,
            "dl_nfc_read",
            4096,        // stack: PN532 + Serial + JsonDocument caben holgados
            nullptr,
            1,           // prioridad baja: por debajo del stack de RF
            nullptr,
            1            // core 1 (loop Arduino). Core 0 queda libre para WiFi/BT.
        );
        if (r != pdPASS) {
            g_nfc_busy = false;
            DaemonLink::emitError("nfc", "failed to spawn task");
        } else {
            DaemonLink::emitInfo("nfc", "dispatch ok (async)");
        }
        return true;
    }

    if (input == "ir_capture") {
        if (!g_ir_ok) {
            DaemonLink::emitError("ir", "module not initialized");
            return true;
        }
        if (g_ir_busy) {
            DaemonLink::emitError("ir", "BUSY — another IR op is in progress");
            return true;
        }

        g_ir_busy = true;
        BaseType_t r = xTaskCreatePinnedToCore(
            irCaptureTask,
            "dl_ir_cap",
            6144,        // stack mayor: replay String + JsonDocument
            nullptr,
            1,
            nullptr,
            1
        );
        if (r != pdPASS) {
            g_ir_busy = false;
            DaemonLink::emitError("ir", "failed to spawn task");
        } else {
            DaemonLink::emitInfo("ir", "dispatch ok (capture)");
        }
        return true;
    }

    // ir_send acepta argumentos -> startsWith() en vez de igualdad estricta.
    // Reusa el flag g_ir_busy: TX y RX comparten el modulo IR, no
    // queremos que se solapen aunque sean perifericos distintos.
    if (input.startsWith("ir_send ") || input == "ir_send") {
        if (!g_ir_ok) {
            DaemonLink::emitError("ir", "module not initialized");
            return true;
        }
        if (g_ir_busy) {
            DaemonLink::emitError("ir", "BUSY — another IR op is in progress");
            return true;
        }

        // Heap-copy del payload: la String del input se libera en cuanto
        // volvemos al parser de Marauder.
        String* payload = new String(input.length() > 8 ? input.substring(8) : "");

        g_ir_busy = true;
        BaseType_t r = xTaskCreatePinnedToCore(
            irSendTask,
            "dl_ir_send",
            6144,
            payload,
            1,
            nullptr,
            1
        );
        if (r != pdPASS) {
            delete payload;
            g_ir_busy = false;
            DaemonLink::emitError("ir", "failed to spawn task");
        } else {
            DaemonLink::emitInfo("ir", "dispatch ok (send)");
        }
        return true;
    }

    // ----- ir_save <name> ----------------------------------------------
    // Sincronico: escribir LittleFS toma <50 ms y no bloquea RF.
    if (input.startsWith("ir_save ") || input == "ir_save") {
        if (!g_fs_ok) { DaemonLink::emitError("fs", "module not initialized"); return true; }
        if (!g_ir_ok) { DaemonLink::emitError("ir", "module not initialized"); return true; }

        String name = input.length() > 8 ? input.substring(8) : "";
        name.trim();
        if (name.length() == 0) {
            DaemonLink::emitError("fs", "ir_save requires a <name>");
            return true;
        }
        if (!g_ir.hasLastCapture()) {
            DaemonLink::emitError("ir", "no capture in RAM — run ir_capture first");
            return true;
        }

        g_fs.saveIRPayload(name, g_ir.lastReplay());
        return true;
    }

    // ----- ir_play <name> ----------------------------------------------
    if (input.startsWith("ir_play ") || input == "ir_play") {
        if (!g_fs_ok) { DaemonLink::emitError("fs", "module not initialized"); return true; }
        if (!g_ir_ok) { DaemonLink::emitError("ir", "module not initialized"); return true; }
        if (g_ir_busy) {
            DaemonLink::emitError("ir", "BUSY — another IR op is in progress");
            return true;
        }

        String raw = input.length() > 8 ? input.substring(8) : "";
        raw.trim();
        if (raw.length() == 0) {
            DaemonLink::emitError("fs", "ir_play requires a <name>");
            return true;
        }

        String* name = new String(raw);
        g_ir_busy = true;
        BaseType_t r = xTaskCreatePinnedToCore(
            irPlayTask, "dl_ir_play", 6144, name, 1, nullptr, 1
        );
        if (r != pdPASS) {
            delete name;
            g_ir_busy = false;
            DaemonLink::emitError("ir", "failed to spawn task");
        } else {
            DaemonLink::emitInfo("ir", "dispatch ok (play)");
        }
        return true;
    }

    // ----- fs_list ------------------------------------------------------
    if (input == "fs_list") {
        if (!g_fs_ok) { DaemonLink::emitError("fs", "module not initialized"); return true; }

        JsonDocument d;
        d["source"] = "fs";
        d["event"]  = "list";
        d["path"]   = "/ir";
        d["total"]  = (uint32_t)g_fs.totalBytes();
        d["used"]   = (uint32_t)g_fs.usedBytes();
        JsonArray items = d["items"].to<JsonArray>();
        size_t n = g_fs.listIR(items);
        d["count"] = (uint32_t)n;
        DaemonLink::emitJson(d);
        return true;
    }

    if (input == "dl_help") {
        printDlHelp();
        return true;
    }

    return false;  // no es nuestro -> Marauder sigue su flujo
}
