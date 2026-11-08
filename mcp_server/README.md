# NP2Debug Next MCP Server

Este servidor MCP utiliza transporte **stdio** para el cliente MCP y una tubería local de Windows para comunicarse con el emulador.

## Requisitos

- Windows.
- Python 3.10 o posterior.
- NP2Debug Next ejecutándose con el servidor IPC integrado.
- Cliente MCP compatible con servidores stdio.

## Instalación

Desde esta carpeta:

```powershell
py -m pip install -r requirements.txt
py server.py
```

Configura el cliente MCP para iniciar `server.py` con el intérprete de Python de esta instalación. El servidor no abre puertos TCP: conecta únicamente con `\\.\pipe\NP2DebugNext`.

## Herramientas incluidas

- `emulator_status`, `current_instruction`
- `emulator_resume`, `emulator_pause`, `emulator_step`, `emulator_step_over`
- `breakpoint_toggle`, `breakpoint_toggle_linear`, `breakpoints_reset`
- `dialogue_jump_arm`, `dialogue_jump_cancel`
- `scene_redirect_arm`, `scene_redirect_cancel`
- `fast_forward_arm`, `fast_forward_cancel`, `fast_forward_status`

## Protocolo IPC v1

Los mensajes son estructuras binarias little-endian empaquetadas sin alineación:

- Solicitud: `<7I8I260s` (320 bytes).
- Respuesta: `<4Ii` (20 bytes).
- Magic: `0x4432504E` (bytes ASCII `NP2D`).
- Versión: `1`.

Cada llamada MCP abre una conexión, envía una solicitud y espera el resultado de la cola de comandos del hilo del emulador. La tubería rechaza clientes remotos; no se expone un puerto de red.

## Alcance actual

Esta versión expone 16 herramientas: control de ejecución, consulta de indicadores del emulador, lectura de CS:IP, breakpoints, saltos de diálogo, redirección de escenas y estado/control de Fast Forward. Las consultas son de solo lectura y se procesan en el hilo principal mediante la cola de comandos. Lectura/escritura de memoria, snapshots, extracción/inyección y navegación avanzada siguen pendientes; no se simulan como si ya estuvieran disponibles.
