# Instalador de NP2Debug Next MCP para Windows

Este paquete instala el servidor MCP autónomo para Claude Desktop. **No requiere instalar Python**.

## Instalación

1. Descarga y extrae el ZIP completo en una carpeta.
2. Abre PowerShell en esa carpeta.
3. Ejecuta:

   ```powershell
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1
   ```

4. Si se detecta más de una configuración de Claude Desktop, selecciona la que utilizas.
5. Cierra y vuelve a abrir Claude Desktop.

El instalador copia `np2debug-mcp-server.exe` a:

```text
%LOCALAPPDATA%\Programs\NP2DebugNextMCP
```

Después registra el servidor como `np2debug` con transporte stdio y sin argumentos. Busca las ubicaciones habituales de Claude Desktop y también perfiles bajo `%LOCALAPPDATA%\Claude-Data\claude_*\`. Si no encuentra una configuración, pregunta antes de crear una nueva. Para indicar una ruta manualmente:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install.ps1 -ConfigPath "C:\ruta\claude_desktop_config.json"
```

## Seguridad y conservación de configuración

- Crea una copia de seguridad de la configuración JSON antes de modificarla.
- Comprueba que el JSON existente sea válido; si está dañado, se detiene sin sobrescribirlo.
- Solo crea o actualiza la entrada `mcpServers.np2debug`.
- Conserva las demás entradas MCP, incluidas las de Ghidra.
- Si ya existe una entrada `np2debug`, solicita confirmación antes de reemplazarla.
- No cambia el registro de Windows, no instala servicios y no abre puertos TCP.

## Desinstalación

Ejecuta el desinstalador instalado:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:LOCALAPPDATA\Programs\NP2DebugNextMCP\uninstall.ps1"
```

El desinstalador elimina únicamente la entrada `np2debug` si todavía apunta al ejecutable de esta instalación. Crea una copia de seguridad antes de editar el JSON y solicita confirmación antes de borrar los archivos del programa. Las demás entradas MCP y los archivos de usuario se conservan.

## Requisitos de funcionamiento

- Windows 10/11 x64.
- NP2Debug Next x64 abierto con el servidor IPC integrado.
- Claude Desktop reiniciado después de instalar.

El instalador no instala el emulador ni modifica BIOS, discos, imágenes de juego o archivos de configuración del emulador.
