// ============================================================================
//  DaemonLink_FS.h
//  Wrapper sobre LittleFS para persistir payloads tacticos. Monta la
//  particion etiquetada "littlefs" (definida en boards/daemonlink_partitions.csv)
//  para no colisionar con el SPIFFS que Marauder usa en settings.cpp.
//
//  Convenciones:
//    * Los archivos IR viven en /ir/<nombre>.json
//    * El nombre se valida: [A-Za-z0-9_-], 1..32 chars (sin '/', '.', '..')
//    * Cada archivo es un JSON minificado de una linea con la forma:
//        {"name":"...","payload":"NEC 32 0xff629d","ts":<millis>}
// ============================================================================
#pragma once
#ifndef DAEMONLINK_FS_H
#define DAEMONLINK_FS_H

#include <Arduino.h>
#include <ArduinoJson.h>

class DaemonLink_FS {
public:
    DaemonLink_FS();

    // Monta LittleFS sobre la particion "littlefs". Si esta vacia o
    // corrupta la formatea automaticamente. Devuelve true si quedo lista.
    bool begin();

    bool isReady() const { return _ready; }

    // --- IR payload store -------------------------------------------------
    bool saveIRPayload(const String& name, const String& payload);
    // Carga el payload IR. Devuelve "" si no existe o falla el parseo.
    String loadIRPayload(const String& name);

    // --- listing / housekeeping -------------------------------------------
    // Lista archivos en /ir y los inserta en el JsonArray como
    // {name, size, ts}. Devuelve la cantidad insertada.
    size_t listIR(JsonArray out);

    bool removeIR(const String& name);

    // --- helpers ---------------------------------------------------------
    static bool isValidName(const String& name);

    // Bytes totales / usados en la particion littlefs (para reportes).
    size_t totalBytes() const;
    size_t usedBytes()  const;

private:
    bool _ready;
    String pathFor(const String& name) const { return "/ir/" + name + ".json"; }
    void ensureDir(const char* path);
};

#endif  // DAEMONLINK_FS_H
