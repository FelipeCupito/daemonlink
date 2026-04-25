// ============================================================================
//  DaemonLink_IR.cpp
//  Implementacion del modulo IR. Usa IRremoteESP8266 en modo "receive".
//  Salida formateada con prefijo "[IR]" para que la PWA pueda colorear.
// ============================================================================
#include "DaemonLink_IR.h"

// El constructor de IRrecv toma (rx_pin, buffer_size, timeout_ms, save_buffer).
// `save_buffer = true` permite seguir recibiendo mientras decodificamos el
// frame anterior, util para señales repetidas (botones mantenidos).
// IRsend usa LEDC (PWM) en el pin TX — peripheral distinto del RMT que
// usa el receptor, asi que coexisten sin conflicto.
DaemonLink_IR::DaemonLink_IR()
    : _recv(DAEMONLINK_IR_RX_PIN,
            DAEMONLINK_IR_CAPTURE_BUFFER_SIZE,
            DAEMONLINK_IR_TIMEOUT_MS,
            /*save_buffer=*/true),
      _emit(DAEMONLINK_IR_TX_PIN),
      _ready(false) {}

bool DaemonLink_IR::begin() {
    if (_ready) return true;

    _recv.enableIRIn();   // arranca el ISR de captura (RMT)
    _emit.begin();        // configura LEDC para modular ~38 kHz por defecto
    _ready = true;

    Serial.print(F("[IR] online (RX="));
    Serial.print(DAEMONLINK_IR_RX_PIN);
    Serial.print(F(" TX="));
    Serial.print(DAEMONLINK_IR_TX_PIN);
    Serial.println(F(")"));
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

            // Linea "replay:" lista para alimentar `ir_send` desde la PWA.
            // Formato canonico DaemonLink — el unico que nuestro parser
            // de send() acepta. Para protocolos conocidos: "PROTO bits hex".
            // Para UNKNOWN: "raw 38 us,us,us,..." (asumimos modulacion 38 kHz,
            // estandar en ~95% de los controles consumer).
            if (results.decode_type != decode_type_t::UNKNOWN) {
                Serial.print(F("[IR] replay: "));
                Serial.print(typeToString(results.decode_type));
                Serial.print(' ');
                Serial.print(results.bits);
                Serial.print(F(" 0x"));
                Serial.println(uint64ToString(results.value, 16));
            } else {
                Serial.print(F("[IR] replay: raw 38 "));
                // rawbuf[0] es el "gap" inicial, no se transmite — empezamos en 1.
                // kRawTick (= 2us tipicamente) convierte ticks a microsegundos.
                for (uint16_t i = 1; i < results.rawlen; ++i) {
                    Serial.print(results.rawbuf[i] * kRawTick);
                    if (i < results.rawlen - 1) Serial.print(',');
                }
                Serial.println();
                Serial.println(F("[IR] WARN: unknown protocol — raw timing only"));
            }

            // Linea adicional con el formato C++ canonico de la libreria
            // (util si el usuario quiere copiarlo a un sketch propio).
            Serial.print(F("[IR] dump: "));
            Serial.println(resultToSourceCode(&results));

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

// ============================================================================
//  Transmision (Fase D)
// ============================================================================

bool DaemonLink_IR::send(const String& payload) {
    if (!_ready) {
        Serial.println(F("[IR] ERROR: module not initialized"));
        return false;
    }

    String s = payload;
    s.trim();
    if (s.length() == 0) {
        Serial.println(F("[IR] usage: ir_send <PROTOCOL> <bits> <hex>"));
        Serial.println(F("[IR]    or: ir_send raw <khz> <us,us,us,...>"));
        return false;
    }

    int sp = s.indexOf(' ');
    if (sp < 0) {
        Serial.println(F("[IR] ERROR: missing arguments"));
        return false;
    }
    String head = s.substring(0, sp);
    String rest = s.substring(sp + 1);
    rest.trim();

    if (head.equalsIgnoreCase("raw")) {
        return sendRawFromString(rest);
    }
    return sendProtocolFromString(head, rest);
}

bool DaemonLink_IR::sendProtocolFromString(const String& proto, const String& rest) {
    int sp = rest.indexOf(' ');
    if (sp < 0) {
        Serial.println(F("[IR] ERROR: expected '<PROTO> <bits> <hex>'"));
        return false;
    }
    String bitsStr = rest.substring(0, sp);
    String hexStr  = rest.substring(sp + 1);
    bitsStr.trim();
    hexStr.trim();

    if (hexStr.startsWith("0x") || hexStr.startsWith("0X")) {
        hexStr = hexStr.substring(2);
    }

    decode_type_t type = strToDecodeType(proto.c_str());
    if (type == decode_type_t::UNKNOWN || type == decode_type_t::UNUSED) {
        Serial.print(F("[IR] ERROR: unknown protocol '"));
        Serial.print(proto);
        Serial.println('\'');
        return false;
    }

    int bits = bitsStr.toInt();
    if (bits <= 0 || bits > 64) {
        Serial.print(F("[IR] ERROR: invalid bit count: "));
        Serial.println(bitsStr);
        return false;
    }

    uint64_t value = strtoull(hexStr.c_str(), nullptr, 16);

    Serial.print(F("[IR] tx proto="));
    Serial.print(proto);
    Serial.print(F(" bits="));
    Serial.print(bits);
    Serial.print(F(" hex=0x"));
    Serial.println(hexStr);

    bool ok = _emit.send(type, value, bits);
    Serial.println(ok ? F("[IR] tx OK") : F("[IR] ERROR: protocol not implemented for TX"));
    return ok;
}

bool DaemonLink_IR::sendRawFromString(const String& rest) {
    int sp = rest.indexOf(' ');
    if (sp < 0) {
        Serial.println(F("[IR] ERROR: expected 'raw <khz> <us,us,...>'"));
        return false;
    }
    int khz = rest.substring(0, sp).toInt();
    if (khz < 30 || khz > 60) {
        // Sanity: fuera de [30..60] kHz no es IR consumer estandar.
        Serial.print(F("[IR] ERROR: implausible carrier (expect ~38kHz), got "));
        Serial.println(khz);
        return false;
    }

    String csv = rest.substring(sp + 1);
    csv.trim();
    if (csv.length() == 0) {
        Serial.println(F("[IR] ERROR: empty raw timing list"));
        return false;
    }

    // Conteo previo: comas + 1. Limite defensivo igual al buffer de RX.
    uint16_t count = 1;
    for (size_t i = 0; i < csv.length(); ++i) {
        if (csv[i] == ',') count++;
    }
    if (count > DAEMONLINK_IR_CAPTURE_BUFFER_SIZE) {
        Serial.print(F("[IR] ERROR: raw payload too long ("));
        Serial.print(count);
        Serial.print(F(" entries, max "));
        Serial.print(DAEMONLINK_IR_CAPTURE_BUFFER_SIZE);
        Serial.println(')');
        return false;
    }

    uint16_t* buf = (uint16_t*)malloc(count * sizeof(uint16_t));
    if (!buf) {
        Serial.println(F("[IR] ERROR: out of memory for raw buffer"));
        return false;
    }

    uint16_t idx = 0;
    int start = 0;
    for (int i = 0; i <= (int)csv.length(); ++i) {
        if (i == (int)csv.length() || csv[i] == ',') {
            String tok = csv.substring(start, i);
            tok.trim();
            buf[idx++] = (uint16_t)tok.toInt();
            start = i + 1;
        }
    }

    Serial.print(F("[IR] tx raw len="));
    Serial.print(idx);
    Serial.print(F(" carrier="));
    Serial.print(khz);
    Serial.println(F("kHz"));

    _emit.sendRaw(buf, idx, (uint16_t)khz);
    Serial.println(F("[IR] tx OK"));
    free(buf);
    return true;
}
