<#
.SYNOPSIS
    Builds RedAlert.dll and installs it, with the procedural preset library, into the
    "Procedural Missions" Red Alert mod.

.EXAMPLE
    .\deploy_procmod.ps1                 # build + deploy, AllMaps=yes in the mod copy
    .\deploy_procmod.ps1 -NoBuild -AllMaps:$false
    .\deploy_procmod.ps1 -NoBuild -Spectate    # watch your lobby team's AI fight the enemy
#>
param(
    [string]$ModDir = (Join-Path ([Environment]::GetFolderPath("MyDocuments")) "CnCRemastered\Mods\Red_Alert\ProceduralMissions"),
    [bool]$AllMaps = $true,
    [switch]$Spectate,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path "$PSScriptRoot\..").Path
$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\MSBuild\15.0\Bin\MSBuild.exe"

if (-not $NoBuild) {
    $out = & $MSBuild "$Repo\REDALERT\RedAlert.vcxproj" -p:Configuration=Release -p:Platform=Win32 -m -v:minimal -nologo 2>&1
    if ($LASTEXITCODE -ne 0) { $out | Select-String "error" | ForEach-Object { Write-Host $_ }; throw "RedAlert.dll build failed" }
}

$data = Join-Path $ModDir "Data"
New-Item -ItemType Directory -Force $data | Out-Null

$json = Join-Path $ModDir "ccmod.json"
if (-not (Test-Path $json)) {
    '{ "name": "Procedural Missions", "description": "Mission Generator", "author": "Boso", "load_order": 1, "version_low": 2, "version_high": 1, "game_type": "RA" }' |
        Set-Content -Path $json -Encoding ascii
}

Copy-Item "$Repo\bin\Win32\RedAlert.dll", "$Repo\bin\Win32\RedAlert.pdb" $data -Force
foreach ($f in "PROCMISSION.INI", "PROCTEAMS.INI", "PROCBASES.INI", "PROCOBJECTIVES.INI", "PROCEVENTS.INI") {
    Copy-Item "$Repo\REDALERT\$f" $data -Force
}
$ini = Join-Path $data "PROCMISSION.INI"
$lines = Get-Content $ini
if ($AllMaps) { $lines = $lines -replace '^AllMaps=no', 'AllMaps=yes' }
if ($Spectate) { $lines = $lines -replace '^Spectate=no', 'Spectate=yes' }
$lines | Set-Content -Path $ini -Encoding ascii

Write-Host "Deployed to $data (AllMaps=$AllMaps, Spectate=$([bool]$Spectate))"
Get-ChildItem $data | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
