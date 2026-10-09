param([string]$Deps="$env:USERPROFILE\.universal-modder\sm64-vc\deps")
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $Deps -Force | Out-Null
$Pins=@(
    @('plugin-sdk','https://github.com/DK22Pac/plugin-sdk.git','15f15b60bbf74c106e1b496ff92c98764abf4605'),
    @('libsm64','https://github.com/libsm64/libsm64.git','fd11813208272b4271d92bd92feb8f3fdbe61be5'),
    @('minhook','https://github.com/TsudaKageyu/minhook.git','c3fcafdc10146beb5919319d0683e44e3c30d537'))
foreach($Pin in $Pins){
    $Checkout=Join-Path $Deps $Pin[0]
    if(!(Test-Path -LiteralPath $Checkout)){
        & git clone $Pin[1] $Checkout
        if($LASTEXITCODE){throw 'git clone failed'}
        & git -C $Checkout checkout --detach $Pin[2]
        if($LASTEXITCODE){throw 'git checkout failed'}
    }
    if((& git -C $Checkout rev-parse HEAD) -ne $Pin[2]){throw "Dependency pin differs: $Checkout"}
}
if(!(Test-Path "$Deps\w64devkit\bin\gcc.exe")){
    $Archive=Join-Path $Deps 'w64devkit-x86-2.10.0.7z.exe'
    Invoke-WebRequest -UseBasicParsing 'https://github.com/skeeto/w64devkit/releases/download/v2.10.0/w64devkit-x86-2.10.0.7z.exe' -OutFile $Archive
    if((Get-FileHash $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne '513ff7fc571cf6764a5aedbca92f43c513f8d01085c65df5133a82db13a7e63d'){throw 'Compiler archive hash mismatch'}
    $Extraction=Start-Process -FilePath $Archive -ArgumentList @('-y',('-o'+$Deps)) -WindowStyle Hidden -PassThru -Wait
    if($Extraction.ExitCode){throw 'Compiler extraction failed'}
}
Write-Host 'Pinned dependencies ready. Visual Studio 2022 Build Tools (C++ x86) and CMake must also be installed.'
