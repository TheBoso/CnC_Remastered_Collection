<#
.SYNOPSIS
    Automated tests for the procedural mission generator.

.DESCRIPTION
    Builds RedAlert.dll (Release) and the ProcTest harness, then plays a set of headless
    skirmish games through the real DLL and checks the generator's debug output.
    Each test gets its own copy of the preset library (patched for that test) as the
    first mod search path, so it never touches the repo copies.

    Logs go to tools\ProcTest\run\<test>.log.

.EXAMPLE
    .\run_tests.ps1                     # build + all tests
    .\run_tests.ps1 -NoBuild -Only survive_win
    .\run_tests.ps1 -Repeat 5           # run every test 5 times (different random seeds)
#>
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\CnCRemastered",
    [string[]]$Only = @(),
    [int]$Repeat = 1,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$Here = $PSScriptRoot
$Run = Join-Path $Here "run"
$Content = Join-Path $GameDir "Data\CNCDATA\RED_ALERT\CD1"
$MSBuild = "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\MSBuild\15.0\Bin\MSBuild.exe"
$PresetFiles = @("PROCMISSION.INI", "PROCTEAMS.INI", "PROCBASES.INI", "PROCOBJECTIVES.INI")

# Teams that need vehicles; none of them may appear at tech level 1.
$ArmorTeams = @("SOV_HEAVY_RAID", "SOV_ARMOR_COLUMN", "SOV_V2_STRIKE", "SOV_HARV_HUNTERS", "SOV_COMBINED_ARMS",
    "SOV_MAMMOTH_SPEAR", "SOV_IRON_FIST", "ALL_JEEP_RAID", "ALL_LIGHT_ARMOR", "ALL_APC_ASSAULT", "ALL_MEDIUM_ARMOR",
    "ALL_ARTY_SIEGE", "ALL_HARV_HUNTERS", "ALL_COMBINED_ARMS", "ALL_STEEL_WALL")

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------

function Set-IniValue([string]$Path, [string]$Section, [string]$Key, [string]$Value) {
    $lines = [System.Collections.Generic.List[string]](Get-Content $Path)
    $start = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Trim() -ieq "[$Section]") { $start = $i; break }
    }
    if ($start -lt 0) {
        $lines.Add(""); $lines.Add("[$Section]"); $lines.Add("$Key=$Value")
    } else {
        $end = $lines.Count
        for ($i = $start + 1; $i -lt $lines.Count; $i++) {
            if ($lines[$i].Trim().StartsWith("[")) { $end = $i; break }
        }
        $found = $false
        for ($i = $start + 1; $i -lt $end; $i++) {
            if ($lines[$i] -match "^\s*$([regex]::Escape($Key))\s*=") { $lines[$i] = "$Key=$Value"; $found = $true; break }
        }
        if (-not $found) { $lines.Insert($start + 1, "$Key=$Value") }
    }
    Set-Content -Path $Path -Value $lines -Encoding ascii
}

function Get-Status($Lines) {
    # "ProcMission: status t=120s threat=1 waves=1/2 types=2 enemies=[18,9] player=12 objectives=[A,C]"
    foreach ($l in $Lines) {
        if ($l -match "status t=(\d+)s threat=(\d+) waves=(\d+)/(\d+) types=(\d+) enemies=\[([^\]]*)\] player=(-?\d+) objectives=\[([^\]]*)\]") {
            [pscustomobject]@{
                Seconds = [int]$Matches[1]; Threat = [int]$Matches[2]; Waves = [int]$Matches[3]
                Enemies = $Matches[6]; Player = [int]$Matches[7]; Objectives = $Matches[8]
            }
        }
    }
}

# -----------------------------------------------------------------------------
# Test definitions
#   Args     extra ProcTest arguments
#   Patch    { param($dir) ... } edits the test's preset copies
#   Check    { param($r) ... } returns a list of failure strings ($r has Lines, Status, Waves, End)
# -----------------------------------------------------------------------------

