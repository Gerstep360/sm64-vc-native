param(
    [string]$Destination='C:\Games\GTA_VC_SM64_Lab',
    [Parameter(Mandatory=$true)][string]$Rom,
    [string]$Saves="$env:USERPROFILE\Documents\GTA Vice City User Files")
$ErrorActionPreference='Stop'
$Project=Split-Path $PSScriptRoot -Parent
$ResolvedDestination=(Resolve-Path -LiteralPath $Destination).Path
if($ResolvedDestination.TrimEnd('\') -ieq 'C:\Games\GTA_VC_1.0'){throw 'Use the backed-up lab copy, not the original installation'}
$Running=Get-CimInstance Win32_Process -Filter "Name='gta-vc.exe'" | Where-Object {$_.ExecutablePath -ieq (Join-Path $ResolvedDestination 'gta-vc.exe')}
if($Running){throw 'Close this lab game before replacing its ASI/DLL'}
& python "$PSScriptRoot\inspect.py" (Join-Path $ResolvedDestination 'gta-vc.exe') $Rom
if($LASTEXITCODE){throw 'Executable or ROM identity rejected'}
$Um=Join-Path (Split-Path (Split-Path $Project -Parent) -Parent) '.venv\Scripts\um.exe'
if(!(Test-Path $Um)){throw 'Universal Modder environment not found; back up saves before installing'}
& $Um backup create $Saves --name vc-sm64-saves
if($LASTEXITCODE){throw 'Save backup failed'}
$Rollback=Join-Path "$env:USERPROFILE\.universal-modder\sm64-vc" ('rollback-'+(Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Force $Rollback | Out-Null
foreach($Name in @('SM64VCNative.asi','sm64.dll')){
    $Existing=Join-Path $ResolvedDestination $Name
    if(Test-Path -LiteralPath $Existing){Copy-Item -LiteralPath $Existing -Destination $Rollback}
    Copy-Item -LiteralPath (Join-Path "$Project\package" $Name) -Destination $Existing -Force
}
Copy-Item -LiteralPath $Rom -Destination (Join-Path $ResolvedDestination 'sm64.us.z64') -Force
Write-Host "Installed in $ResolvedDestination; previous plugin files: $Rollback"
