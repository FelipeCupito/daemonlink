// ============================================================================
//  DaemonLink_CLI.h
//  Shim CLI para inyectar comandos personalizados de DaemonLink en el
//  parser de ESP32 Marauder. Toda la logica vive en este modulo: el unico
//  cambio en Marauder es una linea de despacho dentro de runCommand().
//
//  Uso:
//    1. Llamar DaemonLink_initCli() desde setup() (en esp32_marauder.ino).
//    2. En CommandLine.cpp -> runCommand(): primera linea util tras el
//       early-return por input vacio:
//           if (DaemonLink_handleCli(input)) return;
//
//  Comandos registrados (Fase A):
//    - nfc_read   -> dispara una lectura de UID Mifare en una tarea
//                    FreeRTOS aislada (no bloquea el loop principal).
//    - dl_help    -> lista comandos DaemonLink (no toca el help nativo
//                    de Marauder, que sigue funcionando con `help`).
// ============================================================================
#pragma once
#ifndef DAEMONLINK_CLI_H
#define DAEMONLINK_CLI_H

#include <Arduino.h>

// Inicializa los modulos DaemonLink (NFC, IR, etc.). Debe llamarse una
// sola vez, despues de Serial.begin(), tipicamente al final de setup().
void DaemonLink_initCli();

// Intenta despachar `input` como un comando DaemonLink.
// - Devuelve true  -> el comando fue consumido (Marauder NO debe procesarlo).
// - Devuelve false -> no es nuestro; Marauder sigue su flujo normal.
// La funcion es no bloqueante: las acciones pesadas se delegan a tareas
// FreeRTOS dedicadas.
bool DaemonLink_handleCli(const String& input);

#endif  // DAEMONLINK_CLI_H