$Tests = [ordered]@{

    assault_1v1 = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--tech", "10", "--frames", "18000")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            # The harness never deploys an MCV, so give the idle player a base the waves can hit.
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
        }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "generation complete")) { $f += "generation did not complete" }
            $base = $r.Lines | Select-String "enemy 0 \(Soviet\) base '([^']+)', (\d+) buildings" | Select-Object -First 1
            if (-not $base) { $f += "no Soviet enemy base" }
            elseif ([int]$base.Matches[0].Groups[2].Value -lt 8) { $f += "enemy base only has $($base.Matches[0].Groups[2].Value) buildings" }
            if (-not ($r.Lines -match "^MSG .*Objective: Destroy all enemy forces")) { $f += "primary objective not announced" }
            if ($r.Waves.Count -lt 4) { $f += "only $($r.Waves.Count) waves sent in 20 minutes" }
            $over = @($r.Lines | Select-String "wave PROCW\d+ over after (\d+)s" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
            if ($over.Count -gt 0 -and ($over | Measure-Object -Maximum).Maximum -lt 40) { $f += "every wave disbanded within 40s (waves never engage)" }
            $first = $r.Status | Where-Object { $_.Seconds -eq 60 } | Select-Object -First 1
            $last = $r.Status | Select-Object -Last 1
            if ($first -and $last -and $last.Player -ge $first.Player) { $f += "player base untouched after 20 minutes ($($first.Player) -> $($last.Player) buildings)" }
            $maxthreat = ($r.Status | Measure-Object Threat -Maximum).Maximum
            if ($maxthreat -lt 2) { $f += "threat never escalated (max $maxthreat)" }
            $f
        }
    }

    low_tech = @{
        Args = @("--player", "USSR", "--enemies", "Greece", "--tech", "1", "--frames", "10800")
        Patch = { param($d) }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "tech=2 ")) { $f += "lobby tech 1 did not map to tech level 2" }
            $base = $r.Lines | Select-String "enemy 0 \(Allied\) base '([^']+)', (\d+) buildings" | Select-Object -First 1
            if (-not $base) { $f += "no Allied enemy base" }
            elseif ([int]$base.Matches[0].Groups[2].Value -lt 3) { $f += "low tech enemy base only has $($base.Matches[0].Groups[2].Value) buildings" }
            if ($r.Waves.Count -lt 1) { $f += "no waves at low tech" }
            $armor = @($r.Waves | Where-Object { $ArmorTeams -contains $_ })
            if ($armor.Count -gt 0) { $f += "vehicle waves at tech 1: $($armor -join ', ')" }
            $f
        }
    }

    soviet_2v2_ffa = @{
        Args = @("--player", "USSR", "--allies", "Ukraine", "--enemies", "Greece,England", "--ffa", "--tech", "10", "--frames", "9000")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "AllyStart" "Base" }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "side=Soviet allies=1 enemies=2")) { $f += "lobby teams not classified as 1 ally / 2 enemies" }
            if (@($r.Lines -match "enemy \d \(Allied\) base").Count -lt 2) { $f += "both enemies should get a base" }
            if (-not ($r.Lines -match "ally .* starts with base")) { $f += "ally with AllyStart=Base got no base" }
            $attackers = @($r.Lines | Select-String "wave \S+ \(sent.* from (.+) ->" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique)
            $f
        }
    }

    survive_win = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--tech", "10", "--frames", "4500")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "SURVIVE_SIEGE" "MinMinutes" "2"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "SURVIVE_SIEGE" "MaxMinutes" "2"
        }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "player Tester starts with base")) { $f += "survival should hand the player a base" }
            if (-not ($r.Lines -match "^MSG .*Hold out against the enemy assault for 2 minutes")) { $f += "survival objective text missing or wrong" }
            $over = $r.Lines | Select-String "^OVER (\d+) (\w+)" | Select-Object -First 1
            if (-not $over) { $f += "game did not end after the 2 minute timer" }
            else {
                $frame = [int]$over.Matches[0].Groups[1].Value; $result = $over.Matches[0].Groups[2].Value
                if ($result -ne "win") { $f += "survival ended in '$result'" }
                if ($frame -lt 1700 -or $frame -gt 2000) { $f += "survival ended at frame $frame, expected about 1800" }
            }
            $f
        }
    }

    destroy_target = @{
        Args = @("--player", "England", "--enemies", "USSR", "--tech", "10", "--frames", "1800")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_COMMAND" }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "primary objective DESTROY_COMMAND")) { $f += "forced primary not used" }
            if (-not ($r.Lines -match "objective DESTROY_COMMAND target (STEK|ATEK|DOME) at cell")) { $f += "command center target not placed" }
            if ($r.Lines -match "falling back to destroy all enemies") { $f += "primary fell back" }
            $f
        }
    }

    protect_with_base = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1800")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "SecondaryMin" "1"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "SecondaryMax" "1"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "BONUS_PROTECT_SILOS" "Weight" "100000"
        }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "secondary objective BONUS_PROTECT_SILOS")) { $f += "protect bonus not offered with a base start" }
            $low = @($r.Lines | Select-String "^MSG (\d+) TEXT_LOW_POWER" | Where-Object { [int]$_.Matches[0].Groups[1].Value -lt 900 })
            if ($low.Count -gt 0) { $f += "low power warning in the first minute (frame $($low[0].Matches[0].Groups[1].Value))" }
            $f
        }
    }

    protect_needs_base = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "300")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "MCV"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "SecondaryMin" "2"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "SecondaryMax" "2"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "BONUS_PROTECT_SILOS" "Weight" "100000"
        }
        Check = { param($r)
            $f = @()
            if ($r.Lines -match "objective BONUS_PROTECT_SILOS") { $f += "player-base objective offered with an MCV start" }
            if ($r.Lines -match "player Tester starts with base") { $f += "MCV start produced a base" }
            $f
        }
    }

    presets_beside_dll = @{
        # How the real client runs a mod: RedAlert.dll and the presets share the mod's Data
        # folder, which is not on the engine's file search path.
        BesideDll = $true
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "900")
        Patch = { param($d) }
        Check = { param($r)
            $f = @()
            if ($r.Lines -match "ProcINI: .* not found") { $f += "presets next to RedAlert.dll were not found" }
            if (-not ($r.Lines -match "ProcINI: loaded \d+ teams, \d+ bases, \d+ objectives") -or ($r.Lines -match "loaded 0 teams")) { $f += "preset library did not load" }
            if (-not ($r.Lines -match "generation complete")) { $f += "generation did not complete" }
            $f
        }
    }

    allmaps_off = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "600")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "AllMaps" "no" }
        Check = { param($r)
            $f = @()
            if ($r.Lines -match "generating procedural mission") { $f += "generator ran on an unflagged map with AllMaps=no" }
            $f
        }
    }
}

