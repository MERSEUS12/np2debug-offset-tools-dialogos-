"""Smoke-test the frozen NP2Debug Next MCP executable over stdio."""

from __future__ import annotations

import asyncio
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

EXPECTED_TOOLS = {
    "emulator_resume", "emulator_pause", "emulator_step", "emulator_step_over",
    "breakpoint_toggle", "breakpoint_toggle_linear", "breakpoints_reset",
    "dialogue_jump_arm", "dialogue_jump_cancel", "scene_redirect_arm",
    "scene_redirect_cancel", "fast_forward_arm", "fast_forward_cancel",
    "emulator_status", "current_instruction", "fast_forward_status",
    "memory_read", "memory_read_block", "dialogue_current", "scene_history", "jump_trace",
    "cpu_registers", "breakpoints_list",
    "dialogue_catalog_refresh", "dialogue_catalog_entry", "dialogue_catalog",
    "dialogue_jump_request", "dialogue_jump_status",
}


async def main(executable: Path) -> None:
    if not executable.is_file():
        raise FileNotFoundError(f"No existe el ejecutable MCP: {executable}")

    parameters = StdioServerParameters(command=str(executable.resolve()), args=[], env=None)
    async with stdio_client(parameters) as (read_stream, write_stream):
        async with ClientSession(read_stream, write_stream) as session:
            initialized = await session.initialize()
            if initialized.serverInfo.name != "np2debug-next":
                raise AssertionError(f"Nombre MCP inesperado: {initialized.serverInfo.name!r}")

            response = await session.list_tools()
            actual_tools = {tool.name for tool in response.tools}
            missing = EXPECTED_TOOLS - actual_tools
            unexpected = actual_tools - EXPECTED_TOOLS
            if missing or unexpected:
                raise AssertionError(
                    f"Inventario MCP incorrecto. Faltan: {sorted(missing)}; "
                    f"sobran: {sorted(unexpected)}"
                )
            print(f"OK: handshake MCP y {len(actual_tools)} herramientas verificadas.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Uso: python test_frozen_server.py RUTA_AL_EXE")
    asyncio.run(main(Path(sys.argv[1])))
