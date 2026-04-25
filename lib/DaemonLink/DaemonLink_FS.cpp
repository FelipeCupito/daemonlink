// ============================================================================
//  DaemonLink_FS.cpp
//  Implementacion del store LittleFS. Toda la salida pasa por DaemonLink::Json
//  (los errores se serializan como cards `{"source":"fs","level":"error",...}`).
// ============================================================================
#include "DaemonLink_FS.h"
#include "DaemonLink_Json.h"
#include <LittleFS.h>

namespace {
    constexpr const char* kPartitionLabel = "littlefs";
    constexpr const char* kBasePath       = "/littlefs";
    constexpr const char* kIrDir          = "/ir";
    constexpr size_t      kMaxNameLen     = 32;
}

DaemonLink_FS::DaemonLink_FS() : _ready(false) {}

bool DaemonLink_FS::begin() {
    if (_ready) return true;

    // Argumentos de LittleFS::begin():
    //   formatOnFail = true  -> si la particion esta vacia/corrupta, formatea.
    //   basePath             -> mountpoint VFS (lo usa la libreria internamente).
    //   maxOpenFiles = 5     -> con 5 alcanza para list + read + write concurrentes.
    //   partitionLabel       -> CRITICO: separamos del "spiffs" que usa Marauder.
    if (!LittleFS.begin(/*formatOnFail=*/true, kBasePath, 5, kPartitionLabel)) {
        DaemonLink::emitError("fs", "LittleFS mount failed (partition 'littlefs' missing?)");
        _ready = false;
        return false;
    }

    ensureDir(kIrDir);

    JsonDocument d;
    d["source"] = "fs";
    d["event"]  = "ready";
    d["total"]  = LittleFS.totalBytes();
    d["used"]   = LittleFS.usedBytes();
    DaemonLink::emitJson(d);

    _ready = true;
    return true;
}

void DaemonLink_FS::ensureDir(const char* path) {
    if (LittleFS.exists(path)) return;
    LittleFS.mkdir(path);
}

bool DaemonLink_FS::isValidName(const String& name) {
    if (name.length() == 0 || name.length() > kMaxNameLen) return false;
    for (size_t i = 0; i < name.length(); ++i) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

bool DaemonLink_FS::saveIRPayload(const String& name, const String& payload) {
    if (!_ready) {
        DaemonLink::emitError("fs", "not initialized");
        return false;
    }
    if (!isValidName(name)) {
        JsonDocument d;
        d["source"] = "fs";
        d["level"]  = "error";
        d["msg"]    = "invalid file name (allowed: A-Za-z0-9_-, 1..32)";
        d["name"]   = name;
        DaemonLink::emitJson(d);
        return false;
    }
    if (payload.length() == 0) {
        DaemonLink::emitError("fs", "refused to save empty payload");
        return false;
    }

    const String path = pathFor(name);
    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) {
        JsonDocument d;
        d["source"] = "fs";
        d["level"]  = "error";
        d["msg"]    = "open for write failed";
        d["path"]   = path;
        DaemonLink::emitJson(d);
        return false;
    }

    JsonDocument doc;
    doc["name"]    = name;
    doc["payload"] = payload;
    doc["ts"]      = (uint32_t)millis();
    size_t written = serializeJson(doc, f);
    f.close();

    if (written == 0) {
        DaemonLink::emitError("fs", "serialize failed (disk full?)");
        return false;
    }

    JsonDocument d;
    d["source"] = "fs";
    d["event"]  = "save";
    d["name"]   = name;
    d["bytes"]  = written;
    DaemonLink::emitJson(d);
    return true;
}

String DaemonLink_FS::loadIRPayload(const String& name) {
    if (!_ready) {
        DaemonLink::emitError("fs", "not initialized");
        return String();
    }
    if (!isValidName(name)) {
        DaemonLink::emitError("fs", "invalid file name");
        return String();
    }

    const String path = pathFor(name);
    File f = LittleFS.open(path, FILE_READ);
    if (!f) {
        JsonDocument d;
        d["source"] = "fs";
        d["level"]  = "error";
        d["msg"]    = "file not found";
        d["name"]   = name;
        DaemonLink::emitJson(d);
        return String();
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        JsonDocument d;
        d["source"] = "fs";
        d["level"]  = "error";
        d["msg"]    = "failed to parse stored file";
        d["name"]   = name;
        d["err"]    = err.c_str();
        DaemonLink::emitJson(d);
        return String();
    }

    const char* p = doc["payload"] | "";
    return String(p);
}

size_t DaemonLink_FS::listIR(JsonArray out) {
    if (!_ready) return 0;
    File dir = LittleFS.open(kIrDir);
    if (!dir || !dir.isDirectory()) return 0;

    size_t n = 0;
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String fname(entry.name());
            // entry.name() puede venir con o sin path absoluto segun la version
            // del SDK. Normalizamos: nos quedamos con el ultimo segmento.
            int slash = fname.lastIndexOf('/');
            if (slash >= 0) fname = fname.substring(slash + 1);

            if (fname.endsWith(".json")) {
                String stem = fname.substring(0, fname.length() - 5);
                JsonObject o = out.add<JsonObject>();
                o["name"] = stem;
                o["size"] = (uint32_t)entry.size();
                // entry.getLastWrite() devuelve epoch UNIX si hay RTC; en el
                // S3 sin sync NTP es 0 -> lo omitimos en ese caso.
                time_t mt = entry.getLastWrite();
                if (mt > 0) o["mtime"] = (uint32_t)mt;
                ++n;
            }
        }
        entry = dir.openNextFile();
    }
    return n;
}

bool DaemonLink_FS::removeIR(const String& name) {
    if (!_ready) return false;
    if (!isValidName(name)) return false;
    return LittleFS.remove(pathFor(name));
}

size_t DaemonLink_FS::totalBytes() const { return LittleFS.totalBytes(); }
size_t DaemonLink_FS::usedBytes()  const { return LittleFS.usedBytes();  }
