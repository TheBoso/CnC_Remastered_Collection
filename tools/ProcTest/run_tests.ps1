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
    .\run_tests.ps1 -Balance -Repeat 8  # squad playthroughs only, with win rates (random by nature)
#>
param(
    [string]$GameDir = "C:\Program Files (x86)\Steam\steamapps\common\CnCRemastered",
    [string[]]$Only = @(),
    [int]$Repeat = 1,
    [switch]$NoBuild,
    [switch]$Balance
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
        if ($l -match "status t=(\d+)s threat=(\d+) waves=(\d+)/(\d+) types=(\d+) enemies=\[([^\]]*)\] player=(-?\d+)(?: patrols=\S+ moved=\d+)? objectives=\[([^\]]*)\]") {
            [pscustomobject]@{
                Seconds = [int]$Matches[1]; Threat = [int]$Matches[2]; Waves = [int]$Matches[3]
                Enemies = $Matches[6]; Player = [int]$Matches[7]; Objectives = $Matches[8]
            }
        }
    }
}

# --do results: "TEST <frame> <cmd> <objective> <value> -> <result>"
function Get-Test($r, [string]$Cmd, [string]$Objective = "\S+") {
    foreach ($l in $r.Lines) {
        if ($l -match "^TEST (\d+) $Cmd $Objective -?\d+ -> (-?\d+)") {
            [pscustomobject]@{ Frame = [int]$Matches[1]; Result = [int]$Matches[2] }
        }
    }
}

function Get-Over($r) {
    $m = $r.Lines | Select-String "^OVER (\d+) (\w+)" | Select-Object -First 1
    if ($m) { [pscustomobject]@{ Frame = [int]$m.Matches[0].Groups[1].Value; Result = $m.Matches[0].Groups[2].Value } }
}

# Frame of the first on-screen message matching the pattern, or $null.
function Get-MsgFrame($r, [string]$Pattern) {
    $m = $r.Lines | Select-String "^MSG\s+(\d+) .*$Pattern" | Select-Object -First 1
    if ($m) { [int]$m.Matches[0].Groups[1].Value }
}

# Same distance as Cell_Dist in PROCMISSION.CPP (map is 128 cells wide).
function Get-CellDist([int]$A, [int]$B) {
    $dx = [math]::Abs(($A % 128) - ($B % 128)); $dy = [math]::Abs([math]::Floor($A / 128) - [math]::Floor($B / 128))
    [math]::Max($dx, $dy) + [math]::Floor([math]::Min($dx, $dy) / 2)
}

# Makes the named bonus objectives the only ones picked.
function Set-Bonuses([string]$Dir, [string[]]$Names) {
    Set-IniValue "$Dir\PROCMISSION.INI" "Generator" "SecondaryMin" "$($Names.Count)"
    Set-IniValue "$Dir\PROCMISSION.INI" "Generator" "SecondaryMax" "$($Names.Count)"
    foreach ($n in $Names) { Set-IniValue "$Dir\PROCOBJECTIVES.INI" $n "Weight" "100000" }
}

function Assert-Over($r, [string]$Want, [int]$Before = 0) {
    $over = Get-Over $r
    if (-not $over) { "game did not end (expected $Want)" }
    elseif ($over.Result -ne $Want) { "game ended in '$($over.Result)', expected '$Want'" }
    elseif ($Before -gt 0 -and $over.Frame -gt $Before) { "game ended at frame $($over.Frame), expected before $Before" }
}

function Assert-NoOver($r) {
    $over = Get-Over $r
    if ($over) { "a bonus objective ended the game ($($over.Result) at frame $($over.Frame))" }
}

# The objective's COMPLETE / FAILED message (text is matched loosely).
function Assert-Objective($r, [string]$Text, [string]$State) {
    if ($null -eq (Get-MsgFrame $r "$Text.* - $State")) { "'$Text' was not $State" }
}

# Money gained between the last credits sample before frame $From and the last sample.
function Get-CreditsGain($r, [int]$From) {
    $samples = @(Get-Test $r "credits")
    $before = $samples | Where-Object { $_.Frame -lt $From } | Select-Object -Last 1
    $after = $samples | Select-Object -Last 1
    if ($before -and $after) { $after.Result - $before.Result } else { $null }
}

