$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$hack = Get-Content -Raw (Join-Path $root "module/src/main/cpp/hack.cpp")
$dump = Get-Content -Raw (Join-Path $root "module/src/main/cpp/il2cpp_dump.cpp")
$dumpHeader = Get-Content -Raw (Join-Path $root "module/src/main/cpp/il2cpp_dump.h")

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

Assert-Contains $dump 'resolve_il2cpp_api\(' 'il2cpp_dump.cpp must centralize Il2CPP API symbol resolution.'
Assert-Contains $dump 'xdl_sym\(handle,\s*name,\s*nullptr\)' 'Il2CPP API resolver must try exported dynsym first.'
Assert-Contains $dump 'xdl_dsym\(handle,\s*name,\s*nullptr\)' 'Il2CPP API resolver must fall back to symtab/.gnu_debugdata.'
Assert-Contains $dumpHeader 'il2cpp_dump_diagnostics\(void\s*\*handle,\s*const\s+char\s*\*outDir,\s*const\s+char\s*\*reason\)' 'Diagnostics function must be exposed to hack_start.'
Assert-Contains $hack 'il2cpp_dump_diagnostics\(handle,\s*game_data_dir,\s*"il2cpp api init failed"\)' 'hack_start must write diagnostics when API init fails.'
Assert-Contains $dump '/files"\)' 'Diagnostics must stay under the app private files directory.'
Assert-Contains $dump '"/il2cpp_diag"' 'Diagnostics must use a stable il2cpp_diag directory.'
Assert-Contains $dump 'maps\.txt' 'Diagnostics must write full process maps.'
Assert-Contains $dump 'il2cpp_maps\.txt' 'Diagnostics must write libil2cpp-focused maps.'
Assert-Contains $dump 'phdr\.txt' 'Diagnostics must write program header info.'
Assert-Contains $dump 'symbol_probe\.txt' 'Diagnostics must write symbol probe results.'
Assert-Contains $dump 'global-metadata-\s*"\s*\+\s*std::to_string' 'Diagnostics must copy mapped metadata candidates when available.'
Assert-Contains $dump 'Game update note: if a new game moves metadata' 'New diagnostic feature must document how to update it for game version changes.'

Write-Host "il2cpp diagnostics checks passed"
