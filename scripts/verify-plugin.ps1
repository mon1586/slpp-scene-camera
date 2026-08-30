param(
    [Parameter(Mandatory)]
    [string] $PluginDirectory
)

$ErrorActionPreference = 'Stop'
$dll = Join-Path $PluginDirectory 'SexlabSceneCamera.dll'
$pdb = Join-Path $PluginDirectory 'SexlabSceneCamera.pdb'

foreach ($path in @($dll, $pdb)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required build artifact is missing: $path"
    }
}

$dumpbinCandidates = @()
if ($env:VCToolsInstallDir) {
    $dumpbinCandidates += Join-Path $env:VCToolsInstallDir 'bin\Hostx64\x64\dumpbin.exe'
}
$visualStudioRoots = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC'
)
foreach ($root in $visualStudioRoots) {
    if (Test-Path -LiteralPath $root -PathType Container) {
        $dumpbinCandidates += Get-ChildItem -LiteralPath $root -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x64\dumpbin.exe' }
    }
}
$dumpbin = $dumpbinCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $dumpbin) {
    throw 'dumpbin.exe was not found in the active Visual Studio toolchain.'
}

$headers = & $dumpbin /headers $dll
if ($LASTEXITCODE -ne 0) { throw 'dumpbin /headers failed.' }
$exports = & $dumpbin /exports $dll
if ($LASTEXITCODE -ne 0) { throw 'dumpbin /exports failed.' }
$dependents = & $dumpbin /dependents $dll
if ($LASTEXITCODE -ne 0) { throw 'dumpbin /dependents failed.' }

if (-not ($headers | Select-String -SimpleMatch 'machine (x64)')) {
    throw 'The DLL is not x64.'
}
if (-not ($exports | Select-String -Pattern 'SKSEPlugin_(Load|Version)')) {
    throw 'The DLL does not export an SKSE plugin entry point.'
}
if ($dependents | Select-String -Pattern '(?i)\b(fmt|spdlog)\.dll\b') {
    throw 'The DLL has an unexpected dynamic fmt.dll or spdlog.dll dependency.'
}

$hash = Get-FileHash -LiteralPath $dll -Algorithm SHA256
Get-Item -LiteralPath $dll, $pdb | Select-Object FullName, Length, LastWriteTime
Write-Output "Verified x64 SKSE plugin: $($hash.Hash.ToLowerInvariant())"
