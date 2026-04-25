// ============================================================================
//  DaemonLink_Json.h
//  Pequeño wrapper sobre ArduinoJson v7 para emitir mensajes estructurados
//  por Serial. Convenciones DaemonLink:
//    * Una sola linea por mensaje, terminada en '\n'.
//    * Formato minificado (serializeJson, NO serializeJsonPretty).
//    * Campo "source" obligatorio: identifica el modulo emisor para la PWA
//      ("nfc", "ir", "sys"). Permite ruteo en el frontend sin parsear el
//      resto del documento.
//    * Errores se serializan tambien como JSON con {"source": "...", "level":
//      "error", "msg": "..."} — la PWA los renderiza como cards rojas.
//
//  Memoria: usamos JsonDocument con allocator dinamico (heap), pero las
//  instancias viven en el STACK del caller (siempre dentro de una FreeRTOS
//  task con stack >= 4KB). El heap se libera deterministicamente al
//  destruirse el JsonDocument al final del scope.
// ============================================================================
#pragma once
#ifndef DAEMONLINK_JSON_H
#define DAEMONLINK_JSON_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace DaemonLink {

// Emite un JsonDocument por Serial como linea minificada + '\n'.
// Marca _printed=true para que el caller pueda detectar ENOMEM.
inline void emitJson(JsonDocument& doc) {
    serializeJson(doc, Serial);
    Serial.println();
}

// Emite un mensaje de error estandarizado.
// Uso: DaemonLink::emitError("nfc", "init failed: PN532 not on bus");
inline void emitError(const char* source, const char* msg) {
    JsonDocument d;
    d["source"] = source;
    d["level"]  = "error";
    d["msg"]    = msg;
    emitJson(d);
}

// Mensaje informativo del sistema.
inline void emitInfo(const char* source, const char* msg) {
    JsonDocument d;
    d["source"] = source;
    d["level"]  = "info";
    d["msg"]    = msg;
    emitJson(d);
}

}  // namespace DaemonLink

#endif  // DAEMONLINK_JSON_H
