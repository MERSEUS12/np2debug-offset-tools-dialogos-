$ErrorActionPreference = "Stop"

$installRoot = Join-Path $env:LOCALAPPDATA "Programs\NP2DebugNextMCP"
$manifestPath = Join-Path $installRoot "install-manifest.json"
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "No se encontro install-manifest.json. No se puede verificar que archivos y configuracion pertenecen a esta instalacion."
}

$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$expectedRoot = [System.IO.Path]::GetFullPath($installRoot).TrimEnd('\')
$recordedRoot = [System.IO.Path]::GetFullPath([string]$manifest.installPath).TrimEnd('\')
if (-not [string]::Equals($expectedRoot, $recordedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "El manifiesto apunta a otra carpeta. Se cancela para evitar borrar archivos ajenos."
}

$configFile = [string]$manifest.configPath
$targetExe = [System.IO.Path]::GetFullPath([string]$manifest.executable)
if (Test-Path -LiteralPath $configFile -PathType Leaf) {
    $rawConfig = Get-Content -LiteralPath $configFile -Raw
    try {
        $config = ConvertFrom-Json -InputObject $rawConfig -ErrorAction Stop
    }
    catch {
        throw "La configuracion MCP ya no contiene JSON valido. No se borro ningun archivo: $($_.Exception.Message)"
    }

    if ($config.mcpServers -and $config.mcpServers.PSObject.Properties["np2debug"]) {
        $registeredCommand = [string]$config.mcpServers.np2debug.command
        if ([string]::Equals(
            [System.IO.Path]::GetFullPath($registeredCommand),
            $targetExe,
            [System.StringComparison]::OrdinalIgnoreCase
        )) {
            $answer = Read-Host "Se encontro la entrada np2debug que apunta a esta instalacion. Eliminar solo esa entrada? (S/N)"
            if ($answer -notmatch '^(S|SI|Y|YES)$') {
                throw "Desinstalacion cancelada."
            }
            $config.mcpServers.PSObject.Properties.Remove("np2debug")
            $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
            Copy-Item -LiteralPath $configFile -Destination "$configFile.backup_np2debug_uninstall_$stamp"
            $json = $config | ConvertTo-Json -Depth 100
            $tempConfig = "$configFile.np2debug.tmp"
            $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
            [System.IO.File]::WriteAllText($tempConfig, $json + [Environment]::NewLine, $utf8NoBom)
            Move-Item -LiteralPath $tempConfig -Destination $configFile -Force
        }
        else {
            Write-Host "La entrada np2debug apunta a otro ejecutable; se conserva sin cambios."
        }
    }
}

$answer = Read-Host "Eliminar los archivos instalados en '$installRoot'? (S/N)"
if ($answer -notmatch '^(S|SI|Y|YES)$') {
    Write-Host "Se conservaron los archivos del programa."
    exit 0
}
Remove-Item -LiteralPath $installRoot -Recurse -Force
Write-Host "NP2Debug Next MCP se desinstalo. Las demas entradas MCP y los archivos de usuario se conservaron."
