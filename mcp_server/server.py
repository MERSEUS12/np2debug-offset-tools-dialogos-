"""NP2Debug Next MCP stdio bridge for the Windows named-pipe transport."""

from __future__ import annotations

import json
import struct
from typing import Sequence

from mcp.server.fastmcp import FastMCP

PIPE_NAME = r"\\.\pipe\NP2DebugNext"
MAGIC = 0x4432504E
VERSION = 2
NP2DN_IPC_TIMEOUT_STATUS = 4

# Must match the packed NP2DN_IpcRequest / NP2DN_IpcResponse in np2dn_ipc.h.
REQUEST = struct.Struct("<7I8I260s")
RESPONSE = struct.Struct("<4IiI256s")

CMD_RESUME = 1
CMD_PAUSE = 2
CMD_STEP = 3
CMD_STEP_OVER = 4
CMD_BREAKPOINT_TOGGLE = 5
CMD_BREAKPOINT_TOGGLE_LINEAR = 6
CMD_BREAKPOINTS_RESET = 7
CMD_JUMP_ARM = 8
CMD_JUMP_CANCEL = 9
CMD_SCENE_ARM = 10
CMD_SCENE_CANCEL = 11
CMD_SKIP_ARM = 12
CMD_SKIP_CANCEL = 13
CMD_STATUS_QUERY = 14
CMD_CURRENT_CSIP = 15
CMD_SKIP_STATUS_QUERY = 16
CMD_MEMORY_READ = 17
CMD_MEMORY_READ_BLOCK = 18
CMD_DIALOGUE_CURRENT = 19
CMD_SCENE_HISTORY = 20
CMD_JUMP_TRACE = 21
CMD_CURRENT_INSTRUCTION = 22
CMD_CPU_REGISTERS = 23
CMD_BREAKPOINTS_LIST = 24
CMD_DIALOGUE_CATALOG_REFRESH = 25
CMD_DIALOGUE_CATALOG_GET = 26
CMD_DIALOGUE_JUMP_REQUEST = 27
CMD_DIALOGUE_JUMP_CANCEL = 28
CMD_DIALOGUE_STATUS_QUERY = 29
CMD_DOS_TRACKER_STATUS = 30

mcp = FastMCP("np2debug-next")


def _u32(value: int, field: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"{field} debe ser un entero")
    if value < 0 or value > 0xFFFFFFFF:
        raise ValueError(f"{field} debe estar entre 0 y 4294967295")
    return value


def _u64(value: int, field: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"{field} debe ser un entero")
    if value < 0 or value > 0xFFFFFFFFFFFFFFFF:
        raise ValueError(f"{field} debe estar entre 0 y 18446744073709551615")
    return value