# -----------------------------------------------------------------------------
# Build
# -----------------------------------------------------------------------------

if (-not $NoBuild) {
    Write-Host "Building RedAlert.dll..."
    $out = & $MSBuild "$Repo\REDALERT\RedAlert.vcxproj" -p:Configuration=Release -p:Platform=Win32 -m -v:minimal -nologo 2>&1
    if ($LASTEXITCODE -ne 0) { $out | Select-String "error" | ForEach-Object { Write-Host $_ }; throw "RedAlert.dll build failed" }
    Write-Host "Building ProcTest.exe..."
    $out = & cmd /c "`"$Here\build.bat`"" 2>&1
    if ($LASTEXITCODE -ne 0) { $out | ForEach-Object { Write-Host $_ }; throw "ProcTest build failed" }
}

New-Item -ItemType Directory -Force $Run | Out-Null
Copy-Item "$Repo\bin\Win32\RedAlert.dll", "$Repo\bin\Win32\RedAlert.pdb" $Run -Force
# DLL_Startup changes to the DLL's folder and reads REDALERT.INI there.
Copy-Item "$GameDir\REDALERT.INI" $Run -Force

# -----------------------------------------------------------------------------
# Run
# -----------------------------------------------------------------------------

$results = @()
foreach ($name in $Tests.Keys) {
    if ($Only.Count -gt 0 -and $Only -notcontains $name) { continue }
    $test = $Tests[$name]

    for ($rep = 1; $rep -le $Repeat; $rep++) {
        $label = if ($Repeat -gt 1) { "$name#$rep" } else { $name }
        $presets = Join-Path $Run "presets_$name"
        New-Item -ItemType Directory -Force $presets | Out-Null
        foreach ($f in $PresetFiles) { Copy-Item "$Repo\REDALERT\$f" $presets -Force }
        # Stock maps are not flagged Procedural=yes, so tests run with AllMaps on unless they say otherwise.
        Set-IniValue "$presets\PROCMISSION.INI" "Generator" "AllMaps" "yes"
        & $test.Patch $presets

        $log = Join-Path $Run "$($label -replace '#','_').log"
        if ($test.BesideDll) {
            # Separate folder: DLL + REDALERT.INI + presets together, no mod search path.
            $dllDir = Join-Path $Run "beside_$name"
            New-Item -ItemType Directory -Force $dllDir | Out-Null
            Copy-Item "$Run\RedAlert.dll", "$Run\RedAlert.pdb", "$Run\REDALERT.INI" $dllDir -Force
            Copy-Item "$presets\*.INI" $dllDir -Force
            $argList = @("--dll", "`"$dllDir\RedAlert.dll`"", "--content", "`"$Content`"") + $test.Args
        } else {
            $argList = @("--dll", "`"$Run\RedAlert.dll`"", "--content", "`"$Content`"", "--mod", "`"$presets`"") + $test.Args
        }
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $p = Start-Process -FilePath "$Here\bin\ProcTest.exe" -ArgumentList $argList -WorkingDirectory $Run `
            -NoNewWindow -PassThru -Wait -RedirectStandardOutput $log
        $seconds = [int]$sw.Elapsed.TotalSeconds

        $lines = @(Get-Content $log)
        $r = [pscustomobject]@{
            Lines = $lines
            Status = @(Get-Status $lines)
            Waves = @($lines | Select-String "wave (\S+) \(sent" | ForEach-Object { $_.Matches[0].Groups[1].Value })
            End = ($lines | Select-String "^END" | Select-Object -Last 1)
        }

        $failures = @()
        if ($p.ExitCode -ne 0) { $failures += "harness exit code $($p.ExitCode)" }
        if ($lines -match "^END .*crash") { $failures += ($lines | Select-String "^END .*crash").Line }
        if (-not $r.End) { $failures += "no END line (harness died)" }
        # Every generated base starts with power covering its drain.
        foreach ($m in ($lines | Select-String "(enemy \d .*|player \S+|ally \S+) .*base '([^']+)'.*\(power (\d+)/(\d+)\)")) {
            $pw = [int]$m.Matches[0].Groups[3].Value; $dr = [int]$m.Matches[0].Groups[4].Value
            if ($pw -lt $dr) { $failures += "base '$($m.Matches[0].Groups[2].Value)' starts underpowered ($pw/$dr)" }
        }
        # Every structure objective must get its target placed.
        foreach ($m in ($lines | Select-String "(primary|secondary) objective (\S+) \((Destroy|Capture|Protect)," )) {
            $obj = $m.Matches[0].Groups[2].Value
            if (-not ($lines -match "objective $obj target \S+ at cell")) { $failures += "target for $obj was never placed" }
        }
        $failures += @(& $test.Check $r)
        $failures = @($failures | Where-Object { $_ })

        $state = if ($failures.Count -eq 0) { "PASS" } else { "FAIL" }
        $color = if ($state -eq "PASS") { "Green" } else { "Red" }
        Write-Host ("{0,-4} {1,-22} {2,4}s  waves={3}" -f $state, $label, $seconds, $r.Waves.Count) -ForegroundColor $color
        foreach ($f in $failures) { Write-Host "       - $f" -ForegroundColor $color }
        $results += [pscustomobject]@{ Test = $label; State = $state; Failures = $failures }
    }
}

$failed = @($results | Where-Object { $_.State -eq "FAIL" })
Write-Host ""
Write-Host "$($results.Count - $failed.Count)/$($results.Count) passed. Logs: $Run"
if ($failed.Count -gt 0) { exit 1 }
exit 0
