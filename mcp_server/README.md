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

- `memory_read` (lectura escalar de 1, 2 o 4 bytes; espacio físico o lógico)
- `memory_read_block` (lectura contigua de 1 a 256 bytes; espacio físico o lógico)
- `emulator_status`, `current_instruction`
- `emulator_resume`, `emulator_pause`, `emulator_step`, `emulator_step_over`
- `breakpoint_toggle`, `breakpoint_toggle_linear`, `breakpoints_reset`
- `dialogue_jump_arm`, `dialogue_jump_cancel`
- `scene_redirect_arm`, `scene_redirect_cancel`
- `fast_forward_arm`, `fast_forward_cancel`, `fast_forward_status`

## Protocolo IPC v2

Los mensajes son estructuras binarias little-endian empaquetadas sin alineación:

- Solicitud: `<7I8I260s` (320 bytes).
- Respuesta: `<4IiI256s` (280 bytes): magic, versión, estado, ID, resultado firmado, longitud y payload.
- Payload máximo: 256 bytes.
- **Lectura escalar (`MEMORY_READ`):** el emulador devuelve `result=1` y un payload de exactamente 4 bytes (`payload_length=4`) para cualquiera de los tamaños solicitados (1, 2 o 4). El valor se codifica en little-endian; los bytes superiores quedan a cero cuando el tamaño solicitado es menor que 4. El servidor MCP interpreta únicamente los primeros 1, 2 o 4 bytes según el parámetro `size`.
- **Lectura por bloques (`MEMORY_READ_BLOCK`):** devuelve `result=1` y un payload cuya longitud coincide con los bytes solicitados.
- Magic: `0x4432504E` (bytes ASCII `NP2D`).
- Versión: `2`.

Cada llamada MCP abre una conexión, envía una solicitud y espera el resultado de la cola de comandos del hilo del emulador. La tubería rechaza clientes remotos; no se expone un puerto de red.

## Alcance actual

Esta versión expone 18 herramientas. `memory_read` conserva la lectura escalar de 1, 2 o 4 bytes; `memory_read_block` lee entre 1 y 256 bytes en espacio físico o lógico. Ambas lecturas se ejecutan en el hilo principal mediante la cola de comandos y utilizan las funciones de memoria del núcleo. La escritura de memoria, snapshots, extracción/inyección y navegación avanzada siguen pendientes.
