<#
.SYNOPSIS
    Generates procedural missions on every skirmish map and reports layout problems.

.DESCRIPTION
    run_tests.ps1 plays a few games in depth on one map; this checks generation breadth:
    each map x primary objective is started for a few seconds and the generator's log is
    checked for crashes, targets that could not be placed or ended up outside their area,
    and bases that start underpowered. Uses the DLL and harness built by run_tests.ps1.

.EXAMPLE
    .\sweep_maps.ps1
    .\sweep_maps.ps1 -Primaries COMMANDO_FORWARD_BASE -Maps 1..10
#>
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\CnCRemastered",
    [string[]]$Primaries = @("DESTROY_ALL", "SURVIVE_SIEGE", "DESTROY_COMMAND", "CAPTURE_LAB", "HOLD_THE_LINE", "COMMANDO_RAID", "COMMANDO_FORWARD_BASE",
        "ASSASSINATE_MARSHAL", "RESCUE_PRISONERS", "RESCUE_SQUAD", "HOLD_THE_HILL"),
    [int[]]$Maps = 1..24,
    [string]$Player = "Greece",
    [string]$Enemies = "USSR,England"
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$Run = Join-Path $PSScriptRoot "run"
$Exe = Join-Path $PSScriptRoot "bin\ProcTest.exe"
$Content = Join-Path $GameDir "Data\CNCDATA\RED_ALERT\CD1"

New-Item -ItemType Directory -Force $Run | Out-Null
Copy-Item "$Repo\bin\Win32\RedAlert.dll", "$Repo\bin\Win32\RedAlert.pdb" $Run -Force
Copy-Item "$GameDir\REDALERT.INI" $Run -Force

$problems = 0
$runs = 0
foreach ($primary in $Primaries) {
    $presets = Join-Path $Run "presets_sweep"
    New-Item -ItemType Directory -Force $presets | Out-Null
    Copy-Item "$Repo\REDALERT\PROC*.INI" $presets -Force
    $ini = Join-Path $presets "PROCMISSION.INI"
    (Get-Content $ini) -replace '^AllMaps=.*', 'AllMaps=yes' -replace '^Primary=.*', "Primary=$primary" -replace '^SecondaryMin=.*', 'SecondaryMin=3' -replace '^SecondaryMax=.*', 'SecondaryMax=3' |
        Set-Content $ini -Encoding ascii

    foreach ($map in $Maps) {
        $log = Join-Path $Run "sweep_$($primary)_$map.log"
        $argList = @("--dll", "`"$Run\RedAlert.dll`"", "--content", "`"$Content`"", "--mod", "`"$presets`"", "--scenario", "$map",
            "--player", $Player, "--enemies", $Enemies, "--ffa", "--frames", "60")
        $p = Start-Process -FilePath $Exe -ArgumentList $argList -WorkingDirectory $Run -NoNewWindow -PassThru -RedirectStandardOutput $log
        if (-not $p.WaitForExit(60000)) { Stop-Process $p.Id -Force }
        $lines = @(Get-Content $log)
        if ($lines -match "^END .*startfail") { continue }    # no such map
        $runs++

        $found = @()
        if ($lines -match "^END .*crash") { $found += ($lines | Select-String "^END .*crash").Line }
        if (-not ($lines -match "^END")) { $found += "harness died" }
        if (-not ($lines -match "generation complete")) { $found += "not generated" }
        if ($lines -match "falling back to destroy all") { $found += "primary fell back to destroy all" }
        # A bonus that doesn't fit is swapped for another one; only count the ones that weren't.
        foreach ($m in ($lines | Select-String "could not place target for objective (\S+)")) {
            $name = $m.Matches[0].Groups[1].Value
            if (-not ($lines -match "secondary objective $name replaced by")) { $found += "no target for $name" }
        }
        foreach ($m in ($lines | Select-String "objective (\S+) target \S+ at cell \d+ \((\d+) cells from the area centre, radius (\d+)")) {
            $out = [int]$m.Matches[0].Groups[2].Value; $radius = [int]$m.Matches[0].Groups[3].Value
            if ($out -gt [math]::Ceiling(($radius + 2) * 1.5)) { $found += "$($m.Matches[0].Groups[1].Value) target $out cells out" }
        }
        if ($primary -eq "HOLD_THE_HILL") {
            $points = @($lines | Select-String "objective HOLD_THE_HILL: control point").Count
            if ($points -lt 3) { $found += "only $points control points" }
        }
        foreach ($m in ($lines | Select-String "base '([^']+)'.*\(power (\d+)/(\d+)\)")) {
            if ([int]$m.Matches[0].Groups[2].Value -lt [int]$m.Matches[0].Groups[3].Value) { $found += "'$($m.Matches[0].Groups[1].Value)' power $($m.Matches[0].Groups[2].Value)/$($m.Matches[0].Groups[3].Value)" }
        }
        if ($found.Count -gt 0) {
            $problems++
            Write-Host ("{0,-22} map {1,2}: {2}" -f $primary, $map, ($found -join "; ")) -ForegroundColor Red
        }
    }
}

Write-Host ""
Write-Host "$runs generations, $problems with problems. Logs: $Run\sweep_*.log"
if ($problems -gt 0) { exit 1 }
exit 0
