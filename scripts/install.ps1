param(
    [Parameter(Mandatory=$true)][string]$Destination,
    [Parameter(Mandatory=$true)][string]$Rom,
    [string]$Saves)
$ErrorActionPreference='Stop'
$Project=Split-Path $PSScriptRoot -Parent

function Assert-GameClosed {
    # Block every classic Vice City process, including those whose path is inaccessible.
    $Running=@(Get-CimInstance Win32_Process -Filter "Name='gta-vc.exe'")
    if($Running.Count){throw 'Close GTA Vice City before installing (gta-vc.exe is running)'}
}

function Copy-Verified([string]$Source, [string]$Target) {
    $Hash=(Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $Source -Destination $Target -Force
    if((Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -ne $Hash){
        throw "Copy verification failed: $Target"
    }
}

function Assert-CompiledDll([string]$Path) {
    if(!(Test-Path -LiteralPath $Path -PathType Leaf)){throw "Missing compiled mod file: $Path. Run scripts/build.ps1 first."}
    $Bytes=[IO.File]::ReadAllBytes($Path)
    if($Bytes.Length -lt 64 -or $Bytes[0] -ne 0x4d -or $Bytes[1] -ne 0x5a){throw "Invalid compiled PE file: $Path"}
    $Offset=[BitConverter]::ToUInt32($Bytes,0x3c)
    if([long]$Offset+26 -gt $Bytes.Length){throw "Invalid compiled PE header: $Path"}
    if([BitConverter]::ToUInt32($Bytes,$Offset) -ne 0x4550 -or
       [BitConverter]::ToUInt16($Bytes,$Offset+4) -ne 0x14c -or
       [BitConverter]::ToUInt16($Bytes,$Offset+24) -ne 0x10b -or
       !([BitConverter]::ToUInt16($Bytes,$Offset+22) -band 0x2000)){
        throw "Compiled mod must be an x86 PE DLL: $Path"
    }
}

$ResolvedDestination=(Resolve-Path -LiteralPath $Destination).ProviderPath
if(!(Test-Path -LiteralPath $ResolvedDestination -PathType Container)){throw 'Destination must be an existing game directory'}
Assert-GameClosed
$Exe=Join-Path $ResolvedDestination 'gta-vc.exe'
if(!(Test-Path -LiteralPath $Exe -PathType Leaf)){throw "Missing game executable: $Exe"}
if(!(Test-Path -LiteralPath $Rom -PathType Leaf)){throw "Missing ROM file: $Rom"}
$ResolvedRom=(Resolve-Path -LiteralPath $Rom).ProviderPath
$Files=@(
    @{Name='SM64VCNative.asi'; Source=(Join-Path $Project 'package\SM64VCNative.asi')},
    @{Name='sm64.dll'; Source=(Join-Path $Project 'package\sm64.dll')},
    @{Name='sm64.us.z64'; Source=$ResolvedRom})
foreach($File in $Files[0..1]){Assert-CompiledDll $File.Source}
foreach($File in $Files){
    $Existing=Join-Path $ResolvedDestination $File.Name
    if(Test-Path -LiteralPath $Existing){
        $Item=Get-Item -LiteralPath $Existing -Force
        if($Item.PSIsContainer -or ($Item.Attributes -band [IO.FileAttributes]::ReparsePoint)){
            throw "Refusing to overwrite a directory or linked file: $Existing"
        }
    }
}

# Preserve inspect.py's exact executable/ROM hashes, GAME_10EN detector and hook-byte inspection.
$ValidationJson=(& python (Join-Path $PSScriptRoot 'inspect.py') $Exe $ResolvedRom | Out-String)
if($LASTEXITCODE -ne 0){throw 'Executable or ROM identity rejected; nothing installed'}
$Validation=$ValidationJson | ConvertFrom-Json
if($Validation.accepted -ne $true){throw 'Executable or ROM identity rejected; nothing installed'}
Write-Host $ValidationJson.Trim()

if([string]::IsNullOrWhiteSpace($Saves)){
    # Windows resolves redirected Documents (including OneDrive); do not guess USERPROFILE\Documents.
    $Documents=[Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments)
    if([string]::IsNullOrWhiteSpace($Documents)){throw 'Windows Documents folder could not be identified. Specify -Saves with the verified Vice City user-files directory.'}
    $Saves=Join-Path $Documents 'GTA Vice City User Files'
}
if(!(Test-Path -LiteralPath $Saves -PathType Container)){
    throw "Vice City save directory not found: $Saves. Specify -Saves with the verified 'GTA Vice City User Files' directory; nothing installed."
}
$ResolvedSaves=(Resolve-Path -LiteralPath $Saves).ProviderPath
$SaveItems=@(Get-ChildItem -LiteralPath $ResolvedSaves -Force)
$Markers=@($SaveItems | Where-Object {
    !$_.PSIsContainer -and $_.Length -gt 0 -and
    ($_.Name -ieq 'gta_vc.set' -or $_.Name -imatch '^GTAVCsf[1-8]\.b$')
})
if(!$Markers.Count){throw "Cannot identify Vice City saves safely in ${ResolvedSaves}: expected gta_vc.set or GTAVCsf1.b through GTAVCsf8.b. Nothing installed."}
$SaveTree=@(Get-ChildItem -LiteralPath $ResolvedSaves -Recurse -Force)
if(@($SaveTree | Where-Object {$_.LinkType}).Count){
    throw "Save directory contains symbolic links or junctions that cannot be backed up safely: $ResolvedSaves. Specify the actual Vice City user-files directory; nothing installed."
}
# Avoid recursive backups if an explicitly supplied -Saves includes the destination.
if(($ResolvedDestination.TrimEnd('\')+'\').StartsWith($ResolvedSaves.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){
    throw 'Destination must not be inside the save directory; nothing installed'
}

# No destination files are overwritten until ALL backups and staged copies have been verified.
$Rollback=Join-Path $ResolvedDestination ('SM64VCNative-backups\'+(Get-Date -Format yyyyMMdd-HHmmss)+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
$Previous=Join-Path $Rollback 'previous'
$Staged=Join-Path $Rollback 'install'
$SaveBackup=Join-Path $Rollback 'saves'
New-Item -ItemType Directory -Path $Previous,$Staged,$SaveBackup | Out-Null
$Manifest=@{saves_source=$ResolvedSaves; validation=$Validation; previous=@(); installed=@(); saves=@()}
foreach($Item in $SaveTree){
    $Relative=$Item.FullName.Substring($ResolvedSaves.TrimEnd('\').Length+1)
    $Target=Join-Path $SaveBackup $Relative
    if($Item.PSIsContainer){New-Item -ItemType Directory -Path $Target -Force | Out-Null}
    else{
        Copy-Verified $Item.FullName $Target
        $Manifest.saves+=@{path=$Relative; sha256=(Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash}
    }
}
foreach($File in $Files){
    $Existing=Join-Path $ResolvedDestination $File.Name
    if(Test-Path -LiteralPath $Existing){
        $BackupFile=Join-Path $Previous $File.Name
        Copy-Verified $Existing $BackupFile
        $Manifest.previous+=@{path=$File.Name; sha256=(Get-FileHash -LiteralPath $BackupFile -Algorithm SHA256).Hash}
    }
    $StageFile=Join-Path $Staged $File.Name
    Copy-Verified $File.Source $StageFile
    if($File.Name -ne 'sm64.us.z64'){Assert-CompiledDll $StageFile}
    $Manifest.installed+=@{path=$File.Name; sha256=(Get-FileHash -LiteralPath $StageFile -Algorithm SHA256).Hash}
}
# Validate the frozen ROM again, together with the still-unmodified game executable.
& python (Join-Path $PSScriptRoot 'inspect.py') $Exe (Join-Path $Staged 'sm64.us.z64') | Out-Null
if($LASTEXITCODE -ne 0){throw "Staged executable/ROM validation failed; nothing installed. Backup: $Rollback"}
$Manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Rollback 'manifest.json') -Encoding UTF8
Assert-GameClosed
$Touched=@()
try {
    foreach($File in $Files){
        $Touched+=$File.Name
        Copy-Verified (Join-Path $Staged $File.Name) (Join-Path $ResolvedDestination $File.Name)
    }
} catch {
    $InstallError=$_
    foreach($Name in $Touched){
        $Target=Join-Path $ResolvedDestination $Name
        $Old=Join-Path $Previous $Name
        try {
            if(Test-Path -LiteralPath $Old){Copy-Verified $Old $Target}
            elseif(Test-Path -LiteralPath $Target){Remove-Item -LiteralPath $Target -Force}
        } catch {Write-Warning "Automatic restoration failed for $Target. Restore it from $Previous. $($_.Exception.Message)"}
    }
    throw "Installation failed; restoration attempted. Backup: $Rollback. $($InstallError.Exception.Message)"
}
Write-Host "Installed in $ResolvedDestination :"
foreach($File in $Files){Write-Host "  $($File.Name)"}
Write-Host "Backup: $Rollback"
Write-Host "Saved games copied from $ResolvedSaves to $SaveBackup (originals unchanged)."
