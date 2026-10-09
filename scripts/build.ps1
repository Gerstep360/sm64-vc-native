param([string]$Deps="$env:USERPROFILE\.universal-modder\sm64-vc\deps")
$ErrorActionPreference='Stop'
$Project = Split-Path $PSScriptRoot -Parent
$Sdk = Join-Path $Deps 'plugin-sdk'
$Sm = Join-Path $Deps 'libsm64'
$Mh = Join-Path $Deps 'minhook'
$GccBin = Join-Path $Deps 'w64devkit\bin'
function RunChecked([string]$Exe,[string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with $LASTEXITCODE" }
}
if (!(Test-Path "$Sdk\shared\Events.h") -or !(Test-Path "$Sm\src\libsm64.h")) {
    throw 'Clone pinned dependencies first; see README.md'
}
if ((& git -C $Sdk rev-parse HEAD) -ne '15f15b60bbf74c106e1b496ff92c98764abf4605') {throw 'Plugin-SDK pin mismatch'}
if ((& git -C $Sm rev-parse HEAD) -ne 'fd11813208272b4271d92bd92feb8f3fdbe61be5') {throw 'libsm64 pin mismatch'}
if ((& git -C $Mh rev-parse HEAD) -ne 'c3fcafdc10146beb5919319d0683e44e3c30d537') {throw 'MinHook pin mismatch'}
New-Item -ItemType Directory -Force "$Project\logs","$Project\package" | Out-Null
$env:PATH="$GccBin;"+$env:PATH
Push-Location $Sm
try {
    if (!(Test-Path 'src\decomp\mario\geo.inc.c')) {RunChecked 'python' @('import-mario-geo.py')}
    # Keep static surface addresses stable across resident-world refreshes. Upstream otherwise frees
    # memory still referenced by Mario's previous floor. Fixed capacity, enforced by the host bridge.
    RunChecked 'python' @("$PSScriptRoot\patch_sm64.py",$Sm)
    RunChecked 'python' @("$PSScriptRoot\patch_rom_geometry.py",$Sm)
    RunChecked 'python' @("$PSScriptRoot\patch_bridge.py",$Sm)
    $ErrorActionPreference='Continue' # Windows PowerShell represents compiler warnings as ErrorRecords.
    & make lib -j8 -W src/libsm64.c 'CC=gcc' 'CFLAGS=-fno-strict-aliasing -O2 -Wall -Wno-unused-function -fvisibility=hidden -DSM64_LIB_EXPORT -DGBI_FLOATS -DVERSION_US -DNO_SEGMENTED_MEMORY' 'LDFLAGS=-lm -shared -static-libgcc -Wl,--no-insert-timestamp' *> "$Project\logs\libsm64-build.log"
    $ErrorActionPreference='Stop'
    if($LASTEXITCODE -ne 0){throw 'libsm64 build failed; see logs'}
} finally {Pop-Location}
RunChecked 'cmake' @('-S',$Project,'-B',"$Project\build",'-G','Visual Studio 17 2022','-A','Win32',"-DPLUGIN_SDK=$Sdk","-DLIBSM64=$Sm","-DMINHOOK=$Mh")
& cmake --build "$Project\build" --config Release --parallel 8 *> "$Project\logs\plugin-build.log"
if($LASTEXITCODE -ne 0){Get-Content "$Project\logs\plugin-build.log" -Tail 40;throw 'ASI build failed'}
Copy-Item -LiteralPath "$Project\build\Release\SM64VCNative.asi" -Destination "$Project\package\SM64VCNative.asi"
Copy-Item -LiteralPath "$Sm\dist\sm64.dll" -Destination "$Project\package\sm64.dll"
& gcc "$Project\tests\physics.c" '-I' "$Sm\src" "$Sm\dist\sm64.dll" '-o' "$Project\package\physics.exe" '-static-libgcc' '-lm' *> "$Project\logs\bank-build.log"
if($LASTEXITCODE -ne 0){throw 'physics bank build failed'}
Get-FileHash "$Project\package\SM64VCNative.asi","$Project\package\sm64.dll" | Format-List | Out-File "$Project\logs\artifacts.sha256.txt"
Write-Host "Built x86 ASI and libsm64: $Project\package"
