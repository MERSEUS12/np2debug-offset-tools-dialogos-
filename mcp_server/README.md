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
- `dialogue_current` (posición e ID del diálogo actual y texto ASCII aproximado, si se detectó)
- `dialogue_catalog_refresh`, `dialogue_catalog`, `dialogue_catalog_entry` (actualización y consulta paginada del catálogo de diálogos del adaptador activo)
- `dialogue_jump_request`, `dialogue_jump_status`, `dialogue_operation_cancel` (máquina de estados del salto universal, prueba de presentación y continuidad, y cancelación)
- `scene_history` (hasta 16 aperturas recientes, con nombre BIN, dirección, redirección, caller CS:IP y 16 bytes de contexto)
- `jump_trace` (hasta 12 cambios de ESI registrados después de aplicar un salto a diálogo; incluye CS, EIP, ESI anterior y nuevo)
- `current_instruction` (desensamblado nativo de la instrucción actual: CS:IP, EIP, dirección lineal, bytes, mnemónico, operandos y longitud)
- `cpu_registers` (consulta de solo lectura de EAX..EDI, ES..GS, EIP, EFLAGS y modo protegido)
- `emulator_status`, `current_instruction`
- `emulator_resume`, `emulator_pause`, `emulator_step`, `emulator_step_over`
- `breakpoint_toggle`, `breakpoint_toggle_linear`, `breakpoints_reset`
- `dialogue_jump_arm`, `dialogue_jump_cancel` (mecanismo de salto de bajo nivel; se conserva por compatibilidad)
- `scene_redirect_arm`, `scene_redirect_cancel`
- `fast_forward_arm`, `fast_forward_cancel`, `fast_forward_status`

## Protocolo IPC v2

Los mensajes son estructuras binarias little-endian empaquetadas sin alineación:

- Solicitud: `<7I8I260s` (320 bytes).
- Respuesta: `<4IiI256s` (280 bytes): magic, versión, estado, ID, resultado firmado, longitud y payload.
- Payload máximo: 256 bytes.
- **Consulta de diálogo (`DIALOGUE_CURRENT`, comando 19):** `result=1` indica que hay diálogo detectado y devuelve ESI, ID, inicio del bloque y texto ASCII terminado en NUL; `result=0` y payload vacío indican que todavía no hay diálogo disponible. El encabezado binario ocupa 12 bytes y el payload completo no supera 256 bytes.
- **Traza de salto (`JUMP_TRACE`, comando 21):** payload de 4 bytes de encabezado (cantidad) y hasta 12 registros de 14 bytes (`CS` uint16, `EIP` uint32, `old_esi` uint32, `new_esi` uint32), máximo 172 bytes. Orden nativo, del registro más antiguo al más reciente.
- **Instrucción actual (`CURRENT_INSTRUCTION`, comando 22):** payload fijo de 96 bytes: `CS:u16`, `IP:u16`, `EIP:u32`, dirección lineal `u32`, longitud `u8`, 15 bytes de instrucción, mnemónico ASCII de 24 bytes y operandos ASCII de 44 bytes. Se decodifica con el desensamblador nativo ya utilizado por Step Over.
- **Registros (`CPU_REGISTERS`, comando 23):** payload fijo de 53 bytes: ocho registros generales `u32`, seis selectores de segmento `u16`, `EIP:u32`, `EFLAGS:u32` y un byte de modo protegido. Consulta de solo lectura.
- **Historial de escenas (`SCENE_HISTORY`, comando 20):** consulta en páginas de hasta cinco registros. Cada registro contiene nombre de archivo (16 bytes), dirección lineal, indicador de redirección, caller CS:IP y 16 bytes de contexto. El orden es más reciente primero; el encabezado contiene total, offset y cantidad devuelta. El payload máximo por página es 217 bytes.
- **Lectura escalar (`MEMORY_READ`):** el emulador devuelve `result=1` y un payload de exactamente 4 bytes (`payload_length=4`) para cualquiera de los tamaños solicitados (1, 2 o 4). El valor se codifica en little-endian; los bytes superiores quedan a cero cuando el tamaño solicitado es menor que 4. El servidor MCP interpreta únicamente los primeros 1, 2 o 4 bytes según el parámetro `size`.
- **Lectura por bloques (`MEMORY_READ_BLOCK`):** devuelve `result=1` y un payload cuya longitud coincide con los bytes solicitados.
- **Catálogo de diálogos (`DIALOGUE_CATALOG_REFRESH`, comando 25):** devuelve total, generación de catálogo y bandera de truncamiento en 13 bytes. El núcleo conserva esa generación y rechaza con `STALE_CATALOG` cualquier localizador resuelto desde una generación distinta.
- **Entrada de catálogo (`DIALOGUE_CATALOG_GET`, comando 26):** recibe el índice en `a` y devuelve longitud de ID, longitud de texto UTF-8, flags, ID ASCII y texto UTF-8. La respuesta ocupa como máximo 256 bytes.
- **Solicitud de salto universal (`DIALOGUE_JUMP_REQUEST`, comando 27):** recibe el ID estable del diálogo en `script_name`. Requiere que el catálogo se haya actualizado; si el script cambia después de enumerarlo, el núcleo rechaza el destino con `STALE_CATALOG`. El resultado positivo solo confirma que la solicitud fue aceptada por la máquina de estados; no significa que el salto ya haya terminado.
- **Cancelación universal (`DIALOGUE_JUMP_CANCEL`, comando 28):** cancela la operación en curso y permite que el adaptador aplique su política de cancelación.
- **Estado universal (`DIALOGUE_STATUS_QUERY`, comando 29):** devuelve estado, error, contador de ticks, adaptador, IDs solicitados/resueltos y flags de prueba. El éxito exige `STATE_VERIFIED`, `PRESENTED` y `CONTINUATION_READY`; un cambio de ESI por sí solo no es suficiente.
- El catálogo y el adaptador PC-98 inicial solo enumeran el script activo detectado. El núcleo universal no interpreta IDs, offsets, segmentos ni marcadores de motor; cada motor/formato futuro necesita su propio adaptador.

- Magic: `0x4432504E` (bytes ASCII `NP2D`).
- Versión: `2`.

Cada llamada MCP abre una conexión, envía una solicitud y espera el resultado de la cola de comandos del hilo del emulador. La tubería rechaza clientes remotos; no se expone un puerto de red.

## Alcance actual

Esta versión expone 29 herramientas. `memory_read` conserva la lectura escalar de 1, 2 o 4 bytes; `memory_read_block` lee entre 1 y 256 bytes en espacio físico o lógico. Ambas lecturas se ejecutan en el hilo principal mediante la cola de comandos y utilizan las funciones de memoria del núcleo. La escritura de memoria, snapshots, extracción/inyección y navegación avanzada siguen pendientes. El salto universal ya está cableado al MCP y al adaptador PC-98 inicial, pero la validación final de continuidad requiere completar las pruebas de ejecución reales sin confundir aceptación de una solicitud con éxito del salto.
