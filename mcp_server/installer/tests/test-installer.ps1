$ErrorActionPreference = "Stop"

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "FALLO: $Message" }
}

$tempRoot = Join-Path $env:RUNNER_TEMP ("np2debug-installer-test-" + [guid]::NewGuid().ToString("N"))
$oldLocal = $env:LOCALAPPDATA
$oldApp = $env:APPDATA
try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null
    $env:LOCALAPPDATA = Join-Path $tempRoot "LocalAppData"
    $env:APPDATA = Join-Path $tempRoot "AppData"
    New-Item -ItemType Directory -Path $env:LOCALAPPDATA, $env:APPDATA -Force | Out-Null

    $packageDir = Join-Path $tempRoot "package"
    New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
    Copy-Item "mcp_server/installer/install.ps1" $packageDir
    Copy-Item "mcp_server/installer/uninstall.ps1" $packageDir
    Set-Content -LiteralPath (Join-Path $packageDir "np2debug-mcp-server.exe") -Value "test executable" -Encoding ASCII

    $configDir = Join-Path $tempRoot "ClaudeProfile"
    New-Item -ItemType Directory -Path $configDir -Force | Out-Null
    $configPath = Join-Path $configDir "claude_desktop_config.json"
    $initial = @{
        mcpServers = @{
            ghidra = @{ command = "ghidra-mcp.exe"; args = @("--stdio") }
        }
        globalShortcut = "Ctrl+Shift+Space"
    }
    $initial | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $configPath -Encoding UTF8

    function global:Read-Host {
        param([string]$Prompt)
        return "S"
    }

    & (Join-Path $packageDir "install.ps1") -ConfigPath $configPath
    $installed = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    Assert-True ($null -ne $installed.mcpServers.ghidra) "La instalación eliminó la entrada Ghidra."
    Assert-True ($installed.mcpServers.np2debug.command -eq (Join-Path $env:LOCALAPPDATA "Programs\NP2DebugNextMCP\np2debug-mcp-server.exe")) "La entrada np2debug no apunta al ejecutable instalado."
    Assert-True ($installed.globalShortcut -eq "Ctrl+Shift+Space") "La instalación alteró otras preferencias JSON."
    Assert-True (@(Get-ChildItem -LiteralPath $configDir -Filter "*.backup_np2debug_*").Count -eq 1) "No se creó la copia de seguridad de instalación."

    & (Join-Path $env:LOCALAPPDATA "Programs\NP2DebugNextMCP\uninstall.ps1")
    $afterUninstall = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
    Assert-True ($null -eq $afterUninstall.mcpServers.PSObject.Properties["np2debug"]) "La desinstalación no eliminó la entrada np2debug."
    Assert-True ($null -ne $afterUninstall.mcpServers.ghidra) "La desinstalación eliminó la entrada Ghidra."
    Assert-True ($afterUninstall.globalShortcut -eq "Ctrl+Shift+Space") "La desinstalación alteró otras preferencias JSON."
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $env:LOCALAPPDATA "Programs\NP2DebugNextMCP"))) "La carpeta propia no se eliminó tras confirmar."
    Assert-True (@(Get-ChildItem -LiteralPath $configDir -Filter "*.backup_np2debug_uninstall_*").Count -eq 1) "No se creó la copia de seguridad de desinstalación."

    $invalid = "{ JSON roto"
    Set-Content -LiteralPath $configPath -Value $invalid -NoNewline -Encoding UTF8
    $before = Get-Content -LiteralPath $configPath -Raw
    $failedAsExpected = $false
    try {
        & (Join-Path $packageDir "install.ps1") -ConfigPath $configPath
    }
    catch {
        $failedAsExpected = $true
    }
    $after = Get-Content -LiteralPath $configPath -Raw
    Assert-True $failedAsExpected "El instalador aceptó un JSON inválido."
    Assert-True ($before -ceq $after) "El instalador modificó el archivo JSON inválido."

    Write-Host "OK: instalación, backup, preservación de Ghidra/preferencias, desinstalación y rechazo de JSON inválido."
}
finally {
    Remove-Item Function:\global:Read-Host -ErrorAction SilentlyContinue
    if ($oldLocal) { $env:LOCALAPPDATA = $oldLocal } else { Remove-Item Env:\LOCALAPPDATA -ErrorAction SilentlyContinue }
    if ($oldApp) { $env:APPDATA = $oldApp } else { Remove-Item Env:\APPDATA -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $tempRoot) { Remove-Item -LiteralPath $tempRoot -Recurse -Force }
}
