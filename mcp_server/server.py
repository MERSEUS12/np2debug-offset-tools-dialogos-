"""NP2Debug Next MCP stdio bridge for the Windows named-pipe transport."""

from __future__ import annotations

import json
import struct
from typing import Sequence

from mcp.server.fastmcp import FastMCP

PIPE_NAME = r"\\.\pipe\NP2DebugNext"
MAGIC = 0x4432504E
VERSION = 2

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

mcp = FastMCP("np2debug-next")


def _u32(value: int, field: str) -> int:
    number = int(value)
    if number < 0 or number > 0xFFFFFFFF:
        raise ValueError(f"{field} debe estar entre 0 y 4294967295")
    return number


def _u16(value: int, field: str) -> int:
    number = int(value)
    if number < 0 or number > 0xFFFF:
        raise ValueError(f"{field} debe estar entre 0 y 65535")
    return number


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
        raise ValueError("El emulador rechazó la orden: solicitud no válida")
    if status == 2:
        raise RuntimeError("La cola de comandos está llena; vuelve a intentarlo")
    if status == 3:
        raise RuntimeError("El emulador está cerrando el servidor IPC")
    if status != 0:
        raise RuntimeError(f"Estado IPC desconocido: {status}")
    if payload_length > 256:
        raise RuntimeError("Respuesta IPC inválida: longitud de payload superior a 256 bytes")
    if command_type in (CMD_MEMORY_READ, CMD_MEMORY_READ_BLOCK) and result < 0:
        raise RuntimeError("El emulador rechazó la lectura de memoria: dirección, tamaño o espacio inválido")
    if command_type == CMD_DIALOGUE_CURRENT and result not in (0, 1):
        raise RuntimeError("Respuesta IPC inválida para seguimiento de diálogo")
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
    """Consulta la dirección actual de CPU en formato segmento:offset (CS:IP)."""
    raw = int(json.loads(_send_command(CMD_CURRENT_CSIP))["result"]) & 0xFFFFFFFF
    return json.dumps({
        "cs": (raw >> 16) & 0xFFFF,
        "ip": raw & 0xFFFF,
        "cs_ip": f"{(raw >> 16) & 0xFFFF:04X}:{raw & 0xFFFF:04X}",
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
    if size not in (1, 2, 4):
        raise ValueError("size debe ser 1, 2 o 4 bytes")
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
    if not isinstance(length, int) or length < 1 or length > 256:
        raise ValueError("length debe estar entre 1 y 256 bytes")
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
def dialogue_jump_cancel() -> str:
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
    if wait_kind not in (0, 1, 2):
        raise ValueError("wait_kind debe ser 0, 1 o 2")
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
