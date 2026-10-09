# Integration tests using the REAL inspect.py and the user's read-only input files.
# Every write is confined to a unique directory under the Windows temporary directory.
# No Pester, Universal Modder, game launch or real installation is needed.
param(
    [Parameter(Mandatory=$true)][string]$GameExe,
    [Parameter(Mandatory=$true)][string]$Rom)
$ErrorActionPreference='Stop'
$Project=Split-Path $PSScriptRoot -Parent
$GameExe=(Resolve-Path -LiteralPath $GameExe).ProviderPath
$Rom=(Resolve-Path -LiteralPath $Rom).ProviderPath
$TestRoot=Join-Path ([IO.Path]::GetTempPath()) ('sm64-vc-installer-tests-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TestRoot | Out-Null
$script:Case=0
$script:Failures=0
$global:SM64VCInstallerTestState=@{ProcessQueries=0; RunningOnQuery=0; ProcessError=$false; FailCopyTarget=$null; FailCopySource=$null}

# Only the OS process enumeration and deliberate I/O failures are mocked.
# The production executable, ROM, hook-byte and compiled-PE validators all run.
function Get-CimInstance([string]$ClassName, [string]$Filter) {
    if($ClassName -ne 'Win32_Process' -or $Filter -ne "Name='gta-vc.exe'"){throw 'Unexpected process query'}
    $State=$global:SM64VCInstallerTestState
    $State.ProcessQueries++
    if($State.ProcessError){throw 'Simulated process enumeration failure'}
    if($State.RunningOnQuery -and $State.ProcessQueries -ge $State.RunningOnQuery){
        [pscustomobject]@{ProcessId=12345; ExecutablePath=$null}
    }
}
function Copy-Item {
    param([string]$LiteralPath,[string]$Destination,[switch]$Force)
    $State=$global:SM64VCInstallerTestState
    if(($State.FailCopyTarget -and $Destination -eq $State.FailCopyTarget) -or
       ($State.FailCopySource -and $LiteralPath -eq $State.FailCopySource)){
        $State.FailCopyTarget=$null; $State.FailCopySource=$null
        throw 'Simulated copy failure'
    }
    Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Force:$Force
}
function Assert([bool]$Condition,[string]$Message) {
    if(!$Condition){throw "Assertion failed: $Message"}
}
function Snapshot([string]$Directory) {
    $Result=@{}
    if(!(Test-Path -LiteralPath $Directory -PathType Container)){return $Result}
    foreach($File in Get-ChildItem -LiteralPath $Directory -Recurse -File -Force){
        $Result[$File.FullName.Substring($Directory.Length+1)]=(Get-FileHash -LiteralPath $File.FullName -Algorithm SHA256).Hash
    }
    return $Result
}
function Assert-Same($Before,$After,[string]$Description) {
    Assert ($Before.Count -eq $After.Count) "$Description file count"
    foreach($Name in $Before.Keys){Assert ($Before[$Name] -eq $After[$Name]) "$Description changed: $Name"}
}
function Run-Case([string]$Name,[scriptblock]$Arrange,[string]$ExpectedError='', [scriptblock]$Check={},[switch]$AutomaticSaves) {
    $script:Case++
    $global:SM64VCInstallerTestState=@{ProcessQueries=0; RunningOnQuery=0; ProcessError=$false; FailCopyTarget=$null; FailCopySource=$null}
    $Root=Join-Path $TestRoot ("$($script:Case) $Name [literal]")
    $FixtureProject=Join-Path $Root 'relocated project'
    $Package=Join-Path $FixtureProject 'package'
    $Scripts=Join-Path $FixtureProject 'scripts'
    $Game=Join-Path $Root 'game [VC]'
    $SaveDir=Join-Path $Root 'redirected Documents\GTA Vice City User Files'
    New-Item -ItemType Directory -Path $Package,$Scripts,$Game,$SaveDir | Out-Null
    foreach($NameToCopy in @('install.ps1','inspect.py')){
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $NameToCopy) -Destination (Join-Path $Scripts $NameToCopy)
    }
    foreach($NameToCopy in @('SM64VCNative.asi','sm64.dll')){
        Copy-Item -LiteralPath (Join-Path $Project "package\$NameToCopy") -Destination (Join-Path $Package $NameToCopy)
    }
    Copy-Item -LiteralPath $GameExe -Destination (Join-Path $Game 'gta-vc.exe')
    $FixtureRom=Join-Path $Root 'own ROM [!].z64'
    Copy-Item -LiteralPath $Rom -Destination $FixtureRom
    Set-Content -LiteralPath (Join-Path $SaveDir 'gta_vc.set') -Value 'test settings'
    Set-Content -LiteralPath (Join-Path $SaveDir 'GTAVCsf1.b') -Value 'test save 1'
    $Before=@{}
    try {
        . $Arrange
        if($AutomaticSaves){$SaveDir=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'GTA Vice City User Files'}
        $Before=Snapshot $Game
        $SavesBefore=Snapshot $SaveDir
        $ErrorText=''
        $Output=@()
        try {
            $Arguments=@{Destination=$Game; Rom=$FixtureRom}
            if(!$AutomaticSaves){$Arguments.Saves=$SaveDir}
            $Output=@(& (Join-Path $Scripts 'install.ps1') @Arguments *>&1)
        }
        catch {$ErrorText=$_.Exception.Message}
        if($ExpectedError){Assert ($ErrorText -like "*$ExpectedError*") "Expected '$ExpectedError'; got '$ErrorText'"}
        else {Assert (!$ErrorText) "Unexpected installer failure: $ErrorText"}
        Assert-Same $SavesBefore (Snapshot $SaveDir) 'save originals'
        if($Before.ContainsKey('gta-vc.exe')){
            Assert ((Get-FileHash -LiteralPath (Join-Path $Game 'gta-vc.exe')).Hash -eq $Before['gta-vc.exe']) 'game executable unchanged'
        }
        $BackupDirs=@(Get-ChildItem -LiteralPath $Game -Directory | Where-Object {$_.Name -eq 'SM64VCNative-backups'} | Get-ChildItem -Directory)
        $Backup=$null
        if($BackupDirs.Count){Assert ($BackupDirs.Count -eq 1) 'one backup'; $Backup=$BackupDirs[0].FullName}
        if(!$ExpectedError){
            Assert ($null -ne $Backup) 'backup exists'
            Assert-Same $SavesBefore (Snapshot (Join-Path $Backup 'saves')) 'complete save backup'
            foreach($Artifact in @('SM64VCNative.asi','sm64.dll','sm64.us.z64')){
                $Source=Join-Path $Package $Artifact
                if($Artifact -eq 'sm64.us.z64'){$Source=$FixtureRom}
                Assert ((Get-FileHash -LiteralPath (Join-Path $Game $Artifact)).Hash -eq (Get-FileHash -LiteralPath $Source).Hash) "installed $Artifact"
            }
            Assert (@($Output | Where-Object {$_.ToString() -eq "Backup: $Backup"}).Count -eq 1) 'summary includes backup location'
            $Manifest=Get-Content -LiteralPath (Join-Path $Backup 'manifest.json') -Raw | ConvertFrom-Json
            Assert ($Manifest.validation.accepted -eq $true) 'manifest records real validation'
            Assert ($Manifest.installed.Count -eq 3) 'manifest records three installed files'
        } else {
            $After=Snapshot $Game
            foreach($Key in @($After.Keys)){if($Key.StartsWith('SM64VCNative-backups\')){$After.Remove($Key)}}
            Assert-Same $Before $After 'failed installation leaves game files unchanged'
        }
        . $Check
        Write-Host "PASS $($script:Case): $Name"
    } catch {
        $script:Failures++
        Write-Host "FAIL $($script:Case): $Name -- $($_.Exception.Message)"
    }
}

Run-Case 'fresh install' {}
Run-Case 'upgrade and complete backups' {
    foreach($Artifact in @('SM64VCNative.asi','sm64.dll','sm64.us.z64')){Set-Content -LiteralPath (Join-Path $Game $Artifact) -Value "previous $Artifact"}
    New-Item -ItemType Directory -Path (Join-Path $SaveDir 'nested [settings]\empty') | Out-Null
    Set-Content -LiteralPath (Join-Path $SaveDir 'nested [settings]\extra.dat') -Value 'also backed up'
} '' {
    foreach($Artifact in @('SM64VCNative.asi','sm64.dll','sm64.us.z64')){
        Assert ((Get-FileHash -LiteralPath (Join-Path $Backup "previous\$Artifact")).Hash -eq $Before[$Artifact]) "previous $Artifact backed up"
    }
    Assert (Test-Path -LiteralPath (Join-Path $Backup 'saves\nested [settings]\empty') -PathType Container) 'empty save directories preserved'
}
Run-Case 'missing ASI' {Remove-Item -LiteralPath (Join-Path $Package 'SM64VCNative.asi')} 'Missing compiled mod file'
Run-Case 'missing DLL' {Remove-Item -LiteralPath (Join-Path $Package 'sm64.dll')} 'Missing compiled mod file'
Run-Case 'empty compiled file' {[IO.File]::WriteAllBytes((Join-Path $Package 'sm64.dll'),[byte[]]@())} 'Invalid compiled PE file'
Run-Case 'wrong compiled architecture' {
    $Path=Join-Path $Package 'sm64.dll'; $Bytes=[IO.File]::ReadAllBytes($Path)
    $Offset=[BitConverter]::ToUInt32($Bytes,0x3c); $Bytes[$Offset+4]=0x64; $Bytes[$Offset+5]=0x86
    [IO.File]::WriteAllBytes($Path,$Bytes)
} 'Compiled mod must be an x86 PE DLL'
Run-Case 'modified executable' {
    $Path=Join-Path $Game 'gta-vc.exe'; $Bytes=[IO.File]::ReadAllBytes($Path); $Bytes[$Bytes.Length-1]=$Bytes[$Bytes.Length-1] -bxor 1
    [IO.File]::WriteAllBytes($Path,$Bytes)
} 'Executable or ROM identity rejected'
Run-Case 'modified hook bytes' {
    $Path=Join-Path $Game 'gta-vc.exe'; $Bytes=[IO.File]::ReadAllBytes($Path)
    $Pe=[BitConverter]::ToUInt32($Bytes,0x3c)
    $Sections=[BitConverter]::ToUInt16($Bytes,$Pe+6)
    $Opt=$Pe+24; $Size=[BitConverter]::ToUInt16($Bytes,$Pe+20)
    $Rva=0x4a5da0-[BitConverter]::ToUInt32($Bytes,$Opt+28)
    $Found=$false
    for($Index=0;$Index -lt $Sections;$Index++){
        $Section=$Opt+$Size+40*$Index
        $Address=[BitConverter]::ToUInt32($Bytes,$Section+12)
        $RawSize=[BitConverter]::ToUInt32($Bytes,$Section+16)
        if($Address -le $Rva -and $Rva -lt $Address+$RawSize){
            $Raw=[BitConverter]::ToUInt32($Bytes,$Section+20)
            $Bytes[$Raw+$Rva-$Address]=$Bytes[$Raw+$Rva-$Address] -bxor 1
            $Found=$true; break
        }
    }
    Assert $Found 'hook mapped through real PE sections'
    [IO.File]::WriteAllBytes($Path,$Bytes)
} 'Executable or ROM identity rejected'
Run-Case 'modified ROM' {
    $Bytes=[IO.File]::ReadAllBytes($FixtureRom); $Bytes[$Bytes.Length-1]=$Bytes[$Bytes.Length-1] -bxor 1
    [IO.File]::WriteAllBytes($FixtureRom,$Bytes)
} 'Executable or ROM identity rejected'
Run-Case 'missing ROM' {Remove-Item -LiteralPath $FixtureRom} 'Missing ROM file'
Run-Case 'missing executable' {Remove-Item -LiteralPath (Join-Path $Game 'gta-vc.exe')} 'Missing game executable'
Run-Case 'unknown save directory' {
    Remove-Item -LiteralPath (Join-Path $SaveDir 'gta_vc.set'),(Join-Path $SaveDir 'GTAVCsf1.b')
} 'Cannot identify Vice City saves safely'
Run-Case 'wrong save path' {$SaveDir=Join-Path $Root 'absent saves'} 'Vice City save directory not found'
Run-Case 'save files without settings' {Remove-Item -LiteralPath (Join-Path $SaveDir 'gta_vc.set')}
Run-Case 'settings without saves' {Remove-Item -LiteralPath (Join-Path $SaveDir 'GTAVCsf1.b')}
Run-Case 'target is a directory' {New-Item -ItemType Directory -Path (Join-Path $Game 'sm64.dll') | Out-Null} 'Refusing to overwrite'
Run-Case 'game running with inaccessible path' {$global:SM64VCInstallerTestState.RunningOnQuery=1} 'Close GTA Vice City'
Run-Case 'process query fails' {$global:SM64VCInstallerTestState.ProcessError=$true} 'Simulated process enumeration failure'
Run-Case 'game starts before commit' {$global:SM64VCInstallerTestState.RunningOnQuery=2} 'Close GTA Vice City'
Run-Case 'backup copy fails' {$global:SM64VCInstallerTestState.FailCopySource=Join-Path $SaveDir 'GTAVCsf1.b'} 'Simulated copy failure'
Run-Case 'failure restores previous binaries and ROM' {
    foreach($Artifact in @('SM64VCNative.asi','sm64.dll','sm64.us.z64')){Set-Content -LiteralPath (Join-Path $Game $Artifact) -Value "previous $Artifact"}
    $global:SM64VCInstallerTestState.FailCopyTarget=Join-Path $Game 'sm64.dll'
} 'Installation failed; restoration attempted'
Run-Case 'failure removes newly installed files' {$global:SM64VCInstallerTestState.FailCopyTarget=Join-Path $Game 'sm64.dll'} 'Installation failed; restoration attempted'

# Read-only check of the real Windows Documents discovery. Backups go ONLY to the fixture.
$DefaultSaves=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'GTA Vice City User Files'
if(Test-Path -LiteralPath $DefaultSaves -PathType Container){
    Run-Case 'automatic Windows Documents detection' {} '' {} -AutomaticSaves
} else {
    Run-Case 'automatic discovery stops on missing saves' {} 'Vice City save directory not found' {} -AutomaticSaves
}

Write-Host "Results: $($script:Case-$script:Failures)/$($script:Case) passed. Temporary fixtures retained at $TestRoot"
if($script:Failures){exit 1}
