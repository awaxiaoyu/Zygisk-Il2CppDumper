$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$logHeader = Get-Content -Raw (Join-Path $root "module/src/main/cpp/log.h")
$main = Get-Content -Raw (Join-Path $root "module/src/main/cpp/main.cpp")

function Assert-Contains {
    param(
        [string] $Text,
        [string] $Pattern,
        [string] $Message
    )

    if ($Text -notmatch $Pattern) {
        throw $Message
    }
}

Assert-Contains $logHeader 'inline\s+bool\s+LogInit\(' 'log.h must expose file log initialization.'
Assert-Contains $logHeader 'inline\s+void\s+LogWrite\(' 'log.h must expose a shared log writer.'
Assert-Contains $logHeader 'zygisk_il2cppdumper\.log' 'log file name must be stable and predictable.'
Assert-Contains $logHeader '/files/zygisk_il2cppdumper\.log' 'log.h must write logs under the app private files directory.'
Assert-Contains $logHeader 'Unable to open file log: %s' 'file log open failures must include the private log path.'
Assert-Contains $main 'LogInit\(app_data_dir\);' 'target process must initialize private file logging before module logs.'

Write-Host "log file checks passed"
