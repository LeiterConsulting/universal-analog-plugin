[CmdletBinding()]
param(
    [switch]$Hardware,
    [string[]]$PluginDll,
    [ValidateRange(0, 60)]
    [int]$CaptureSeconds = 3,
    [switch]$RequireAnalogInput
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$originalLocation = Get-Location

function Find-VsDevCmd {
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $programFiles = [Environment]::GetFolderPath('ProgramFiles')
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'

    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installation) {
            $candidate = Join-Path $installation.Trim() 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path -LiteralPath $candidate) {
                return $candidate
            }
        }
    }

    $fallbacks = @(
        (Join-Path $programFilesX86 'Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'),
        (Join-Path $programFiles 'Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'),
        (Join-Path $programFiles 'Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat'),
        (Join-Path $programFiles 'Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat')
    )
    foreach ($candidate in $fallbacks) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw 'Visual Studio C++ Build Tools were not found. Install the MSVC x64 toolchain before running tests.'
}

try {
    Set-Location $repoRoot
    New-Item -ItemType Directory -Path 'build\tests' -Force | Out-Null

    $vsDevCmd = Find-VsDevCmd
    Write-Host "Using Visual Studio environment: $vsDevCmd"

    $commands = @(
        'cl /nologo /std:c++20 /EHsc /W4 /WX /I Soup tests\cppro_protocol_test.cpp /Fe:build\tests\cppro_protocol_test.exe /Fo:build\tests\cppro_protocol_test.obj',
        'build\tests\cppro_protocol_test.exe',
        'cl /nologo /std:c++20 /EHsc /W4 /I Soup /c Soup\soup\AnalogueKeyboard.cpp /Fo:build\tests\AnalogueKeyboard.obj',
        'cl /nologo /std:c++20 /EHsc /W4 /I Soup /DABI_VERSION_TARGET=0 /c main.cpp /Fo:build\tests\main-abiv0.obj',
        'cl /nologo /std:c++20 /EHsc /W4 /I Soup /DABI_VERSION_TARGET=1 /c main.cpp /Fo:build\tests\main-abiv1.obj',
        'cl /nologo /std:c++20 /EHsc /W4 tools\cppro_hid_probe.cpp /Fe:build\tests\cppro_hid_probe.exe /Fo:build\tests\cppro_hid_probe.obj hid.lib cfgmgr32.lib',
        'cl /nologo /std:c++20 /EHsc /W4 /WX tests\release_dll_smoke.cpp /Fe:build\tests\release_dll_smoke.exe /Fo:build\tests\release_dll_smoke.obj'
    ) -join ' && '

    $cmdLine = '"' + $vsDevCmd + '" -arch=x64 -host_arch=x64 >nul && ' + $commands
    & cmd.exe /d /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        throw "MSVC test compilation failed with exit code $LASTEXITCODE."
    }

    if ($Hardware) {
        $output = & '.\build\tests\cppro_hid_probe.exe' 2 | Out-String
        Write-Host $output
        foreach ($expected in @(
            'Centerpiece-Pro-MCU',
            'Usage: 0xff00:0x1',
            'Report lengths (input/output/feature): 64/64/0',
            '04 02 f1 1d',
            '04 08 03'
        )) {
            if (-not $output.Contains($expected)) {
                throw "Live CPPRO validation did not contain: $expected"
            }
        }
        Write-Host 'CPPRO live hardware assertions passed'
    }

    foreach ($dll in $PluginDll) {
        $resolvedDll = (Resolve-Path -LiteralPath $dll).Path
        $arguments = @($resolvedDll, $CaptureSeconds)
        if ($RequireAnalogInput) {
            $arguments += '--require-input'
        }
        & '.\build\tests\release_dll_smoke.exe' @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Release DLL smoke test failed for $resolvedDll with exit code $LASTEXITCODE."
        }
    }

    Write-Host 'All CPPRO tests passed'
}
finally {
    Set-Location $originalLocation
}
