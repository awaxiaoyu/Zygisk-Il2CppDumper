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
Assert-Contains $logHeader '/sdcard/' 'log.h must write logs under the shared storage root.'
Assert-Contains $logHeader 'EnsureDir\(' 'log.h must create the package log directory before opening the log.'
Assert-Contains $main 'LogInit\(package_name\);' 'target process must initialize public file logging before module logs.'

Write-Host "log file checks passed"
