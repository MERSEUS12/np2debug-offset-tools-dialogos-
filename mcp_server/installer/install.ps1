param(
    [string]$ConfigPath
)

$ErrorActionPreference = "Stop"

function Get-ClaudeConfigCandidates {
    $items = @()
    if ($env:APPDATA) {
        $items += Join-Path $env:APPDATA "Claude\claude_desktop_config.json"
    }
    if ($env:LOCALAPPDATA) {
        $items += Join-Path $env:LOCALAPPDATA "Claude\claude_desktop_config.json"
        $dataRoot = Join-Path $env:LOCALAPPDATA "Claude-Data"
        if (Test-Path -LiteralPath $dataRoot -PathType Container) {
            Get-ChildItem -LiteralPath $dataRoot -Directory -Filter "claude_*" |
                ForEach-Object {
                    $items += Join-Path $_.FullName "claude_desktop_config.json"
                }
        }
    }
    return @($items | Select-Object -Unique | Where-Object {
        Test-Path -LiteralPath $_ -PathType Leaf
    })
}

function Select-ClaudeConfig {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return [System.IO.Path]::GetFullPath($RequestedPath.Trim('"'))
    }

    $candidates = @(Get-ClaudeConfigCandidates)
    if ($candidates.Count -eq 1) {
        return $candidates[0]
    }
    if ($candidates.Count -gt 1) {
        Write-Host "Se encontraron varias configuraciones de Claude Desktop:"
        for ($i = 0; $i -lt $candidates.Count; $i++) {
            Write-Host ("[{0}] {1}" -f ($i + 1), $candidates[$i])
        }
        $selection = Read-Host "Selecciona el numero de la configuracion que deseas modificar"
        $number = 0
        if (-not [int]::TryParse($selection, [ref]$number) -or
            $number -lt 1 -or $number -gt $candidates.Count) {
            throw "Seleccion invalida. No se modifico ninguna configuracion."
        }
        return $candidates[$number - 1]
    }

    $defaultPath = Join-Path $env:APPDATA "Claude\claude_desktop_config.json"
    $answer = Read-Host "No se encontro la configuracion. Crear una nueva en '$defaultPath'? (S/N)"
    if ($answer -notmatch '^(S|SI|Y|YES)$') {
        throw "Instalacion cancelada: no se selecciono una configuracion MCP."
    }
    return $defaultPath
}

$sourceExe = Join-Path $PSScriptRoot "np2debug-mcp-server.exe"
if (-not (Test-Path -LiteralPath $sourceExe -PathType Leaf)) {
    throw "No se encontro np2debug-mcp-server.exe junto a install.ps1. Extrae el paquete completo antes de ejecutar el instalador."
}

$installRoot = Join-Path $env:LOCALAPPDATA "Programs\NP2DebugNextMCP"
$targetExe = Join-Path $installRoot "np2debug-mcp-server.exe"
$configFile = Select-ClaudeConfig -RequestedPath $ConfigPath

$configDirectory = Split-Path -Parent $configFile
if (-not (Test-Path -LiteralPath $configDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $configDirectory -Force | Out-Null
}

$config = [PSCustomObject]@{}
if (Test-Path -LiteralPath $configFile -PathType Leaf) {
    $rawConfig = Get-Content -LiteralPath $configFile -Raw
    if (-not [string]::IsNullOrWhiteSpace($rawConfig)) {
        try {
            $config = ConvertFrom-Json -InputObject $rawConfig -ErrorAction Stop
        }
        catch {
            throw "El JSON de Claude no es valido. No se modifico el archivo. Detalle: $($_.Exception.Message)"
        }
    }
}
if ($null -eq $config -or $config -is [array] -or $config -is [string]) {
    throw "La raiz de la configuracion de Claude debe ser un objeto JSON."
}

if (-not $config.PSObject.Properties["mcpServers"]) {
    $config | Add-Member -MemberType NoteProperty -Name "mcpServers" -Value ([PSCustomObject]@{})
}
if ($null -eq $config.mcpServers) {
    $config.mcpServers = [PSCustomObject]@{}
}
$servers = $config.mcpServers
if ($servers -is [array] -or $servers -is [string]) {
    throw "La propiedad mcpServers no contiene un objeto JSON valido."
}

if ($servers.PSObject.Properties["np2debug"]) {
    $existing = $servers.np2debug
    Write-Host "Ya existe una entrada MCP llamada 'np2debug':"
    Write-Host ($existing | ConvertTo-Json -Depth 10)
    $answer = Read-Host "Deseas reemplazar solamente esa entrada? (S/N)"
    if ($answer -notmatch '^(S|SI|Y|YES)$') {
        throw "Instalacion cancelada. No se reemplazo la entrada existente."
    }
}

New-Item -ItemType Directory -Path $installRoot -Force | Out-Null
$tempExe = Join-Path $installRoot "np2debug-mcp-server.exe.new"
Copy-Item -LiteralPath $sourceExe -Destination $tempExe -Force
Move-Item -LiteralPath $tempExe -Destination $targetExe -Force

$entry = [PSCustomObject]@{
    command = $targetExe
    args = @()
}
if ($servers.PSObject.Properties["np2debug"]) {
    $servers.np2debug = $entry
}
else {
    $servers | Add-Member -MemberType NoteProperty -Name "np2debug" -Value $entry
}

$backupPath = $null
if (Test-Path -LiteralPath $configFile -PathType Leaf) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $backupPath = "$configFile.backup_np2debug_$stamp"
    Copy-Item -LiteralPath $configFile -Destination $backupPath
}

$json = $config | ConvertTo-Json -Depth 100
$tempConfig = "$configFile.np2debug.tmp"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($tempConfig, $json + [Environment]::NewLine, $utf8NoBom)
Move-Item -LiteralPath $tempConfig -Destination $configFile -Force

$uninstallerSource = Join-Path $PSScriptRoot "uninstall.ps1"
if (Test-Path -LiteralPath $uninstallerSource -PathType Leaf) {
    Copy-Item -LiteralPath $uninstallerSource -Destination (Join-Path $installRoot "uninstall.ps1") -Force
}
$manifest = [PSCustomObject]@{
    installPath = $installRoot
    executable = $targetExe
    configPath = $configFile
}
$manifest | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $installRoot "install-manifest.json") -Encoding UTF8

Write-Host ""
Write-Host "NP2Debug Next MCP se instalo correctamente."
Write-Host "Ejecutable: $targetExe"
Write-Host "Configuracion: $configFile"
if ($backupPath) {
    Write-Host "Copia de seguridad: $backupPath"
}
Write-Host "Cierra y vuelve a abrir Claude Desktop para cargar el servidor MCP."
