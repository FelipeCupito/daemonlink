// ============================================================================
//  DaemonLink_IR.cpp
//  Implementacion del modulo IR. Usa IRremoteESP8266 en modo "receive".
//  Salida formateada con prefijo "[IR]" para que la PWA pueda colorear.
// ============================================================================
#include "DaemonLink_IR.h"
#include "DaemonLink_Json.h"

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
      _ready(false),
      _lastCapture(""),
      _lastCaptureMs(0) {}

bool DaemonLink_IR::begin() {
    if (_ready) return true;

    _recv.enableIRIn();   // arranca el ISR de captura (RMT)
    _emit.begin();        // configura LEDC para modular ~38 kHz por defecto
    _ready = true;

    JsonDocument d;
    d["source"] = "ir";
    d["event"]  = "ready";
    d["rx"]     = DAEMONLINK_IR_RX_PIN;
    d["tx"]     = DAEMONLINK_IR_TX_PIN;
    DaemonLink::emitJson(d);
    return true;
}

bool DaemonLink_IR::capture(uint32_t timeout_ms) {
    if (!_ready) {
        DaemonLink::emitError("ir", "module not initialized");
        return false;
    }

    {
        JsonDocument d;
        d["source"]     = "ir";
        d["event"]      = "wait";
        d["action"]     = "capture";
        d["timeout_ms"] = timeout_ms;
        DaemonLink::emitJson(d);
    }

    decode_results results;
    const uint32_t deadline = millis() + timeout_ms;

    while ((int32_t)(deadline - millis()) > 0) {
        if (_recv.decode(&results)) {
            const bool isUnknown = (results.decode_type == decode_type_t::UNKNOWN);
            const String proto   = typeToString(results.decode_type);

            // --- hex value (sin prefijo 0x para uniformidad) ---
            String hexv = resultToHexidecimal(&results);
            if (hexv.startsWith("0x") || hexv.startsWith("0X")) hexv = hexv.substring(2);

            JsonDocument d;
            d["source"]   = "ir";
            d["event"]    = "capture";
            d["protocol"] = isUnknown ? "UNKNOWN" : proto.c_str();
            d["bits"]     = results.bits;
            d["hex"]      = hexv.c_str();
            d["repeat"]   = results.repeat;

            // "replay" — payload listo para alimentar ir_send sin reformatear.
            // Para protocolos conocidos: "PROTO bits 0xHEX".
            // Para UNKNOWN: "raw 38 us,us,us,..." (38 kHz estandar consumer).
            String rep;
            if (!isUnknown) {
                rep = proto + " " + String(results.bits) + " 0x" +
                      uint64ToString(results.value, 16);
            } else {
                // rawbuf[0] es el gap inicial; empezamos en 1.
                rep = "raw 38 ";
                for (uint16_t i = 1; i < results.rawlen; ++i) {
                    rep += String(results.rawbuf[i] * kRawTick);
                    if (i < results.rawlen - 1) rep += ',';
                }
                d["raw_len"] = results.rawlen - 1;
            }
            d["replay"] = rep.c_str();

            // Cache en RAM para `ir_save`. Sobrescribe el anterior.
            _lastCapture   = rep;
            _lastCaptureMs = millis();

            DaemonLink::emitJson(d);
            _recv.resume();
            return true;
        }
        // Yield 5 ms — RMT del ESP32 muestrea por DMA, no perdemos bordes.
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    JsonDocument d;
    d["source"] = "ir";
    d["event"]  = "timeout";
    d["msg"]    = "no signal detected";
    DaemonLink::emitJson(d);
    return false;
}

// ============================================================================
//  Transmision (Fase D)
// ============================================================================

bool DaemonLink_IR::send(const String& payload) {
    if (!_ready) {
        DaemonLink::emitError("ir", "module not initialized");
        return false;
    }

    String s = payload;
    s.trim();
    if (s.length() == 0) {
        DaemonLink::emitError("ir", "empty payload — expected '<PROTO> <bits> <hex>' or 'raw <khz> <us,...>'");
        return false;
    }

    int sp = s.indexOf(' ');
    if (sp < 0) {
        DaemonLink::emitError("ir", "missing arguments");
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
        DaemonLink::emitError("ir", "expected '<PROTO> <bits> <hex>'");
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
        JsonDocument d;
        d["source"]   = "ir";
        d["level"]    = "error";
        d["msg"]      = "unknown protocol";
        d["protocol"] = proto.c_str();
        DaemonLink::emitJson(d);
        return false;
    }

    int bits = bitsStr.toInt();
    if (bits <= 0 || bits > 64) {
        JsonDocument d;
        d["source"] = "ir";
        d["level"]  = "error";
        d["msg"]    = "invalid bit count";
        d["bits"]   = bitsStr.c_str();
        DaemonLink::emitJson(d);
        return false;
    }

    uint64_t value = strtoull(hexStr.c_str(), nullptr, 16);
    bool ok = _emit.send(type, value, bits);

    JsonDocument d;
    d["source"]   = "ir";
    d["event"]    = "send";
    d["mode"]     = "protocol";
    d["protocol"] = proto.c_str();
    d["bits"]     = bits;
    d["hex"]      = hexStr.c_str();
    d["status"]   = ok ? "ok" : "unsupported";
    if (!ok) d["msg"] = "protocol not implemented for TX";
    DaemonLink::emitJson(d);
    return ok;
}

bool DaemonLink_IR::sendRawFromString(const String& rest) {
    int sp = rest.indexOf(' ');
    if (sp < 0) {
        DaemonLink::emitError("ir", "expected 'raw <khz> <us,us,...>'");
        return false;
    }
    int khz = rest.substring(0, sp).toInt();
    if (khz < 30 || khz > 60) {
        JsonDocument d;
        d["source"]  = "ir";
        d["level"]   = "error";
        d["msg"]     = "implausible carrier (expect ~38kHz)";
        d["khz"]     = khz;
        DaemonLink::emitJson(d);
        return false;
    }

    String csv = rest.substring(sp + 1);
    csv.trim();
    if (csv.length() == 0) {
        DaemonLink::emitError("ir", "empty raw timing list");
        return false;
    }

    uint16_t count = 1;
    for (size_t i = 0; i < csv.length(); ++i) {
        if (csv[i] == ',') count++;
    }
    if (count > DAEMONLINK_IR_CAPTURE_BUFFER_SIZE) {
        JsonDocument d;
        d["source"]  = "ir";
        d["level"]   = "error";
        d["msg"]     = "raw payload too long";
        d["entries"] = count;
        d["max"]     = DAEMONLINK_IR_CAPTURE_BUFFER_SIZE;
        DaemonLink::emitJson(d);
        return false;
    }

    uint16_t* buf = (uint16_t*)malloc(count * sizeof(uint16_t));
    if (!buf) {
        DaemonLink::emitError("ir", "out of memory for raw buffer");
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

    _emit.sendRaw(buf, idx, (uint16_t)khz);
    free(buf);

    JsonDocument d;
    d["source"]  = "ir";
    d["event"]   = "send";
    d["mode"]    = "raw";
    d["khz"]     = khz;
    d["entries"] = idx;
    d["status"]  = "ok";
    DaemonLink::emitJson(d);
    return true;
}
