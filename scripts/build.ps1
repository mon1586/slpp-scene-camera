param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo',
    [string] $BuildDirectory,
    [switch] $SkipTests,
    [switch] $TestsOnly
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repoRoot 'build'
}
$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)

function Invoke-Native {
    param(
        [Parameter(Mandatory)] [string] $Command,
        [Parameter(Mandatory)] [string[]] $Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

$cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
    throw "ctest.exe was not found beside cmake.exe: $ctest"
}

$toolchainCandidates = @(
    $(if ($env:VCPKG_ROOT) { Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake' }),
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake'
) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
if (-not $toolchainCandidates) {
    throw 'vcpkg.cmake was not found. Set VCPKG_ROOT or install the Visual Studio vcpkg component.'
}
$toolchain = $toolchainCandidates[0]

$triplet = 'x64-windows-static-md'
$installedTriplet = Join-Path $BuildDirectory "vcpkg_installed\$triplet"
$manifestInstall = if (Test-Path -LiteralPath $installedTriplet -PathType Container) { 'OFF' } else { 'ON' }
$previousCommonLibPrebuilt = $env:COMMONLIB_PREBUILT
$env:COMMONLIB_PREBUILT = '1'

try {
    Invoke-Native $cmake @(
        '-S', $repoRoot,
        '-B', $BuildDirectory,
        '-G', 'Visual Studio 17 2022',
        '-A', 'x64',
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        "-DVCPKG_TARGET_TRIPLET=$triplet",
        "-DVCPKG_MANIFEST_INSTALL=$manifestInstall",
        '-DSSC_BUILD_TESTS=ON'
    )

    $targets = if ($TestsOnly) {
        @('SexlabSceneCameraControllerTests')
    } else {
        @('SexlabSceneCamera', 'SexlabSceneCameraControllerTests')
    }
    $buildArguments = @(
        '--build', $BuildDirectory,
        '--config', $Configuration,
        '--target'
    ) + $targets
    Invoke-Native -Command $cmake -Arguments $buildArguments

    if (-not $SkipTests) {
        Invoke-Native $ctest @(
            '--test-dir', $BuildDirectory,
            '-C', $Configuration,
            '--output-on-failure'
        )
    }

    if (-not $TestsOnly) {
        $sourceDirectory = Join-Path $BuildDirectory $Configuration
        $distDirectory = Join-Path $repoRoot 'dist\SKSE\Plugins'
        New-Item -ItemType Directory -Path $distDirectory -Force | Out-Null

        foreach ($name in @('SexlabSceneCamera.dll', 'SexlabSceneCamera.pdb')) {
            $source = Join-Path $sourceDirectory $name
            if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
                throw "Build output is missing: $source"
            }
            Copy-Item -LiteralPath $source -Destination (Join-Path $distDirectory $name) -Force
        }

        & (Join-Path $PSScriptRoot 'verify-plugin.ps1') -PluginDirectory $distDirectory
    }
} finally {
    $env:COMMONLIB_PREBUILT = $previousCommonLibPrebuilt
}
