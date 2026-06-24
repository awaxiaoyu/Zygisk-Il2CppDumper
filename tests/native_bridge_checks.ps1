$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$hack = Get-Content -Raw (Join-Path $root "module/src/main/cpp/hack.cpp")

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

Assert-Contains $hack 'OpenLibraryCandidates\(' 'hack.cpp must use a multi-path dlopen helper.'
Assert-Contains $hack '/apex/com\.android\.art/lib64/libart\.so' 'libart fallback must include the ART APEX lib64 path.'
Assert-Contains $hack '/apex/com\.android\.art/lib/libart\.so' 'libart fallback must include the ART APEX lib path.'
Assert-Contains $hack '/system/lib64/libhoudini\.so' 'NativeBridge fallback must include system lib64 houdini.'
Assert-Contains $hack '/system/lib/libhoudini\.so' 'NativeBridge fallback must include system lib houdini.'
Assert-Contains $hack 'constexpr\s+int\s+kIl2CppWaitSeconds\s*=\s*120\s*;' 'libil2cpp wait window must allow late Unity loads.'
Assert-Contains $hack 'xdl_open\("libil2cpp\.so",\s*XDL_TRY_FORCE_LOAD\)' 'libil2cpp open must try linker force-load fallback.'

Write-Host "native bridge checks passed"