def _u16(value: int, field: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise ValueError(f"{field} debe ser un entero")
    if value < 0 or value > 0xFFFF:
        raise ValueError(f"{field} debe estar entre 0 y 65535")
    return value


def _read_exact(pipe, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        part = pipe.read(size - len(chunks))
        if not part:
            raise RuntimeError("El emulador cerró la conexión IPC antes de responder")
        chunks.extend(part)
    return bytes(chunks)


def _send_command(
    command_type: int,
    a: int = 0,
    b: int = 0,
    c: int = 0,
    addresses: Sequence[int] = (),
    script_name: str = "",
) -> str:
    if len(addresses) > 8:
        raise ValueError("El motor admite como máximo 8 direcciones CS:IP para un salto")

    address_values = [_u32(value, "allowed_cs_ip") for value in addresses]
    address_values.extend([0] * (8 - len(address_values)))

    try:
        script_bytes = script_name.encode("mbcs")
    except (LookupError, UnicodeEncodeError) as exc:
        raise ValueError("El nombre del script no se puede convertir a la codificación ANSI de Windows") from exc
    if len(script_bytes) >= 260:
        raise ValueError("El nombre del script debe ocupar menos de 260 bytes")
    script_field = script_bytes + b"\0" * (260 - len(script_bytes))

    request = REQUEST.pack(
        MAGIC,
        VERSION,
        command_type,
        _u32(a, "a"),
        _u32(b, "b"),
        _u32(c, "c"),
        len(addresses),
        *address_values,
        script_field,
    )

    try:
        with open(PIPE_NAME, "r+b", buffering=0) as pipe:
            pipe.write(request)
            magic, version, status, command_id, result, payload_length, payload = RESPONSE.unpack(
                _read_exact(pipe, RESPONSE.size)
            )
    except OSError as exc:
        raise RuntimeError(
            "No se pudo conectar con NP2Debug Next. Comprueba que el emulador "
            "esté abierto y que su servidor IPC esté iniciado."
        ) from exc

    if magic != MAGIC or version != VERSION:
        raise RuntimeError("Respuesta IPC incompatible: revisa la versión del emulador y del servidor MCP")
    if status == 1:
        rejection_reasons = {
            0: "el emulador no proporciona diagnóstico (posible ejecutable anterior)",
            1: "magic IPC incorrecto",
            2: "versión IPC incompatible",
            3: "identificador de comando inferior al rango admitido",
            4: "identificador de comando superior al rango admitido",
            5: "address_count supera el máximo de 8",
        }
        reason = rejection_reasons.get(result, f"código de diagnóstico desconocido {result}")
        raise ValueError(f"El emulador rechazó la orden IPC: {reason}")
    if status == 2:
        raise RuntimeError("La cola de comandos está llena; vuelve a intentarlo")
    if status == 3:
        raise RuntimeError("El emulador está cerrando el servidor IPC")
    if status == 4:
        raise TimeoutError(
            f"La orden IPC {command_id} superó el límite de espera de 15 segundos; "
            "se canceló si aún estaba pendiente y se descartará su resultado si ya se ejecutaba."
        )
    if status != 0:
        raise RuntimeError(f"Estado IPC desconocido: {status}")
    if payload_length > 256:
        raise RuntimeError("Respuesta IPC inválida: longitud de payload superior a 256 bytes")
    if command_type in (CMD_MEMORY_READ, CMD_MEMORY_READ_BLOCK) and result < 0:
        raise RuntimeError("El emulador rechazó la lectura de memoria: dirección, tamaño o espacio inválido")
    if command_type == CMD_DIALOGUE_CURRENT and result not in (0, 1):
        raise RuntimeError("Respuesta IPC inválida para seguimiento de diálogo")
    if command_type in (CMD_DIALOGUE_CATALOG_REFRESH, CMD_DIALOGUE_STATUS_QUERY, CMD_DOS_TRACKER_STATUS) and result != 1:
        raise RuntimeError("El emulador no pudo completar la consulta del catálogo/estado de diálogos")
    if command_type == CMD_DIALOGUE_CATALOG_GET and result not in (0, 1):
        raise RuntimeError("Respuesta inválida al consultar una entrada del catálogo")
    if command_type == CMD_DIALOGUE_JUMP_REQUEST and result != 1:
        raise RuntimeError("El núcleo rechazó la solicitud de salto de diálogo")
    if command_type == CMD_DIALOGUE_JUMP_CANCEL and result != 1:
        raise RuntimeError("El núcleo no pudo cancelar la operación de salto")
    if command_type in (CMD_SCENE_HISTORY, CMD_JUMP_TRACE) and result != 1:
        raise RuntimeError("Respuesta IPC inválida para historial de escenas")
    if command_type in (CMD_CURRENT_INSTRUCTION, CMD_CPU_REGISTERS) and result != 1:
        raise RuntimeError("El emulador no pudo completar la consulta de CPU")
    if command_type == CMD_CURRENT_INSTRUCTION and payload_length != 96:
        raise RuntimeError("Respuesta IPC inválida: payload de instrucción debe ocupar 96 bytes")
    if command_type == CMD_CPU_REGISTERS and payload_length != 53:
        raise RuntimeError("Respuesta IPC inválida: payload de registros debe ocupar 53 bytes")
    if command_type == CMD_DOS_TRACKER_STATUS and (payload_length < 76 or (payload_length - 76) % 46 != 0):
        raise RuntimeError("Respuesta IPC inválida: payload del rastreador DOS incoherente")
    if command_type == CMD_BREAKPOINTS_LIST and (result != 1 or payload_length < 12 or (payload_length - 12) % 5 != 0):
        raise RuntimeError("Respuesta IPC inválida: payload de breakpoints incoherente")
    if command_type == CMD_SCENE_HISTORY and (payload_length < 12 or (payload_length - 12) % 41 != 0):
        raise RuntimeError("Respuesta IPC inválida: payload de historial incoherente")
    if command_type == CMD_JUMP_TRACE and (payload_length < 4 or (payload_length - 4) % 14 != 0):
        raise RuntimeError("Respuesta IPC inválida: payload de jump_trace incoherente")
    if command_type == CMD_DIALOGUE_CURRENT and ((result == 0 and payload_length != 0) or (result == 1 and not 13 <= payload_length <= 256)):
        raise RuntimeError("Respuesta IPC inválida: payload de diálogo incoherente")
    if command_type == CMD_MEMORY_READ and (result != 1 or payload_length != 4):
        raise RuntimeError(
            "Respuesta IPC inválida para lectura escalar de memoria: "
            f"command_id={command_id}, result={result}, "
            f"payload_length={payload_length}, expected_result=1, expected_payload_length=4"
        )
    if command_type == CMD_MEMORY_READ_BLOCK and result != 1:
        raise RuntimeError("El emulador no pudo completar la lectura de memoria")

    response_data = {"command_id": command_id, "result": result}
    if payload_length:
        response_data["payload_hex"] = payload[:payload_length].hex().upper()
        response_data["payload_length"] = payload_length
    return json.dumps(response_data, ensure_ascii=False)


@mcp.tool()
def dialogue_tracker_status() -> str:
    """Diagnóstico de solo lectura del rastreador DOS: entradas INT 21h, operaciones y archivos registrados."""
    stats = None
    files = []
    pending_names = {0: "NONE", 1: "OPEN", 2: "READ", 3: "SEEK", 4: "CLOSE"}
    for offset in range(0, 16, 3):
        response = json.loads(_send_command(CMD_DOS_TRACKER_STATUS, offset, 3))
        payload = bytes.fromhex(response.get("payload_hex", ""))
        if len(payload) != response.get("payload_length") or len(payload) < 112 or (len(payload) - 112) % 46:
            raise RuntimeError("Payload inválido del diagnóstico del rastreador DOS")
        values = struct.unpack_from("<28I", payload, 0)
        if values[17] != offset:
            raise RuntimeError("Paginación incoherente en el diagnóstico del rastreador DOS")
        if stats is None:
            names = ("int21_hits", "ah_3d_open", "ah_3f_read", "ah_42_seek",
                     "ah_3e_close", "ah_other", "begin_open", "begin_read",
                     "begin_seek", "begin_close", "completed_ok", "completed_error")
            stats = {name: value for name, value in zip(names, values[:12])}
            stats.update({
                "pending_operation": pending_names.get(values[12], f"UNKNOWN_{values[12]}"),
                "last_ah": values[13],
                "last_ah_hex": f"0x{values[13]:02X}",
                "last_cs": values[14],
                "last_ip": values[15],
                "tracked_file_count": values[16],
            })
            stats["vector_diagnostic"] = {
                "check_count": values[19],
                "last_current_linear": values[20],
                "last_current_linear_hex": f"0x{values[20]:05X}",
                "last_vector_linear": values[21],
                "last_vector_linear_hex": f"0x{values[21]:05X}",
                "vector_offset": values[22],
                "vector_offset_hex": f"0x{values[22]:04X}",
                "vector_segment": values[23],
                "vector_segment_hex": f"0x{values[23]:04X}",
                "cs_base": values[24],
                "cs_base_hex": f"0x{values[24]:05X}",
                "cpu_cs": values[25],
                "cpu_cs_hex": f"0x{values[25]:04X}",
                "cpu_eip": values[26],
                "cpu_eip_hex": f"0x{values[26]:08X}",
                "protected_mode": bool(values[27] & 1),
                "virtual_8086_mode": bool(values[27] & 2),
            }
        returned = values[18]
        if returned != (len(payload) - 112) // 46 or returned > 3:
            raise RuntimeError("Número de registros incoherente en el diagnóstico DOS")
        for i in range(returned):
            record = struct.unpack_from("<H16sHHHIIIIIH", payload, 112 + i * 46)
            slot, raw_name, handle, segment, base_offset, file_position, file_size, loaded_size, coverage_prefix, sequence, flags = record
            name = raw_name.split(b"\0", 1)[0].decode("ascii", errors="replace")
            files.append({
                "slot": slot, "name": name, "handle": handle,
                "segment": segment, "segment_hex": f"0x{segment:04X}",
                "base_offset": base_offset, "base_offset_hex": f"0x{base_offset:04X}",
                "file_position": file_position, "file_size": file_size,
                "loaded_size": loaded_size, "coverage_prefix": coverage_prefix,
                "sequence": sequence, "closed": bool(flags & 1),
                "mapping_seen": bool(flags & 2), "mapping_valid": bool(flags & 4),
                "size_known": bool(flags & 8), "fully_loaded": bool(flags & 16),
            })
    stats = stats or {}
    return json.dumps({
        "stats": stats,
        "tracked_files": files,
        "read_only": True,
        "note": "vector_diagnostic.check_count cuenta las comprobaciones del vector; int21_hits cuenta las coincidencias exactas. La consulta es de solo lectura y no altera el estado del emulador.",
    }, ensure_ascii=False)


@mcp.tool()
def dialogue_current() -> str:
    """Consulta el último diálogo detectado por el seguimiento nativo del emulador."""
    response = json.loads(_send_command(CMD_DIALOGUE_CURRENT))
    if response.get("result") == 0:
        return json.dumps({
            "available": False,
            "reason": "Todavía no se ha detectado un marcador de espera de diálogo \\HA.",
        }, ensure_ascii=False)

    payload = bytes.fromhex(response.get("payload_hex", ""))
    if len(payload) != response.get("payload_length") or len(payload) < 13 or payload[-1] != 0:
        raise RuntimeError("Payload de diálogo inválido: longitud o terminador incorrecto")
    esi, dialogue_id, block_start = struct.unpack_from("<IiI", payload, 0)
    text_bytes = payload[12:-1]
    try:
        dialogue_text = text_bytes.decode("ascii")
    except UnicodeDecodeError as exc:
        raise RuntimeError("El emulador devolvió texto de diálogo no ASCII") from exc
    return json.dumps({
        "available": True,
        "dialogue_id": dialogue_id,
        "esi": esi,
        "esi_hex": f"0x{esi:08X}",
        "block_start": block_start,
        "block_start_hex": f"0x{block_start:08X}",
        "text": dialogue_text,
        "text_encoding": "ASCII approximation",
        "text_note": "Los bytes no imprimibles o fuera de ASCII se representan como puntos; no es una decodificación Shift-JIS.",
        "command_id": response.get("command_id"),
    }, ensure_ascii=False)


@mcp.tool()
def dialogue_catalog_refresh() -> str:
    """Reconstruye el catálogo de diálogos del script activo y devuelve su generación."""
    response = json.loads(_send_command(CMD_DIALOGUE_CATALOG_REFRESH))
    payload = bytes.fromhex(response.get("payload_hex", ""))
    if len(payload) != 13:
        raise RuntimeError("Respuesta inválida al actualizar el catálogo de diálogos")
    count = struct.unpack_from("<I", payload, 0)[0]
    generation = struct.unpack_from("<Q", payload, 4)[0]
    truncated = bool(payload[12])
    return json.dumps({
        "count": count,
        "generation": generation,
        "truncated": truncated,
        "adapter": "pc98-ha-script",
        "note": "El catálogo corresponde al script activo detectado; la API universal mantiene IDs y localizadores opacos al núcleo.",
    }, ensure_ascii=False)


@mcp.tool()
def dialogue_catalog_entry(index: int) -> str:
    """Obtiene un diálogo del catálogo por índice, incluyendo su ID estable y texto UTF-8."""
    index = _u32(index, "index")
    response = json.loads(_send_command(CMD_DIALOGUE_CATALOG_GET, index))
    if response.get("result") == 0:
        raise IndexError(f"No existe una entrada de catálogo en el índice {index}")
    payload = bytes.fromhex(response.get("payload_hex", ""))
    if len(payload) < 6:
        raise RuntimeError("Payload incompleto para la entrada del catálogo")
    id_length, text_length, flags = struct.unpack_from("<BBI", payload, 0)
    if len(payload) != 6 + id_length + text_length:
        raise RuntimeError("Longitudes incoherentes en la entrada del catálogo")
    try:
        dialogue_id = payload[6:6 + id_length].decode("ascii")
        dialogue_text = payload[6 + id_length:].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError("El catálogo devolvió un ID o texto con codificación inválida") from exc
    return json.dumps({
        "index": index,
        "dialogue_id": dialogue_id,
        "text": dialogue_text,
        "text_encoding": "UTF-8",
        "truncated": bool(flags & 1),
        "flags": flags,
    }, ensure_ascii=False)


@mcp.tool()
def dialogue_catalog(offset: int = 0, limit: int = 32, refresh: bool = True) -> str:
    """Lista una página del catálogo activo. Límite máximo: 128 diálogos por consulta."""
    offset = _u32(offset, "offset")
    limit = _u32(limit, "limit")
    if limit < 1 or limit > 128:
        raise ValueError("limit debe estar entre 1 y 128")
    catalog = json.loads(dialogue_catalog_refresh()) if refresh else None
    entries = []
    for index in range(offset, offset + limit):
        response = json.loads(_send_command(CMD_DIALOGUE_CATALOG_GET, index))
        if response.get("result") == 0:
            break
        payload = bytes.fromhex(response.get("payload_hex", ""))
        if len(payload) < 6:
            raise RuntimeError("Payload incompleto para la entrada del catálogo")
        id_length, text_length, flags = struct.unpack_from("<BBI", payload, 0)
        if len(payload) != 6 + id_length + text_length:
            raise RuntimeError("Longitudes incoherentes en la entrada del catálogo")
        try:
            dialogue_id = payload[6:6 + id_length].decode("ascii")
            dialogue_text = payload[6 + id_length:].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise RuntimeError("El catálogo devolvió un ID o texto con codificación inválida") from exc
        entries.append({
            "index": index,
            "dialogue_id": dialogue_id,
            "text": dialogue_text,
            "truncated": bool(flags & 1),
        })
    return json.dumps({
        "catalog": catalog,
        "offset": offset,
        "returned": len(entries),
        "entries": entries,
        "next_offset": offset + len(entries) if len(entries) == limit else None,
    }, ensure_ascii=False)


@mcp.tool()
def dialogue_jump_request(dialogue_id: str, catalog_generation: int) -> str:
    """Solicita un salto usando el ID y la generación devueltos por dialogue_catalog; rechaza catálogos obsoletos."""
    if not isinstance(dialogue_id, str) or not dialogue_id or "\0" in dialogue_id:
        raise ValueError("dialogue_id no puede estar vacío ni contener NUL")
    catalog_generation = _u64(catalog_generation, "catalog_generation")
    if catalog_generation == 0:
        raise ValueError("catalog_generation debe ser una generación válida")
    try:
        encoded_id = dialogue_id.encode("mbcs")
    except UnicodeEncodeError as exc:
        raise ValueError("dialogue_id no se puede convertir a la codificación ANSI de Windows") from exc
    if len(encoded_id) >= 96:
        raise ValueError("dialogue_id debe ocupar menos de 96 bytes")
    return _send_command(
        CMD_DIALOGUE_JUMP_REQUEST,
        a=catalog_generation & 0xffffffff,
        b=(catalog_generation >> 32) & 0xffffffff,
        script_name=dialogue_id,
    )


@mcp.tool()
def dialogue_jump_status() -> str:
    """Devuelve el estado de la máquina de salto, incluida la verificación de continuidad."""
    response = json.loads(_send_command(CMD_DIALOGUE_STATUS_QUERY))
    payload = bytes.fromhex(response.get("payload_hex", ""))
    if len(payload) < 28:
        raise RuntimeError("Payload incompleto del estado de salto")
    state, error, ticks, adapter_index = struct.unpack_from("<IIII", payload, 0)
    rollback_available = bool(payload[16])
    before_flags, after_flags = struct.unpack_from("<II", payload, 17)
    adapter_len, requested_len, resolved_len = payload[25:28]
    if len(payload) != 28 + adapter_len + requested_len + resolved_len:
        raise RuntimeError("Longitudes incoherentes en el estado de salto")
    pos = 28
    try:
        adapter = payload[pos:pos + adapter_len].decode("ascii")
        pos += adapter_len
        requested = payload[pos:pos + requested_len].decode("utf-8")
        pos += requested_len
        resolved = payload[pos:pos + resolved_len].decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RuntimeError("Codificación inválida en el estado de salto") from exc
    states = ["IDLE", "OBSERVING", "RESOLVING", "VALIDATING", "PREPARING",
              "APPLYING", "VERIFYING", "ROLLING_BACK", "SUCCEEDED", "FAILED",
              "CANCELLED"]
    errors = ["NONE", "NO_ADAPTER", "INVALID_ARGUMENT", "BUSY",
              "OBSERVE_FAILED", "TARGET_NOT_FOUND", "VALIDATION_FAILED",
              "PREPARE_FAILED", "APPLY_FAILED", "VERIFY_FAILED",
              "CONTINUITY_FAILED", "ROLLBACK_FAILED", "ADAPTER_CONTRACT",
              "STALE_CATALOG"]
    return json.dumps({
        "state": states[state] if state < len(states) else f"UNKNOWN_{state}",
        "state_code": state,
        "error": errors[error] if error < len(errors) else f"UNKNOWN_{error}",
        "error_code": error,
        "tick_count": ticks,
        "adapter_index": adapter_index,
        "adapter_id": adapter,
        "requested_dialogue_id": requested,
        "resolved_dialogue_id": resolved,
        "rollback_available": rollback_available,
        "before_flags": before_flags,
        "after_flags": after_flags,
        "proof": {
            "state_verified": bool(after_flags & 1),
            "presented": bool(after_flags & 2),
            "continuation_ready": bool(after_flags & 4),
        },
    }, ensure_ascii=False)


@mcp.tool()
def dialogue_jump_cancel() -> str:
    """Cancela la operación de salto universal en curso."""
    return _send_command(CMD_DIALOGUE_JUMP_CANCEL)


@mcp.tool()
def scene_history() -> str:
    """Devuelve las últimas aperturas de archivos del juego, con caller y contexto de código."""
    page_size = 5
    entries = []
    total_count = None

    for offset in range(0, 16, page_size):
        response = json.loads(_send_command(CMD_SCENE_HISTORY, offset, page_size))
        payload = bytes.fromhex(response.get("payload_hex", ""))
        if len(payload) != response.get("payload_length") or len(payload) < 12:
            raise RuntimeError("Payload de historial inválido: encabezado incompleto")
        total, returned_offset, returned = struct.unpack_from("<III", payload, 0)
        if returned_offset != offset or total > 16 or returned > page_size:
            raise RuntimeError("Payload de historial inválido: paginación incoherente")
        if len(payload) != 12 + returned * 41:
            raise RuntimeError("Payload de historial inválido: tamaño de registros incorrecto")
        if total_count is None:
            total_count = total
        elif total != total_count:
            raise RuntimeError("El historial cambió mientras se consultaban sus páginas")

        for i in range(returned):
            pos = 12 + i * 41
            name_raw, linear, redirected, caller_cs, caller_ip, context = struct.unpack_from(
                "<16sIBHH16s", payload, pos
            )
            name = name_raw.split(b"\0", 1)[0].decode("ascii", errors="replace")
            entries.append({
                "index": offset + i,
                "filename": name,
                "linear_address": linear,
                "linear_address_hex": f"0x{linear:08X}",
                "redirected": bool(redirected),
                "caller_cs": caller_cs,
                "caller_ip": caller_ip,
                "caller_cs_ip": f"{caller_cs:04X}:{caller_ip:04X}",
                "caller_context_hex": context.hex(" ").upper(),
            })
        if offset + returned >= total:
            break

    return json.dumps({
        "count": total_count or 0,
        "order": "newest_first",
        "entries": entries,
        "context_note": "caller_context_hex contiene los 16 bytes de código anteriores a la dirección de retorno de INT 21h.",
    }, ensure_ascii=False)


@mcp.tool()
def jump_trace() -> str:
    """Devuelve hasta 12 cambios de ESI registrados tras aplicar un salto a diálogo."""
    response=json.loads(_send_command(CMD_JUMP_TRACE));payload=bytes.fromhex(response.get("payload_hex",""))
    if len(payload)!=response.get("payload_length") or len(payload)<4:raise RuntimeError("Payload de jump_trace inválido: encabezado incompleto")
    count=struct.unpack_from("<I",payload,0)[0]
    if count>12 or len(payload)!=4+count*14:raise RuntimeError("Payload de jump_trace inválido: cantidad o tamaño incorrecto")
    entries=[]
    for index in range(count):
        cs,eip,old_esi,new_esi=struct.unpack_from("<HIII",payload,4+index*14)
        entries.append({"index":index,"cs":cs,"eip":eip,"cs_eip":f"{cs:04X}:{eip & 0xFFFF:04X}","eip_hex":f"0x{eip:08X}","old_esi":old_esi,"old_esi_hex":f"0x{old_esi:08X}","new_esi":new_esi,"new_esi_hex":f"0x{new_esi:08X}"})
    return json.dumps({"count":count,"order":"oldest_first","entries":entries,"note":"Traza nativa de cambios de ESI observados después de aplicar el salto a diálogo; no registra todos los saltos de CPU."},ensure_ascii=False)


@mcp.tool()
def emulator_status() -> str:
    """Consulta indicadores de estado del emulador sin modificar su ejecución."""
    raw = int(json.loads(_send_command(CMD_STATUS_QUERY))["result"])
    flags = raw & 0xFFFFFFFF
    return json.dumps({
        "stop_emulation": bool(flags & 0x01),
        "single_step": bool(flags & 0x02),
        "dialogue_jump_armed": bool(flags & 0x04),
        "scene_redirect_armed": bool(flags & 0x08),
        "fast_forward_waiting": bool(flags & 0x10),
        "fast_forward_busy": bool(flags & 0x20),
        "raw_flags": flags,
    }, ensure_ascii=False)


@mcp.tool()
def current_instruction() -> str:
    """Desensambla la instrucción actual con bytes, mnemónico, operandos y longitud."""
    response = json.loads(_send_command(CMD_CURRENT_INSTRUCTION))
    payload = bytes.fromhex(response.get("payload_hex", ""))
    if len(payload) != 96 or len(payload) != response.get("payload_length"):
        raise RuntimeError("Payload de instrucción inválido")
    cs, ip, eip, linear, instruction_length = struct.unpack_from("<HHIIB", payload, 0)
    if instruction_length < 1 or instruction_length > 15:
        raise RuntimeError("Longitud de instrucción inválida")
    raw_bytes = payload[13:13 + instruction_length]
    mnemonic = payload[28:52].split(bytes((0,)), 1)[0].decode("ascii", errors="replace")
    operands = payload[52:96].split(bytes((0,)), 1)[0].decode("ascii", errors="replace")
    return json.dumps({
        "cs": cs,
        "ip": ip,
        "cs_ip": f"{cs:04X}:{ip:04X}",
        "eip": eip,
        "eip_hex": f"0x{eip:08X}",
        "linear_address": linear,
        "linear_address_hex": f"0x{linear:08X}",
        "length": instruction_length,
        "bytes": list(raw_bytes),
        "bytes_hex": raw_bytes.hex(" ").upper(),
        "mnemonic": mnemonic,
        "operands": operands,
    }, ensure_ascii=False)


@mcp.tool()
def cpu_registers() -> str:
    """Consulta los registros generales, de segmento, EIP y EFLAGS de solo lectura."""
    response = json.loads(_send_command(CMD_CPU_REGISTERS))
    payload = bytes.fromhex(response.get("payload_hex", ""))
    if len(payload) != 53 or len(payload) != response.get("payload_length"):
        raise RuntimeError("Payload de registros de CPU inválido")
    values = struct.unpack("<8I6HIIB", payload)
    general_names = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")
    segment_names = ("es", "cs", "ss", "ds", "fs", "gs")
    general = {name: value for name, value in zip(general_names, values[:8])}
    segments = {name: value for name, value in zip(segment_names, values[8:14])}
    eip, eflags, protected_mode = values[14:]
    flag_bits = {
        "carry": 0, "parity": 2, "auxiliary": 4, "zero": 6,
        "sign": 7, "trap": 8, "interrupt": 9, "direction": 10,
        "overflow": 11,
    }
    return json.dumps({
        "general": general,
        "general_hex": {name: f"0x{value:08X}" for name, value in general.items()},
        "segments": segments,
        "segments_hex": {name: f"0x{value:04X}" for name, value in segments.items()},
        "eip": eip,
        "eip_hex": f"0x{eip:08X}",
        "eflags": eflags,
        "eflags_hex": f"0x{eflags:08X}",
        "flags": {name: bool(eflags & (1 << bit)) for name, bit in flag_bits.items()},
        "protected_mode": bool(protected_mode),
    }, ensure_ascii=False)


@mcp.tool()
def fast_forward_status() -> str:
    """Consulta si Fast Forward está activo o ha terminado."""
    raw = int(json.loads(_send_command(CMD_SKIP_STATUS_QUERY))["result"]) & 0xFFFFFFFF
    return json.dumps({
        "active": bool(raw & 0x01),
        "finished": bool(raw & 0x02),
        "raw_flags": raw,
    }, ensure_ascii=False)


@mcp.tool()
def memory_read(address: int, size: int = 1, space: str = "physical") -> str:
    """Lee 1, 2 o 4 bytes; space: physical o logical (con paginación)."""
    address = _u32(address, "address")
    if not isinstance(size, int) or isinstance(size, bool) or size not in (1, 2, 4):
        raise ValueError("size debe ser un entero: 1, 2 o 4 bytes")
    spaces = {"physical": 0, "logical": 1}
    if space not in spaces:
        raise ValueError("space debe ser 'physical' o 'logical'")
    if address > 0xFFFFFFFF - (size - 1):
        raise ValueError("La lectura excede el espacio de direcciones de 32 bits")
    response = json.loads(_send_command(CMD_MEMORY_READ, address, size, spaces[space]))
    raw = int.from_bytes(bytes.fromhex(response["payload_hex"]), "little", signed=False)
    value = raw & ((1 << (size * 8)) - 1)
    return json.dumps({"address": f"0x{address:08X}", "space": space,
                       "size": size, "value": value,
                       "hex": f"0x{value:0{size * 2}X}",
                       "byte_order": "little-endian"}, ensure_ascii=False)


@mcp.tool()
def memory_read_block(address: int, length: int = 256, space: str = "physical") -> str:
    """Lee entre 1 y 256 bytes contiguos de memoria física o lógica."""
    address = _u32(address, "address")
    if not isinstance(length, int) or isinstance(length, bool) or length < 1 or length > 256:
        raise ValueError("length debe ser un entero entre 1 y 256 bytes")
    spaces = {"physical": 0, "logical": 1}
    if space not in spaces:
        raise ValueError("space debe ser 'physical' o 'logical'")
    if address > 0xFFFFFFFF - (length - 1):
        raise ValueError("La lectura excede el espacio de direcciones de 32 bits")
    response = json.loads(_send_command(CMD_MEMORY_READ_BLOCK, address, length, spaces[space]))
    if response.get("result") != 1 or response.get("payload_length") != length:
        raise RuntimeError("El emulador no pudo completar la lectura de memoria")
    data_hex = response["payload_hex"]
    return json.dumps({"address": f"0x{address:08X}", "space": space,
                       "length": length, "hex": data_hex,
                       "bytes": [int(data_hex[i:i + 2], 16) for i in range(0, len(data_hex), 2)]},
                      ensure_ascii=False)


@mcp.tool()
def emulator_resume() -> str:
    """Reanuda la emulación."""
    return _send_command(CMD_RESUME)


@mcp.tool()
def emulator_pause() -> str:
    """Pausa la emulación."""
    return _send_command(CMD_PAUSE)


@mcp.tool()
def emulator_step() -> str:
    """Ejecuta una instrucción y vuelve a pausar."""
    return _send_command(CMD_STEP)


@mcp.tool()
def emulator_step_over() -> str:
    """Ejecuta Step Over sobre la instrucción actual."""
    return _send_command(CMD_STEP_OVER)


@mcp.tool()
def breakpoints_list(page_size: int = 48) -> str:
    """Lista todos los breakpoints activos sin modificarlos ni reanudar el emulador."""
    if not isinstance(page_size, int) or isinstance(page_size, bool) or not 1 <= page_size <= 48:
        raise ValueError("page_size debe estar entre 1 y 48")

    entries = []
    total_count = None
    offset = 0
    flag_names = ((0x01, "READ"), (0x02, "WRITE"),
                  (0x04, "EXECUTE"), (0x08, "ONESHOT"))

    while True:
        response = json.loads(_send_command(CMD_BREAKPOINTS_LIST, offset, page_size))
        payload = bytes.fromhex(response.get("payload_hex", ""))
        if len(payload) != response.get("payload_length") or len(payload) < 12:
            raise RuntimeError("Payload de breakpoints inválido: encabezado incompleto")
        total, returned_offset, returned = struct.unpack_from("<III", payload, 0)
        if (returned_offset != offset or returned > page_size or
                offset > total or returned > total - offset or
                len(payload) != 12 + returned * 5):
            raise RuntimeError("Payload de breakpoints inválido: paginación incoherente")
        if total_count is None:
            total_count = total
        elif total != total_count:
            raise RuntimeError("La lista de breakpoints cambió durante la consulta")

        for index in range(returned):
            address, flags = struct.unpack_from("<IB", payload, 12 + index * 5)
            names = [name for bit, name in flag_names if flags & bit]
            unknown_flags = flags & ~0x0F
            canonical = None
            if address <= 0xFFFFF:
                canonical = {"segment": address >> 4, "offset": address & 0x0F}
            elif address <= 0x10FFEF:
                canonical = {"segment": 0xFFFF, "offset": address - 0xFFFF0}
            entries.append({
                "index": offset + index,
                "linear_address": address,
                "linear_address_hex": f"0x{address:08X}",
                "canonical_segment_offset": canonical,
                "flags": flags,
                "flags_hex": f"0x{flags:02X}",
                "types": names,
                "unknown_flags": unknown_flags,
            })

        if offset + returned >= total:
            break
        if returned == 0:
            raise RuntimeError("La consulta de breakpoints no avanzó pese a quedar entradas")
        offset += returned

    return json.dumps({
        "count": total_count or 0,
        "entries": entries,
        "read_only": True,
        "note": "Se omiten entradas vacías. La estructura nativa conserva la dirección lineal, no el segmento:offset original; canonical_segment_offset es una representación equivalente cuando cabe en modo real.",
    }, ensure_ascii=False)


@mcp.tool()
def breakpoint_toggle(segment: int, offset: int, flags: int = 4) -> str:
    """Activa o desactiva un breakpoint en una dirección segmento:offset."""
    return _send_command(
        CMD_BREAKPOINT_TOGGLE,
        _u16(segment, "segment"),
        _u16(offset, "offset"),
        _u32(flags, "flags"),
    )


@mcp.tool()
def breakpoint_toggle_linear(address: int, flags: int = 4) -> str:
    """Activa o desactiva un breakpoint en una dirección lineal."""
    return _send_command(
        CMD_BREAKPOINT_TOGGLE_LINEAR,
        _u32(address, "address"),
        _u32(flags, "flags"),
    )


@mcp.tool()
def breakpoints_reset() -> str:
    """Elimina todos los breakpoints."""
    return _send_command(CMD_BREAKPOINTS_RESET)


@mcp.tool()
def dialogue_jump_arm(
    script_segment: int,
    target_esi: int,
    allowed_cs_ip: list[int] | None = None,
) -> str:
    """Prepara un salto controlado a un diálogo, con hasta 8 direcciones CS:IP permitidas."""
    return _send_command(
        CMD_JUMP_ARM,
        _u16(script_segment, "script_segment"),
        _u32(target_esi, "target_esi"),
        addresses=allowed_cs_ip or (),
    )


@mcp.tool()
def dialogue_operation_cancel() -> str:
    """Cancela el salto a diálogo preparado."""
    return _send_command(CMD_JUMP_CANCEL)


@mcp.tool()
def scene_redirect_arm(script_name: str) -> str:
    """Redirige la próxima apertura de script BIN elegible al nombre indicado."""
    if not script_name or "\0" in script_name:
        raise ValueError("script_name no puede estar vacío ni contener NUL")
    return _send_command(CMD_SCENE_ARM, script_name=script_name)


@mcp.tool()
def scene_redirect_cancel() -> str:
    """Cancela la redirección de escena preparada."""
    return _send_command(CMD_SCENE_CANCEL)


@mcp.tool()
def fast_forward_arm(
    script_segment: int,
    dialogue_count: int,
    wait_kind: int = 0,
) -> str:
    """Activa Fast Forward; wait_kind: 0 inmediato, 1 tras salto ESI, 2 tras redirección de escena."""
    if not isinstance(wait_kind, int) or isinstance(wait_kind, bool) or wait_kind not in (0, 1, 2):
        raise ValueError("wait_kind debe ser un entero: 0, 1 o 2")
    return _send_command(
        CMD_SKIP_ARM,
        _u16(script_segment, "script_segment"),
        _u32(dialogue_count, "dialogue_count"),
        wait_kind,
    )


@mcp.tool()
def fast_forward_cancel() -> str:
    """Cancela Fast Forward."""
    return _send_command(CMD_SKIP_CANCEL)


if __name__ == "__main__":
    mcp.run(transport="stdio")
