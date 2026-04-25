// ============================================================================
//  DaemonLink_IR.h
//  Modulo de captura/decodificacion infrarroja para DaemonLink.
//  Hardware: receptor VS1838B en GPIO 4 (RX), LED IR en GPIO 5 (TX, reservado
//            para una fase posterior de replay).
//  Diseñado para ser invocado desde la CLI inyectada en Marauder
//  (comando "ir_capture"), corriendo en su propia tarea de FreeRTOS.
// ============================================================================
#ifndef DAEMONLINK_IR_H
#define DAEMONLINK_IR_H

#include <Arduino.h>
#include <IRrecv.h>
#include <IRutils.h>

// --- Pinout (segun stack de hardware del proyecto) ---
#ifndef DAEMONLINK_IR_RX_PIN
#define DAEMONLINK_IR_RX_PIN 4
#endif
#ifndef DAEMONLINK_IR_TX_PIN
#define DAEMONLINK_IR_TX_PIN 5
#endif

// Buffer para protocolos largos (A/C, etc). 1024 entradas alcanzan para
// los frames mas pesados que IRremoteESP8266 conoce.
#ifndef DAEMONLINK_IR_CAPTURE_BUFFER_SIZE
#define DAEMONLINK_IR_CAPTURE_BUFFER_SIZE 1024
#endif

// Timeout entre simbolos en ms. 50 ms es el default recomendado por la
// libreria; valores mas altos capturan A/C completos a costa de latencia.
#ifndef DAEMONLINK_IR_TIMEOUT_MS
#define DAEMONLINK_IR_TIMEOUT_MS 50
#endif

class DaemonLink_IR {
public:
    DaemonLink_IR();

    // Inicializa el receptor IR. Se puede llamar varias veces; la segunda
    // vez en adelante es no-op (el IRrecv interno mantiene su estado).
    bool begin();

    // Bloquea hasta que llegue una señal IR valida o se agote el timeout.
    // Imprime por Serial protocolo, hex code y bit count con prefijo [IR].
    // Devuelve true si decodifico una señal, false si timeout.
    bool capture(uint32_t timeout_ms = 4000);

    bool isReady() const { return _ready; }

private:
    IRrecv  _recv;
    bool    _ready;
};

#endif  // DAEMONLINK_IR_H
