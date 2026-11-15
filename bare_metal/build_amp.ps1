<#
    build_amp.ps1 - incremental build of the two ZYNQ7010 AMP applications
                    (Vitis 2024.1 workspace, PS UART1 115200 demo)

    Usage (from the workspace root):
        powershell -ExecutionPolicy Bypass -File .\build_amp.ps1
        powershell -ExecutionPolicy Bypass -File .\build_amp.ps1 -VitisRoot 'D:\xilinx2024.1\VITIS_SDK\Vitis\2024.1'

    Prerequisite:
        The platform has been built in the Vitis IDE at least once, and both
        application projects have a build directory (build.ninja).

    Note:
        The post-link step calls arm-none-eabi-size, so the toolchain bin
        directory must be on PATH. This script adds it explicitly; otherwise
        the link succeeds but the build reports
        "'arm-none-eabi-size' is not recognized".
#>
[CmdletBinding()]
param(
    [string]$VitisRoot = $env:XILINX_VITIS
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

if (-not $VitisRoot) {
    $VitisRoot = 'D:\xilinx2024.1\VITIS_SDK\Vitis\2024.1'
}

$gccBin = Join-Path $VitisRoot 'gnu\aarch32\nt\gcc-arm-none-eabi\bin'
$ninja  = Join-Path $VitisRoot 'tps\win64\lopper-1.1.0-packages\min_sdk\usr\bin\ninja.exe'

foreach ($tool in @($gccBin, $ninja)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Not found: $tool  (use -VitisRoot to point at Vitis 2024.1)"
    }
}

$env:PATH = "$gccBin;$env:PATH"

$failed = @()
foreach ($proj in @('hello_world', 'hello_world_1')) {
    $buildDir = Join-Path $root (Join-Path $proj 'build')
    if (-not (Test-Path -LiteralPath $buildDir)) {
        throw "Build dir not found: $buildDir  (build this project once in the Vitis IDE)"
    }

    Write-Host "==== build $proj ===="
    & $ninja -C $buildDir
    if ($LASTEXITCODE -ne 0) {
        $failed += $proj
    }
}

if ($failed.Count -gt 0) {
    throw ("Build failed: " + ($failed -join ', '))
}

Write-Host ''
Write-Host 'Build OK:'
Write-Host ('  ' + (Join-Path $root 'hello_world\build\hello_world.elf'))
Write-Host ('  ' + (Join-Path $root 'hello_world_1\build\hello_world_1.elf'))
