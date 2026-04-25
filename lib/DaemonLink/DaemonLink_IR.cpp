// ============================================================================
//  DaemonLink_IR.cpp
//  Implementacion del modulo IR. Usa IRremoteESP8266 en modo "receive".
//  Salida formateada con prefijo "[IR]" para que la PWA pueda colorear.
// ============================================================================
#include "DaemonLink_IR.h"

// El constructor de IRrecv toma (rx_pin, buffer_size, timeout_ms, save_buffer).
// `save_buffer = true` permite seguir recibiendo mientras decodificamos el
// frame anterior, util para señales repetidas (botones mantenidos).
DaemonLink_IR::DaemonLink_IR()
    : _recv(DAEMONLINK_IR_RX_PIN,
            DAEMONLINK_IR_CAPTURE_BUFFER_SIZE,
            DAEMONLINK_IR_TIMEOUT_MS,
            /*save_buffer=*/true),
      _ready(false) {}

bool DaemonLink_IR::begin() {
    if (_ready) return true;

    _recv.enableIRIn();   // arranca el ISR de captura (RMT en ESP32)
    _ready = true;

    Serial.print(F("[IR] receiver online (RX="));
    Serial.print(DAEMONLINK_IR_RX_PIN);
    Serial.print(F(" TX="));
    Serial.print(DAEMONLINK_IR_TX_PIN);
    Serial.println(F(" reserved)"));
    return true;
}

bool DaemonLink_IR::capture(uint32_t timeout_ms) {
    if (!_ready) {
        Serial.println(F("[IR] ERROR: module not initialized"));
        return false;
    }

    Serial.print(F("[IR] capturing (timeout="));
    Serial.print(timeout_ms);
    Serial.println(F("ms), point the remote at the receiver..."));

    // Loop no-bloqueante a nivel scheduler: usamos vTaskDelay() / yield()
    // para no acapararle CPU al resto de tareas FreeRTOS (Wi-Fi, BT, etc).
    decode_results results;
    const uint32_t deadline = millis() + timeout_ms;

    while ((int32_t)(deadline - millis()) > 0) {
        if (_recv.decode(&results)) {
            // --- Protocolo + value + bits ---
            const String proto = typeToString(results.decode_type, results.repeat);

            Serial.print(F("[IR] proto="));
            Serial.print(proto);
            Serial.print(F(" bits="));
            Serial.print(results.bits);
            Serial.print(F(" hex=0x"));
            // resultToHexidecimal devuelve "0x..." para protocolos cortos
            // y un volcado de bytes para frames largos (A/C). Le sacamos
            // el prefijo "0x" para mantener una salida uniforme.
            String hexv = resultToHexidecimal(&results);
            if (hexv.startsWith("0x") || hexv.startsWith("0X")) {
                hexv = hexv.substring(2);
            }
            Serial.println(hexv);

            // Linea adicional con el formato canonico de IRremoteESP8266
            // (util para alimentar IRsend en una fase de replay):
            Serial.print(F("[IR] dump: "));
            Serial.println(resultToSourceCode(&results));

            // Aviso explicito si el protocolo no fue identificado.
            if (results.decode_type == decode_type_t::UNKNOWN) {
                Serial.println(F("[IR] WARN: unknown protocol — raw timing only"));
            }

            _recv.resume();   // habilita la siguiente captura
            return true;
        }
        // Cedemos el CPU 5 ms (~200 Hz). El RMT del ESP32 captura por DMA,
        // no perdemos eventos durante este sleep.
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    Serial.println(F("[IR] no signal detected (timeout)"));
    return false;
}
