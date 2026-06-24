$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$main = Get-Content -Raw (Join-Path $root "module/src/main/cpp/main.cpp")
$hack = Get-Content -Raw (Join-Path $root "module/src/main/cpp/hack.cpp")
$dump = Get-Content -Raw (Join-Path $root "module/src/main/cpp/il2cpp_dump.cpp")

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

Assert-Contains $main 'bool\s+enable_hack\s*=\s*false\s*;' 'enable_hack must default to false before postAppSpecialize.'
Assert-Contains $main 'void\s*\*\s*data\s*=\s*nullptr\s*;' 'NativeBridge mapped data must default to nullptr.'
Assert-Contains $main 'size_t\s+length\s*=\s*0\s*;' 'NativeBridge mapped length must default to 0.'
Assert-Contains $main 'mmap\([^;]+;\s*if\s*\(\s*data\s*==\s*MAP_FAILED\s*\)' 'mmap result must be checked before using mapped arm library data.'

Assert-Contains $hack 'bool\s+NativeBridgeLoad\([^)]*\)' 'NativeBridgeLoad must return a status.'
Assert-Contains $hack 'if\s*\(\s*!data\s*\|\|\s*length\s*==\s*0\s*\)' 'NativeBridgeLoad must reject empty mapped library data before memcpy.'
Assert-Contains $hack 'if\s*\(\s*!vm\s*\)' 'NativeBridgeLoad must reject a missing Zygisk JavaVM.'
if ($hack -match 'JNI_GetCreatedJavaVMs') {
    throw 'NativeBridgeLoad must use the Zygisk JavaVM instead of JNI_GetCreatedJavaVMs.'
}
Assert-Contains $hack 'if\s*\(\s*!init\s*\)' 'NativeBridge trampoline JNI_OnLoad must be checked before calling.'

Assert-Contains $dump 'bool\s+il2cpp_api_init\s*\(' 'il2cpp_api_init must report initialization failure to caller.'
Assert-Contains $dump 'return\s+false\s*;' 'il2cpp_api_init must return false on missing required APIs.'
Assert-Contains $hack 'if\s*\(\s*il2cpp_api_init\(handle\)\s*\)' 'hack_start must only dump after il2cpp API initialization succeeds.'

Write-Host "crash guard checks passed"