# Forward Base played by the squad itself: the camp guards are beaten and an engineer takes the yard.
function Test-ForwardBasePlay($r) {
    $f = @()
    $f += Assert-Objective $r "Capture the damaged construction yard" "COMPLETE"
    # The camp must not sit under the enemy main base's turrets.
    $near = Get-Test $r "near" | Select-Object -First 1
    if ($near -and $near.Result -ge 100) { $f += "$([math]::Floor($near.Result / 100)) armed enemy structures within 10 cells of the camp" }
    $over = Get-Over $r
    if ($over) { $f += "game ended ($($over.Result)) at frame $($over.Frame)" }
    $units = @(Get-Test $r "units")
    if ($units.Count -ge 2) { Write-Host "       squad: $($units[0].Result) -> $($units[-1].Result) units" }
    $f
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
            # The idle harness player is often overrun before the 20 minutes are up.
            $over = Get-Over $r
            $minutes = if ($over) { [int]($over.Frame / 900) } else { 20 }
            $want = if ($minutes -ge 19) { 4 } elseif ($minutes -ge 10) { 3 } else { 2 }
            if ($r.Waves.Count -lt $want) { $f += "only $($r.Waves.Count) waves sent in $minutes minutes" }
            $over = @($r.Lines | Select-String "wave PROCW\d+ over after (\d+)s" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
            if ($over.Count -gt 0 -and ($over | Measure-Object -Maximum).Maximum -lt 40) { $f += "every wave disbanded within 40s (waves never engage)" }
            $first = $r.Status | Where-Object { $_.Seconds -eq 60 } | Select-Object -First 1
            $last = $r.Status | Select-Object -Last 1
            if ($first -and $last -and $last.Player -ge $first.Player) { $f += "player base untouched after 20 minutes ($($first.Player) -> $($last.Player) buildings)" }
            $maxthreat = ($r.Status | Measure-Object Threat -Maximum).Maximum
            if ($maxthreat -lt 2 -and $minutes -ge 10) { $f += "threat never escalated in $minutes minutes (max $maxthreat)" }
            $f
        }
    }

    low_tech = @{
        Args = @("--player", "USSR", "--enemies", "Greece", "--tech", "1", "--frames", "10800")
        # A wave mission (squad missions ignore the tech level and may have no waves).
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL" }
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
        Args = @("--player", "England", "--enemies", "USSR", "--tech", "10", "--frames", "1800", "--do", "450+15:damage:PRIMARY:40")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_COMMAND" }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "primary objective DESTROY_COMMAND")) { $f += "forced primary not used" }
            if (-not ($r.Lines -match "objective DESTROY_COMMAND target (STEK|ATEK|DOME) at cell")) { $f += "command center target not placed" }
            if ($r.Lines -match "falling back to destroy all enemies") { $f += "primary fell back" }
            $f += Assert-Objective $r "Destroy the enemy command center" "COMPLETE"
            $f += Assert-Over $r "win"
            $f
        }
    }

    # -------------------------------------------------------------------------
    # Every objective preset played out through the ProcMission_Test hook
    # (--do frame[+every]:command[:objective[:value]], see PROCMISSION.CPP).
    # -------------------------------------------------------------------------

    destroy_all_win = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "3000", "--do", "450+30:wipe:ENEMY")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
        }
        Check = { param($r) Assert-Over $r "win" }
    }

    destroy_all_lose = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "3000", "--do", "450+30:wipe:PLAYER")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
        }
        Check = { param($r) Assert-Over $r "lose" }
    }

    survive_lose = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1800", "--do", "450+30:wipe:PLAYER")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "SURVIVE_SIEGE" "MinMinutes" "3"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "SURVIVE_SIEGE" "MaxMinutes" "3"
        }
        Check = { param($r) Assert-Over $r "lose" }
    }

    capture_lab_win = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1800",
            "--do", "455:clear:PRIMARY:10", "--do", "460:capture:PRIMARY:3")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "CAPTURE_LAB"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
        }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "objective CAPTURE_LAB target (BIO|MISS) at cell")) { $f += "lab not placed" }
            $f += Assert-Objective $r "Capture the research facility" "COMPLETE"
            $f += Assert-Over $r "win"
            $f
        }
    }

    capture_lab_lose = @{
        Args = @("--player", "USSR", "--enemies", "Greece", "--frames", "1800", "--do", "450+15:damage:PRIMARY:50")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "CAPTURE_LAB"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
        }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "Capture the research facility" "FAILED"
            $f += Assert-Over $r "lose"
            $f
        }
    }

    hold_the_line_win = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "2400")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "HOLD_THE_LINE"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "HOLD_THE_LINE" "MinMinutes" "2"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "HOLD_THE_LINE" "MaxMinutes" "2"
        }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "objective HOLD_THE_LINE target DOME at cell")) { $f += "radar dome not placed in the player's base" }
            $f += Assert-Objective $r "Keep your Radar Dome standing" "COMPLETE"
            $f += Assert-Over $r "win"
            $f
        }
    }

    hold_the_line_lose = @{
        Args = @("--player", "USSR", "--enemies", "Greece", "--frames", "1800", "--do", "450+15:damage:PRIMARY:50")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "HOLD_THE_LINE" }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "Keep your Radar Dome standing" "FAILED"
            $f += Assert-Over $r "lose"
            $f
        }
    }

    bonus_destroy_credits = @{
        # Radar, air defense and refinery: 3000 + 2000 + 4000 credits.
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1500", "--do", "300+15:credits",
            "--do", "450+15:damage:BONUS_RADAR:40", "--do", "450+15:damage:BONUS_AIR_DEFENSE:40", "--do", "450+15:damage:BONUS_REFINERY:40")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-Bonuses $d @("BONUS_RADAR", "BONUS_AIR_DEFENSE", "BONUS_REFINERY")
        }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "Knock out the enemy radar" "COMPLETE"
            $f += Assert-Objective $r "Destroy the enemy air defenses" "COMPLETE"
            $f += Assert-Objective $r "destroy their ore refinery" "COMPLETE"
            $gain = Get-CreditsGain $r 450
            if ($null -eq $gain -or $gain -lt 9000) { $f += "credits rose by $gain, expected at least 9000" }
            $f += Assert-NoOver $r
            $f
        }
    }

    bonus_war_factory_allied = @{
        Args = @("--player", "England", "--enemies", "USSR", "--frames", "1200", "--do", "300+15:units", "--do", "450+15:damage:BONUS_WAR_FACTORY_ALLIED:40")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-Bonuses $d @("BONUS_WAR_FACTORY_ALLIED")
        }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "Destroy the enemy War Factory" "COMPLETE"
            if (-not ($r.Lines -match "reward for BONUS_WAR_FACTORY_ALLIED: 3 units")) { $f += "3 reward tanks not delivered" }
            $f += Assert-NoOver $r
            $f
        }
    }

    bonus_war_factory_soviet = @{
        Args = @("--player", "Ukraine", "--enemies", "Greece", "--frames", "1200", "--do", "450+15:damage:BONUS_WAR_FACTORY_SOVIET:40")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-Bonuses $d @("BONUS_WAR_FACTORY_SOVIET")
        }
        Check = { param($r)
            $f = @()
            if ($r.Lines -match "objective BONUS_WAR_FACTORY_ALLIED") { $f += "Allied bonus offered to a Soviet player" }
            $f += Assert-Objective $r "Destroy the enemy War Factory" "COMPLETE"
            if (-not ($r.Lines -match "reward for BONUS_WAR_FACTORY_SOVIET: 3 units")) { $f += "3 reward tanks not delivered" }
            $f
        }
    }

    bonus_time_limit = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1500")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-Bonuses $d @("BONUS_WAR_FACTORY_ALLIED")
            Set-IniValue "$d\PROCOBJECTIVES.INI" "BONUS_WAR_FACTORY_ALLIED" "MinMinutes" "1"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "BONUS_WAR_FACTORY_ALLIED" "MaxMinutes" "1"
        }
        Check = { param($r)
            $f = @()
            $frame = Get-MsgFrame $r "War Factory within 1 minutes.* - FAILED"
            if ($null -eq $frame) { $f += "timed bonus did not fail when its time ran out" }
            elseif ($frame -lt 880 -or $frame -gt 1000) { $f += "timed bonus failed at frame $frame, expected about 945" }
            $f += Assert-NoOver $r
            $f
        }
    }

    bonus_capture_rewards = @{
        # Abandoned refinery (3000 credits) and field hospital (6 infantry).
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1500", "--do", "300+15:credits",
            "--do", "455:clear:BONUS_ABANDONED_REFINERY:10", "--do", "460:capture:BONUS_ABANDONED_REFINERY:3",
            "--do", "455:clear:BONUS_HOSPITAL:10", "--do", "460:capture:BONUS_HOSPITAL:3")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-Bonuses $d @("BONUS_ABANDONED_REFINERY", "BONUS_HOSPITAL")
        }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "Capture the abandoned ore refinery" "COMPLETE"
            $f += Assert-Objective $r "Capture the field hospital" "COMPLETE"
            if (-not ($r.Lines -match "reward for BONUS_HOSPITAL: 6 units")) { $f += "6 reward infantry not delivered" }
            $gain = Get-CreditsGain $r 450
            if ($null -eq $gain -or $gain -lt 3000) { $f += "credits rose by $gain, expected at least 3000" }
            $f
        }
    }

    bonus_protect_win = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1500", "--do", "300+15:credits")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-Bonuses $d @("BONUS_PROTECT_SILOS")
            Set-IniValue "$d\PROCOBJECTIVES.INI" "BONUS_PROTECT_SILOS" "MinMinutes" "1"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "BONUS_PROTECT_SILOS" "MaxMinutes" "1"
        }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "Keep the supply silo intact" "COMPLETE"
            $gain = Get-CreditsGain $r 850
            if ($null -eq $gain -or $gain -lt 5000) { $f += "credits rose by $gain, expected at least 5000" }
            $f
        }
    }

    bonus_protect_fail = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1200", "--do", "450+15:damage:BONUS_PROTECT_SILOS:50")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-Bonuses $d @("BONUS_PROTECT_SILOS")
        }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "Keep the supply silo intact" "FAILED"
            $f += Assert-NoOver $r
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
            $low = @($r.Lines | Select-String "^MSG\s+(\d+) TEXT_LOW_POWER" | Where-Object { [int]$_.Matches[0].Groups[1].Value -lt 900 })
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

    commando_raid = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "4500")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_RAID"
            # Plenty of bonus draws, with capture bonuses heavily weighted, to exercise the engineer filter.
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "SecondaryMin" "3"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "SecondaryMax" "3"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "BONUS_HOSPITAL" "Weight" "100000"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "BONUS_ABANDONED_REFINERY" "Weight" "100000"
        }
        Check = { param($r)
            $f = @()
            $squad = $r.Lines | Select-String "player \S+ starts with squad '([^']+)' \((\d+) units, (-?\d+) credits\)" | Select-Object -First 1
            if (-not $squad) { $f += "no squad start" }
            else {
                if ($squad.Matches[0].Groups[1].Value -ne "ALL_SQUAD_TANYA") { $f += "Allied player got squad '$($squad.Matches[0].Groups[1].Value)'" }
                if ([int]$squad.Matches[0].Groups[2].Value -lt 4) { $f += "squad only has $($squad.Matches[0].Groups[2].Value) units" }
                if ([int]$squad.Matches[0].Groups[3].Value -ne 0) { $f += "squad start credits are $($squad.Matches[0].Groups[3].Value), expected 0" }
            }
            if ($r.Lines -match "player .* starts with base") { $f += "squad mission also handed out a base" }
            if (-not ($r.Lines -match "outpost '[^']+' for COMMANDO_RAID at cell")) { $f += "no outpost built for the raid target" }
            if (-not ($r.Lines -match "enemy production disabled")) { $f += "enemy production not disabled" }
            $patrols = @($r.Lines -match "ProcMission: patrol PROCP").Count
            if ($patrols -ne 3) { $f += "$patrols patrols spawned, expected 3" }
            # Patrols must actually walk their loops, not stand at their first point.
            $walked = @($r.Lines | Select-String "patrols=\d+/\d+ moved=(\d+)" | ForEach-Object { [int]$_.Matches[0].Groups[1].Value } | Measure-Object -Maximum).Maximum
            if (-not $walked -or $walked -lt 2) { $f += "patrols are not moving (max moved=$walked)" }
            if ($r.Waves.Count -gt 0) { $f += "waves sent in a Waves=no mission" }
            $capture = @($r.Lines | Select-String "objective (\S+) \(Capture" | ForEach-Object { $_.Matches[0].Groups[1].Value })
            if ($capture.Count -gt 0) { $f += "capture objective $($capture -join ', ') given to a squad without engineers" }
            if (-not ($r.Lines -match "^MSG .*Slip past the patrols")) { $f += "raid objective not announced" }
            # A squad has no structures: the real client's "no structures left" defeat must not apply.
            $over = Get-Over $r
            if ($over) { $f += "the idle squad's game ended ($($over.Result)) at frame $($over.Frame)" }
            $f
        }
    }

    raid_win = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1500", "--do", "450+15:damage:PRIMARY:40")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_RAID" }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "destroy the enemy command post" "COMPLETE"
            $f += Assert-Over $r "win"
            $f
        }
    }

    raid_lose = @{
        Args = @("--player", "USSR", "--enemies", "England", "--frames", "1500", "--do", "450+30:wipe:PLAYER")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_RAID" }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "starts with squad 'SOV_SQUAD_SABOTEURS'")) { $f += "Soviet player did not get the saboteur squad" }
            $f += Assert-Over $r "lose"
            $f
        }
    }

    raid_reward_units = @{
        # Reward units must arrive next to a squad that has no base (not at the map corner).
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1200", "--do", "450+15:damage:BONUS_WAR_FACTORY_ALLIED:40")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_RAID"
            Set-Bonuses $d @("BONUS_WAR_FACTORY_ALLIED")
        }
        Check = { param($r)
            $f = @()
            $squad = $r.Lines | Select-String "starts with squad .* at cell (\d+)" | Select-Object -First 1
            $reward = $r.Lines | Select-String "reward for BONUS_WAR_FACTORY_ALLIED: (\d+) units at cell (\d+)" | Select-Object -First 1
            if (-not $reward) { $f += "no reward delivered" }
            elseif ([int]$reward.Matches[0].Groups[1].Value -ne 3) { $f += "$($reward.Matches[0].Groups[1].Value) reward tanks delivered, expected 3" }
            elseif ($squad) {
                $dist = Get-CellDist ([int]$squad.Matches[0].Groups[1].Value) ([int]$reward.Matches[0].Groups[2].Value)
                if ($dist -gt 10) { $f += "reward units arrived $dist cells from the squad" }
            }
            $f
        }
    }

    commando_forward_base = @{
        Args = @("--player", "USSR", "--enemies", "Greece", "--frames", "9600", "--do", "300+15:credits", "--do", "450:owner:BONUS_CAPTURE_OUTPOST",
            "--do", "590:clear:BONUS_CAPTURE_OUTPOST:10", "--do", "600:capture:BONUS_CAPTURE_OUTPOST:1", "--do", "700+150:owner:BONUS_CAPTURE_OUTPOST", "--do", "9590:buildings")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_FORWARD_BASE" }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "starts with squad 'SOV_SQUAD_ENGINEERS'")) { $f += "Soviet player did not get the engineer squad" }
            if (-not ($r.Lines -match "secondary objective BONUS_CAPTURE_OUTPOST")) { $f += "forced capture bonus missing" }
            $target = $r.Lines | Select-String "objective BONUS_CAPTURE_OUTPOST target FACT at cell (\d+)" | Select-Object -First 1
            if (-not $target) { $f += "capturable construction yard not placed" }
            elseif ($r.Lines -match "patrol PROCP\d+ .* around cell $($target.Matches[0].Groups[1].Value),") { $f += "a patrol loops around the construction yard to capture" }
            if (-not ($r.Lines -match "outpost '(SOV|ALL)_CAMP' for BONUS_CAPTURE_OUTPOST")) { $f += "the construction yard is not in a camp" }
            # With the full skirmish AI the enemy army found the idle squad within ~3 minutes (seen in
            # the real game). Static enemy bases: only the waves (from 10 minutes) come for it.
            if (-not ($r.Lines -match "enemy production disabled")) { $f += "enemy production not disabled" }
            if ($r.Lines -match "reinforcements \S+ for Tester") { $f += "a squad mission sent the human reinforcements" }
            $over = Get-Over $r
            if ($over -and $over.Frame -lt 8990) { $f += "the squad's game ended ($($over.Result)) at frame $($over.Frame), before the enemy may build" }
            $patrols = @($r.Lines -match "ProcMission: patrol PROCP").Count
            if ($patrols -ne 3) { $f += "$patrols patrols spawned, expected 3" }
            $early = @($r.Lines | Select-String "^DBG\s+(\d+) ProcMission: wave \S+ \(sent" | Where-Object { [int]$_.Matches[0].Groups[1].Value -lt 8990 })
            if ($early.Count -gt 0) { $f += "wave sent at frame $($early[0].Matches[0].Groups[1].Value), before the 10 minute delay" }

            # The squad clears the camp guards (clear) and one engineer takes the damaged yard; it must then hold
            # (the enemy does not repair it first, and no patrol loops through it).
            $f += Assert-Objective $r "Capture the damaged construction yard" "COMPLETE"
            $gain = Get-CreditsGain $r 600
            if ($null -eq $gain -or $gain -lt 3000) { $f += "credits rose by $gain after the capture, expected 3000" }
            $owned = @(Get-Test $r "owner" "BONUS_CAPTURE_OUTPOST")
            # For 3 minutes after the capture. The idle harness player builds nothing, so the enemy AI's
            # own attacks (production is on) can take the base later on.
            $lost = $owned | Where-Object { $_.Frame -ge 700 -and $_.Frame -lt 3300 -and $_.Result -ne 1 } | Select-Object -First 1
            if ($lost) { $f += "captured construction yard lost by frame $($lost.Frame) (owner=$($lost.Result))" }
            $f
        }
    }

    forward_base_win = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "3000", "--do", "440:clear:BONUS_CAPTURE_OUTPOST:10", "--do", "450:capture:BONUS_CAPTURE_OUTPOST:1", "--do", "900+30:wipe:ENEMY")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_FORWARD_BASE" }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "starts with squad 'ALL_SQUAD_ENGINEERS'")) { $f += "Allied player did not get the engineer squad" }
            $f += Assert-Objective $r "Capture the damaged construction yard" "COMPLETE"
            $f += Assert-Over $r "win"
            $f
        }
    }

    # -------------------------------------------------------------------------
    # Playthroughs: the squad's own units do the work, the way a player would
    # (advance on the camp, fight the guards, send an engineer in / attack).
    # -------------------------------------------------------------------------

    forward_base_play_allied = @{
        Balance = $true
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "8800", "--do", "60:units", "--do", "60:near:BONUS_CAPTURE_OUTPOST:10",
            "--do", "300+150:fight:BONUS_CAPTURE_OUTPOST", "--do", "4500+300:engineer:BONUS_CAPTURE_OUTPOST", "--do", "8790:units")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_FORWARD_BASE" }
        Check = { param($r) Test-ForwardBasePlay $r }
    }

    forward_base_play_soviet = @{
        Balance = $true
        Args = @("--player", "USSR", "--enemies", "England", "--frames", "8800", "--do", "60:units", "--do", "60:near:BONUS_CAPTURE_OUTPOST:10",
            "--do", "300+150:fight:BONUS_CAPTURE_OUTPOST", "--do", "4500+300:engineer:BONUS_CAPTURE_OUTPOST", "--do", "8790:units")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_FORWARD_BASE" }
        Check = { param($r) Test-ForwardBasePlay $r }
    }

    raid_play = @{
        Balance = $true
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "9000", "--do", "60:units",
            "--do", "300+150:assault:PRIMARY")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_RAID" }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "destroy the enemy command post" "COMPLETE"
            $f += Assert-Over $r "win"
            $f
        }
    }

    raid_play_soviet = @{
        Balance = $true
        Args = @("--player", "USSR", "--enemies", "England", "--frames", "9000",
            "--do", "300+150:assault:PRIMARY")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_RAID" }
        Check = { param($r)
            $f = @()
            $f += Assert-Objective $r "destroy the enemy command post" "COMPLETE"
            $f += Assert-Over $r "win"
            $f
        }
    }

    forward_base_crowded_map = @{
        # Map 11: the camp used to crowd out its own construction yard (then nothing could be built).
        Args = @("--scenario", "11", "--player", "Greece", "--enemies", "USSR", "--frames", "300")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_FORWARD_BASE" }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "objective BONUS_CAPTURE_OUTPOST target FACT at cell")) { $f += "no construction yard to capture" }
            if (-not ($r.Lines -match "outpost '(SOV|ALL)_CAMP' for BONUS_CAPTURE_OUTPOST")) { $f += "no camp built" }
            $f
        }
    }

    enemy_production_on = @{
        # EnemyProduction=yes hands the enemies back to the full skirmish AI: they build on their own.
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "3600")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "EnemyProduction" "yes"
        }
        Check = { param($r)
            $f = @()
            if ($r.Lines -match "enemy production disabled") { $f += "EnemyProduction=yes still disabled production" }
            # Its count goes up and down (it also loses buildings), so: did it ever have more than it started with?
            $start = $r.Lines | Select-String "enemy 0 \(\w+\) base '\w+', (\d+) buildings" | Select-Object -First 1
            $initial = if ($start) { [int]$start.Matches[0].Groups[1].Value } else { 0 }
            $most = ($r.Status | ForEach-Object { [int]($_.Enemies -replace '\D.*$', '') } | Measure-Object -Maximum).Maximum
            if ($most -le $initial) { $f += "enemy did not build (started with $initial buildings, never had more than $most)" }
            $f
        }
    }

    enemy_production_off = @{
        # Default: enemies keep their generated base and never build or expand.
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "3600")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL" }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "enemy production disabled")) { $f += "enemy production not disabled by default" }
            # Compared with the first status line: objective buildings and their power plants are
            # added after the base itself is logged.
            $first = $r.Status | Select-Object -First 1
            $grew = $r.Status | Where-Object { $first -and [int]($_.Enemies -replace '\D.*$', '') -gt [int]($first.Enemies -replace '\D.*$', '') }
            if ($grew) { $f += "enemy base grew from $($first.Enemies) to $($grew[0].Enemies) buildings" }
            $f
        }
    }

    objectives_revealed = @{
        # Every objective target is revealed when the objectives are announced (frame ~45).
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "120", "--do", "100:revealed:PRIMARY",
            "--do", "100:revealed:BONUS_HOSPITAL", "--do", "100:revealed:BONUS_RADAR")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "CAPTURE_LAB"
            Set-Bonuses $d @("BONUS_HOSPITAL", "BONUS_RADAR")
        }
        Check = { param($r)
            $f = @()
            foreach ($t in (Get-Test $r "revealed")) { if ($t.Result -ne 1) { $f += "an objective target is still shrouded (result $($t.Result))" } }
            if (@(Get-Test $r "revealed").Count -ne 3) { $f += "reveal checks did not run" }
            $f
        }
    }

    naval_waves = @{
        # Map 3: the enemy base gets a shipyard on water that reaches the player's base.
        Args = @("--scenario", "3", "--player", "England", "--enemies", "Greece", "--frames", "6000", "--do", "20:naval", "--do", "20:buildings", "--do", "5990:buildings",
            "--do", "20:health", "--do", "5990:health")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "NavalChance" "100"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "FirstWaveSeconds" "30"
        }
        Check = { param($r)
            $f = @()
            # The bases are placed at random: the yard may end up on another stretch of water, or
            # none of the player's buildings in gunboat range (then the enemy rightly attacks by land).
            $reach = Get-Test $r "naval" | Select-Object -First 1
            $inrange = $r.Lines | Select-String "buildings in range 5/9/20: (\d+)" | Select-Object -First 1
            $shellable = $inrange -and [int]$inrange.Matches[0].Groups[1].Value -gt 0
            if (-not $reach -or $reach.Result -lt 1 -or -not $shellable) {
                Write-Host "       (naval_waves: no yard or no building in gunboat range on this layout, nothing to check)"
            }
            $sea = @($r.Lines | Select-String "wave \S+ \(sent, threat \d+, by sea\)").Count
            if ($sea -lt 1 -and $reach -and $reach.Result -ge 1 -and $shellable) { $f += "no naval wave sent" }
            # Gunboats rarely finish a building off in time, so also count damage (the human never repairs).
            $b = @(Get-Test $r "buildings")
            $h = @(Get-Test $r "health")
            if ($sea -ge 1 -and $b.Count -eq 2 -and $b[1].Result -ge $b[0].Result -and $h.Count -eq 2 -and $h[1].Result -ge $h[0].Result) {
                $f += "naval waves did no damage ($($b[0].Result) -> $($b[1].Result) player buildings, $($h[0].Result)% -> $($h[1].Result)% health)"
            }
            $f
        }
    }

    reinforcements = @{
        # Every minute: the human, the AI ally and the enemy each get a team from their map edge.
        Args = @("--player", "Greece", "--allies", "England", "--enemies", "USSR", "--frames", "2000", "--do", "800:units", "--do", "1990:units")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "PlayerReinforceMinutes" "1"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "AllyReinforceMinutes" "1"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "EnemyReinforceMinutes" "1"
        }
        Check = { param($r)
            $f = @()
            $sent = @($r.Lines | Select-String "reinforcements \S+ for \S+ \(sent")
            $failed = @($r.Lines | Select-String "reinforcements \S+ for \S+ \(failed\)")
            if ($failed.Count -gt 0) { $f += "$($failed.Count) reinforcements failed: $($failed[0].Line -replace '^DBG\s+\d+ ProcMission: ', '')" }
            if (-not ($r.Lines -match "reinforcements (ALL|SOV)_REINF_\w+ for Tester \(sent")) { $f += "no reinforcements for the human" }
            if ($sent.Count -lt 6) { $f += "only $($sent.Count) reinforcements in 2 minutes, expected 2 each for 3 houses" }
            if (-not ($r.Lines -match "^MSG .*Reinforcements have arrived")) { $f += "the human was not told reinforcements arrived" }
            $units = @(Get-Test $r "units")
            if ($units.Count -eq 2 -and $units[1].Result -le $units[0].Result) { $f += "the human's units did not grow ($($units[0].Result) -> $($units[1].Result))" }
            $f
        }
    }

    spectate = @{
        # Spectate=yes: the human owns nothing, sees the whole map and is left alone; the AI
        # ally and the enemy (full skirmish AIs) fight it out with the director's help.
        Args = @("--player", "Greece", "--allies", "England", "--enemies", "USSR", "--frames", "4000",
            "--do", "100:units", "--do", "100:buildings", "--do", "1000:mapped", "--do", "3990:units")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Spectate" "yes"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_RAID"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "FirstWaveSeconds" "30"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "PlayerReinforceMinutes" "1"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "AllyReinforceMinutes" "1"
        }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "ProcMission: spectator mode")) { $f += "spectator mode not on" }
            if ($r.Lines -match "COMMANDO_RAID|secondary objective") { $f += "spectating kept the configured primary / bonus objectives" }
            if ($r.Lines -match "enemy production disabled") { $f += "enemy production was disabled while spectating" }
            foreach ($t in @(Get-Test $r "units") + @(Get-Test $r "buildings")) {
                if ($t.Result -ne 0) { $f += "the spectator owns $($t.Result) objects at frame $($t.Frame)" }
            }
            $mapped = Get-Test $r "mapped" | Select-Object -First 1
            if (-not $mapped -or $mapped.Result -lt 95) { $f += "only $($mapped.Result)% of the map revealed" }
            if ($r.Lines -match "wave .* -> Tester") { $f += "a wave was sent at the spectator" }
            if (-not ($r.Lines -match "wave \S+ \(sent")) { $f += "no waves sent at the AI ally" }
            if ($r.Lines -match "reinforcements \S+ for Tester") { $f += "the spectator got reinforcements" }
            if (-not ($r.Lines -match "reinforcements \S+ for \S+ \(sent")) { $f += "the AI ally got no reinforcements" }
            $over = Get-Over $r
            if ($over) { $f += "the game ended ($($over.Result) at frame $($over.Frame))" }
            $f
        }
    }

    spectate_win = @{
        Args = @("--player", "Greece", "--allies", "England", "--enemies", "USSR", "--frames", "3000", "--do", "450+30:wipe:ENEMY")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Spectate" "yes" }
        Check = { param($r) Assert-Over $r "win" }
    }

    spectate_lose = @{
        # Once every AI ally is gone the spectator's team has lost.
        Args = @("--player", "Greece", "--allies", "England", "--enemies", "USSR", "--frames", "3000", "--do", "450+30:wipe:ALLY")
        Patch = { param($d) Set-IniValue "$d\PROCMISSION.INI" "Generator" "Spectate" "yes" }
        Check = { param($r) Assert-Over $r "lose" }
    }

    reinforcements_water = @{
        # Map 4: the map edges near the bases are water. Infantry come by Chinook (Allied) or
        # Badger paradrop (Soviet), vehicles by landing ship; nobody is left stranded off the map.
        Args = @("--scenario", "4", "--player", "Greece", "--allies", "England", "--enemies", "USSR", "--frames", "2800",
            "--do", "850:athome", "--do", "2780:athome", "--do", "2780:stranded")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "AllyStart" "Base"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "StartThreat" "Medium"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "PlayerReinforceMinutes" "1"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "AllyReinforceMinutes" "1"
            Set-IniValue "$d\PROCMISSION.INI" "Normal" "EnemyReinforceMinutes" "1"
        }
        Check = { param($r)
            $f = @()
            $sent = @($r.Lines | Select-String "reinforcements \S+ for \S+ \(sent, (by Chinook|by landing ship|by paradrop)")
            if ($sent.Count -lt 6) { $f += "only $($sent.Count) reinforcements came by transport (expected 2 each for 3 houses)" }
            if (-not ($r.Lines -match "for Tester \(sent, by (Chinook|landing ship)")) { $f += "the human got no Chinook / landing ship" }
            if (-not ($r.Lines -match "for Computer \(sent, by (paradrop|landing ship)")) { $f += "the Soviet enemy got no paradrop / landing ship" }
            $failed = @($r.Lines | Select-String "reinforcements \S+ for \S+ \(failed")
            if ($failed.Count -gt 0) { $f += "$($failed.Count) reinforcements failed" }
            $stranded = Get-Test $r "stranded" | Select-Object -Last 1
            if ($stranded -and $stranded.Result -gt 0) { $f += "$($stranded.Result) units stranded off the map or on water" }
            $athome = @(Get-Test $r "athome")
            if ($athome.Count -eq 2 -and $athome[1].Result -le $athome[0].Result) { $f += "no reinforcements reached the human base ($($athome[0].Result) -> $($athome[1].Result) units)" }
            $f
        }
    }

    survive_pressure = @{
        # Survival missions: waves 1.5x as often, one more wave at a time, and enemy
        # reinforcements every 4 minutes that hunt the player's team instead of guarding.
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "7300")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "SURVIVE_SIEGE" "MinMinutes" "30"
            Set-IniValue "$d\PROCOBJECTIVES.INI" "SURVIVE_SIEGE" "MaxMinutes" "30"
        }
        Check = { param($r)
            $f = @()
            $hunting = @($r.Lines | Select-String "reinforcements \S+ for Computer \(sent.*hunting\)")
            if ($hunting.Count -lt 1) { $f += "no hunting enemy reinforcements in 8 minutes" }
            if ($r.Lines -match "reinforcements \S+ for Computer \(sent(?!.*hunting)") { $f += "an enemy reinforcement did not hunt" }
            if (-not ($r.Lines -match "status t=\d+s threat=\d+ waves=\d+/3 ")) { $f += "MaxActiveWaves not raised to 3 (Normal 2 + ExtraWaves 1)" }
            $f
        }
    }

    hard_superweapons = @{
        # Hard: enemy main bases get the superweapons their side can own; the AI nukes by
        # itself, the director fires the Iron Curtain (on a wave tank) and the Chronosphere.
        Args = @("--player", "Greece", "--enemies", "USSR,France", "--frames", "15000")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Difficulty" "Hard"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "OutpostChance" "0"
            # Weak waves, so the player lives long enough for every superweapon to charge.
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "WaveSize" "10"
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "MaxThreat" "Light"
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "FirstWaveSeconds" "60"
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "BaseStartWaveRate" "100"
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "BaseStartWaveSize" "100"
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "BaseStartExtraWaves" "0"
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "BaseStartFirstWave" "100"
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "BaseStartThreat" "0"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Sentries" "no"
        }
        Check = { param($r)
            $f = @()
            $sov = $r.Lines | Select-String "enemy \d+ \(Soviet\) base" | Select-Object -First 1
            $all = $r.Lines | Select-String "enemy \d+ \(Allied\) base" | Select-Object -First 1
            if (-not ($sov -and $sov.Line -match "MSLO=1" -and $sov.Line -match "IRON=1")) { $f += "Soviet base lacks a missile silo / Iron Curtain: $($sov.Line)" }
            if (-not ($all -and $all.Line -match "PDOX=1")) { $f += "Allied base lacks a Chronosphere: $($all.Line)" }
            if (-not ($r.Lines -match "launched a nuke")) { $f += "no nuke launched" }
            if (-not ($r.Lines -match "Iron Curtain on")) { $f += "Iron Curtain never used" }
            if (-not ($r.Lines -match "Chronosphere sends")) { $f += "Chronosphere never used" }
            $over = Get-Over $r
            if ($over -and $over.Frame -lt 12000) { $f += "game ended at frame $($over.Frame) ($($over.Result)), before the superweapons charged" }
            $f
        }
    }

    hard_factories = @{
        # FactoryScale multiplies an enemy base's factories and refineries; difficulty weights
        # choose the preset (only SOV_MAIN_ARMOR is allowed on Hard here).
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "60")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Difficulty" "Hard"
            Set-IniValue "$d\PROCMISSION.INI" "Hard" "FactoryScale" "200"
            foreach ($b in @("SOV_MAIN_STANDARD", "SOV_MAIN_FORTRESS", "SOV_MAIN_AIRBASE", "SOV_MAIN_TESLA", "SOV_MAIN_GARRISON")) {
                Set-IniValue "$d\PROCBASES.INI" $b "WeightHard" "0"
            }
        }
        Check = { param($r)
            $f = @()
            $line = $r.Lines | Select-String "enemy 0 \(Soviet\) base" | Select-Object -First 1
            if (-not ($line -and $line.Line -match "base 'SOV_MAIN_ARMOR'")) { $f += "WeightHard=0 presets were picked: $($line.Line)" }
            # The preset has WEAP:2, PROC:2, BARR:1: doubled.
            if (-not ($line -and $line.Line -match "WEAP=4" -and $line.Line -match "BARR=2")) { $f += "factories not doubled: $($line.Line)" }
            $f
        }
    }

    bonus_reward_text = @{
        # Bonus objectives say what they are worth, and say so again when it is received;
        # RewardReveal reveals the whole map.
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "1500", "--do", "440:mapped",
            "--do", "450:damage:BONUS_TECH_CENTER_ALLIED:100",
            "--do", "455:clear:BONUS_CAPTURE_LAB:10", "--do", "460:capture:BONUS_CAPTURE_LAB:3", "--do", "1400:mapped")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-Bonuses $d @("BONUS_TECH_CENTER_ALLIED", "BONUS_CAPTURE_LAB")
        }
        Check = { param($r)
            $f = @()
            if ($null -eq (Get-MsgFrame $r "Bonus: Destroy the enemy tech center\. Reward: 2 Medium Tanks and 2 Artillery\.")) { $f += "tech center bonus does not show its reward" }
            if ($null -eq (Get-MsgFrame $r "Bonus: Capture the research facility .* Reward: \`$1500 and the whole map revealed\.")) { $f += "lab bonus does not show its reward" }
            if ($null -eq (Get-MsgFrame $r "Reward received: 2 Medium Tanks and 2 Artillery\.")) { $f += "no 'reward received' message for the tech center" }
            $f += Assert-Objective $r "Capture the research facility" "COMPLETE"
            $mapped = @(Get-Test $r "mapped")
            if ($mapped.Count -eq 2 -and ($mapped[0].Result -ge 95 -or $mapped[1].Result -lt 95)) { $f += "map not revealed by the reward ($($mapped[0].Result)% -> $($mapped[1].Result)%)" }
            $f
        }
    }

    base_start_pressure = @{
        # A team that starts with a base gets harder waves (Normal: 1.3x as often, 125% size,
        # +1 wave at a time, one threat step up, first wave at 70% of the quiet period).
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "60")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
        }
        Check = { param($r)
            $f = @()
            if (-not ($r.Lines -match "base start: wave rate 130%, wave size 125%, max waves 3, start threat 1, first wave 168s")) {
                $f += "no base-start boost: $(($r.Lines -match 'base start') -join ' | ')"
            }
            $f
        }
    }

    base_start_forced = @{
        # No boost for objectives that hand out the base themselves: they have their own keys.
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "60")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "SURVIVE_SIEGE"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
        }
        Check = { param($r)
            $f = @()
            if ($r.Lines -match "base start:") { $f += "a forced base (SURVIVE_SIEGE) got the base-start boost" }
            $f
        }
    }

    base_start_mcv = @{
        # An MCV start keeps the plain difficulty table.
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "60")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "MCV"
        }
        Check = { param($r)
            $f = @()
            if ($r.Lines -match "base start:") { $f += "an MCV start got the base-start boost" }
            $f
        }
    }

    sentries = @{
        # Small enemy posts across the map (Normal: 2-4), away from the player's team, each
        # with at least one building, held by the enemy; none in squad missions.
        Args = @("--player", "Greece", "--enemies", "USSR,France", "--frames", "60")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "PlayerStart" "Base"
        }
        Check = { param($r)
            $f = @()
            $summary = $r.Lines | Select-String "ProcMission: (\d+)/(\d+) sentries placed" | Select-Object -First 1
            if (-not $summary) { $f += "no sentry summary"; return $f }
            $placed = [int]$summary.Matches[0].Groups[1].Value
            if ($placed -lt 2) { $f += "only $placed sentries placed: $($summary.Line)" }
            foreach ($m in @($r.Lines | Select-String "sentry '(\w+)' \(enemy \d, (\w+)\) at cell \d+, (\d+) buildings, (\d+) cells from the player team")) {
                $g = $m.Matches[0].Groups
                if ([int]$g[3].Value -lt 1) { $f += "sentry $($g[1].Value) has no buildings" }
                if ([int]$g[4].Value -lt 13) { $f += "sentry $($g[1].Value) only $($g[4].Value) cells from the player" }
                if (($g[1].Value -like "SOV_*") -ne ($g[2].Value -eq "Soviet")) { $f += "sentry $($g[1].Value) held by a $($g[2].Value) enemy" }
            }
            $f
        }
    }

    base_minimums = @{
        # Enemy main bases get the difficulty's minimum construction yards and refineries
        # (Hard: 3 and 3), so taking out one building doesn't cripple them.
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "60")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "DESTROY_ALL"
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Difficulty" "Hard"
        }
        Check = { param($r)
            $f = @()
            $line = $r.Lines | Select-String "enemy 0 \(Soviet\) base" | Select-Object -First 1
            if (-not ($line -and $line.Line -match " FACT=3")) { $f += "not 3 construction yards: $($line.Line)" }
            if (-not ($line -and $line.Line -match " PROC=(\d+)" -and [int]$Matches[1] -ge 3)) { $f += "fewer than 3 refineries: $($line.Line)" }
            $f
        }
    }

    sentries_not_in_squads = @{
        Args = @("--player", "Greece", "--enemies", "USSR", "--frames", "60")
        Patch = { param($d)
            Set-IniValue "$d\PROCMISSION.INI" "Generator" "Primary" "COMMANDO_RAID"
        }
        Check = { param($r)
            $f = @()
            if ($r.Lines -match "sentry '") { $f += "a squad mission got sentries" }
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
    # Playthroughs (the squad's own units fight it out) are random by nature: only with -Balance.
    if ([bool]$test.Balance -ne [bool]$Balance -and $Only.Count -eq 0) { continue }

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
        # A capture target a player couldn't order an engineer into (the UI's What_Action check).
        foreach ($m in ($lines | Select-String "ProcTest: an engineer can't be ordered")) { $failures += ($m.Line -replace '^DBG\s+\d+ ProcTest: ', '') }
        # Preset data errors (unknown types, uncapturable capture targets, ...).
        foreach ($m in ($lines | Select-String "ProcINI: \[")) { $failures += "preset warning: $($m.Line -replace '^DBG\s+\d+ ', '')" }
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
        # ...inside its area (base, camp, player base), not pushed out into the open.
        foreach ($m in ($lines | Select-String "objective (\S+) target \S+ at cell \d+ \((\d+) cells from the area centre, radius (\d+)[^)]*\)")) {
            $out = [int]$m.Matches[0].Groups[2].Value; $radius = [int]$m.Matches[0].Groups[3].Value
            # Cell_Dist counts a diagonal as 1.5 cells; the logged cell is the building's centre (+2)
            if ($out -gt [math]::Ceiling(($radius + 2) * 1.5)) { $failures += "target for $($m.Matches[0].Groups[1].Value) placed $out cells out (radius $radius)" }
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

if ($Balance) {
    Write-Host ""
    $results | Group-Object { $_.Test -replace '#\d+$', '' } | ForEach-Object {
        $wins = @($_.Group | Where-Object { $_.State -eq "PASS" }).Count
        Write-Host ("{0,-28} {1}/{2} won" -f $_.Name, $wins, $_.Count)
    }
}
$failed = @($results | Where-Object { $_.State -eq "FAIL" })
Write-Host ""
Write-Host "$($results.Count - $failed.Count)/$($results.Count) passed. Logs: $Run"
if ($failed.Count -gt 0) { exit 1 }
exit 0
