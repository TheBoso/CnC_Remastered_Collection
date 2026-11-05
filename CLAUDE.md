# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Command & Conquer Remastered Collection - GPL v3 licensed source code from Electronic Arts. Contains two game DLLs (Red Alert and Tiberian Dawn) plus a Map Editor. The DLLs export a C interface consumed by the GlyphX engine (not included).

## Build Commands

**Red Alert DLL (Win32):**
```bash
MSBuild "REDALERT\RedAlert.vcxproj" -p:Configuration=Release -p:Platform=Win32
```

**Tiberian Dawn DLL (Win32):**
```bash
MSBuild "TIBERIANDAWN\TiberianDawn.vcxproj" -p:Configuration=Release -p:Platform=Win32
```

**Map Editor (x64 only):**
```bash
MSBuild "CnCTDRAMapEditor\CnCTDRAMapEditor.csproj" -p:Configuration=Release -p:Platform=x64
```

**Full Solution:**
```bash
MSBuild "CnCRemastered.sln" -p:Configuration=Release -p:Platform=Win32
```

Output goes to `bin/Win32/` or `bin/x64/`. Debug builds produce `RedAlertI.dll`/`TiberianDawnI.dll`.

## Architecture

### DLL Interface Pattern
The games export a C interface via `DLLInterface.h` for the GlyphX engine. Key structures use `#pragma pack(1)` for binary compatibility. Interface version tracked in `DLLInterfaceVersion.h` (currently 0x102).

Key interface concepts:
- **GameStateRequestEnum**: Query map data, sidebar, shroud, player info
- **InputRequestEnum**: Mouse/keyboard input handling
- **CNCObjectStruct**: Universal game object representation
- **EventCallbackStruct**: Events back to engine (sound, speech, game over)

### Code Organization
Both REDALERT and TIBERIANDAWN share similar structure:
- Object classes: `AIRCRAFT.CPP`, `BUILDING.CPP`, `INFANTRY.CPP`, `UNIT.CPP`
- Map/terrain: `CELL.CPP`, `MAP.CPP`, `TERRAIN.CPP`
- AI systems: `HOUSE.CPP`, `TEAM.CPP`, `TRIGGER.CPP`
- Game state: `SCENARIO.CPP`, `SESSION.CPP`
- DLL exports: `DLLInterface.cpp`, `DLLInterfaceEditor.cpp`

### House Enums
Allied: `HOUSE_SPAIN`, `HOUSE_GREECE`, `HOUSE_ENGLAND`, `HOUSE_GERMANY`, `HOUSE_FRANCE`, `HOUSE_TURKEY`
Soviet: `HOUSE_USSR`, `HOUSE_UKRAINE`
Note: There is no `HOUSE_UK` - use `HOUSE_ENGLAND`.

### Coordinate System
- `CELL`: Map cell index (single integer)
- `COORD`: 24-bit coordinates (leptons)
- `fixed`: Fixed-point math class in `COORDA.h`

## Procedural Mission System (Feature Branch)

Skirmish-only (`Session.Type == GAME_GLYPHX_MULTIPLAYER`). Enable with `Procedural=yes` in a skirmish map's `[Basic]` section, or `AllMaps=yes` in `[Generator]` (single-human games only; `Procedural=no` opts a map out). The lobby supplies the options:
- Player country = side; AI on the player's lobby team = allies; AI on other teams = enemies (each gets a pre-built base)
- Lobby tech level is respected; `TechLevel=` (1-10, lobby scale) overrides it
- The lobby has no difficulty, so difficulty comes from `[Generator]` / the map's `[Procedural]` section
- The player's team can start with an MCV or a pre-built base (`PlayerStart` / `AllyStart` / `PlayerBaseChance`, Role=Defend presets)
- Enemy AIs are skirmish AIs at full IQ, which would build, expand and attack on their own. By default (`EnemyProduction=no`) their IQ is capped below `Rule.IQProduction`: they only hold their generated base (guard, repair, harvest) and the director's waves are the only attacks, so presets / difficulty tables control the difficulty. The engine's campaign-style base rebuilding (`Base` nodes) only runs in `GAME_NORMAL`, so it isn't available here.
- Squad missions: a primary with `PlayerStart=Squad` gives the humans a Role=Squad team instead (no MCV/base), can disable waves, spawns Role=Patrol teams looping through generated waypoints (60-97; `PatrolThreat=` caps them, they never loop around capture targets or through the player's start), and can force a `Bonus=` objective: the squad's foothold, placed as close to the squad as is safe (e.g. an `Abandoned=yes` construction yard in an `OutpostBase=CAMP`, held at `TargetStrength=20` so one engineer captures it; fully repaired once taken). Other squad targets go far from the start. Enemies never repair a squad mission's objective targets. Capture objectives are only offered to squads with an engineer.
- Naval waves: a team with ships is naval (`IsNaval`); an enemy with a shipyard / sub pen sends one `NavalChance` % of the time, from the yard, when one of the victim's buildings is within the ships' gun range of water in the yard's `MZONE_WATER` zone (`Shell_Targets`), or, for `NavalTarget=Ships` teams (subs), when the victim has ships. Coastal naval waves get `TMISSION_ATTACKTARCOM` orders for those buildings, then Hunt: left to `Greatest_Threat` they lock onto a building far inland and sit offshore. Naval teams skip the land rally points.
- Spectator mode (`Spectate=yes` in `[Generator]`, or `deploy_procmod.ps1 -Spectate`): needs an AI on the human's lobby team. The human gets no base / MCV / units, the whole map is revealed at the announcement, waves and reinforcements go to the AI allies only, enemy production is forced on (both sides run the full skirmish AI), and the primary is always DESTROY_ALL with no bonuses. `ProcMission.Is_Spectator` exempts the human from both engine defeat checks (`Check_Pertinent_Structures` and the "no objects left" check in `HouseClass::AI`); the team loses when every AI ally is defeated (`Update_Objectives`).
- Reinforcements: `Role=Reinforce` teams every `Player/Ally/EnemyReinforceMinutes` (difficulty tables; `Reinforcements=` in `[Generator]`). None for the humans in squad missions, none for enemies without waves. Their TeamTypes are collected like wave types. Delivery (`Send_Reinforcements`): drive in from the nearest playable-border cell in the base's `MZONE_NORMAL` zone (`Find_Edge_Cell`, used as the `Do_Reinforcements` origin waypoint: the engine only scans the edge nearest the origin and otherwise "succeeds" with units stuck off the map); else vehicles by landing ship (`VESSEL_TRANSPORT`) to a beach (`Find_Beach`) from a water edge; else infantry by Chinook (Allied, team move + unload) or Badger paradrop (Soviet, `Paradrop`: cargo attached by hand, no team, dropped on dry land); else dropped in next to the base. A vehicle team with no land/sea route is swapped for an infantry team.
- Survival pressure (objective keys, used by SURVIVE_SIEGE / HOLD_THE_LINE): `WaveRate=` (% of the wave rate), `ExtraWaves=` (+MaxActiveWaves), `EnemyReinforceMinutes=`, `HuntingReinforcements=yes` (enemy reinforcements Hunt; Soviet paratroopers drop outside the victim's base).
- Difficulty and bases: `FactoryScale=` scales enemy factories / refineries (`Is_Production`; fraction = chance of one more, never below one); base presets have `WeightEasy/Normal/Hard` (`DiffWeight`, `BaseWeight` functor in `Weighted_Pick`); `Superweapons=` (Hard: MSLO,IRON,PDOX) adds superweapons to enemy main bases (side ownership filters them). The AI nukes by itself (logged from the recharge restarting); `Superweapon_AI` fires the Iron Curtain (wave vehicle, else the toughest) and the Chronosphere (toughest tank next to the victim's base) since the skirmish AI never does. Barracks: `TENT` is Allied, `BARR` Soviet.
- Base redundancy: enemy main bases get at least `MinConstructionYards` (spares inserted right after the first FACT entry so crowded bases still fit them) and `MinRefineries` (topped up after the build, one extra harvester each). Easy 1/1, Normal 2/2, Hard 3/3.
- Base starts: when the humans start with a pre-built base that the primary didn't force (`IsPlayerBase && !IsBaseForced`), the difficulty's `BaseStartWaveRate / BaseStartWaveSize / BaseStartFirstWave / BaseStartThreat / BaseStartExtraWaves` make the waves harder (applied to `Diff` / `WaveRatePct` / `ExtraWaves` in `Generate`, logged "base start: ..."). Survival / protect objectives that force the base keep their own keys.
- Sentries: `Place_Sentries` scatters `SentriesMin..SentriesMax` (difficulty tables; `Sentries=no` in `[Generator]` turns them off) `Role=Sentry` base presets across the map after the objective targets, each held by the nearest enemy (`Build_Base` with no construction yard). `Find_Sentry_Location`: buildable, in the player's `MZONE_NORMAL` zone, 22+ cells from the player's homes, clear of every base, spread out from the targets and each other. Not in squad missions.
- Unit objectives (`Place_Unit_Objective`, `Update_Assassinate/Rescue/Control`; no target structure, so `Live_Target` is skipped for them):
  - `Kind=Assassinate` (ASSASSINATE_MARSHAL, BONUS_ASSASSINATE): `TargetUnit=` spawned in a camp (`Owner=Outpost`, default) or an enemy base; `obj.Target` is the unit. From a camp it runs to its owner's main base (`Retreat`) once hurt below 75% or the player's units are within `PROC_FLEE_RADIUS`.
  - `Kind=Rescue` (RESCUE_PRISONERS, RESCUE_SQUAD, BONUS_RESCUE): `Prisoners=` spawned neutral and asleep in a camp (turret-free CAMP outposts: turrets kill freed prisoners at once); a human's unit within `PROC_FREE_RADIUS` swaps each for a copy that human owns; within `PROC_EXTRACT_RADIUS` of `Home_Cell(Commander)` they're removed as rescued. `RescueNeeded=`; fails once too few can still make it.
  - `Kind=Control` (HOLD_THE_HILL, BONUS_CONTROL): `Points=` cells from `Find_Sentry_Location`, each with enemy guards and a neutral marker from `Targets=`. A point flips to whoever is alone within `PROC_POINT_RADIUS` (civilians don't count). `MinMinutes` is the hold time, not a time limit: `HeldTicks` only grows while every point is held, and the primary's sidebar timer shows only then. 70% of land waves go to a held point (`Contested_Point`, MOVECELL + Do Guard Area).
  - Test hooks: `kill`, `hurt`, `fleeing`, `approach`, `free`, `escort`, `deliver`, `prisoners`, `occupy`, `contest`, `points`.
- Bonus objectives show their reward ("Reward: $3000 and 2 Medium Tanks", `Reward_Text`; `Unit_Name` has the display names, the DLL can't reach the client's), announce "Reward received", can `RewardReveal=yes` (whole map), or override with `RewardText=`. Base targets reuse an existing building of any allowed type (`Find_Existing_Any`); a bonus whose target can't be placed is swapped for another bonus.
- The real client's skirmish lobby uses the "no structures left = defeated" rule (`Special.IsEarlyWin`, `HouseClass::Check_Pertinent_Structures`); squad players are exempt (`ProcMission.Is_Squad_Player`), they lose when the whole squad is gone.
- Camps / outposts / neutral targets keep `PROC_NEUTRAL_CLEARANCE` (20) cells from every enemy building, and in procedural missions AI units only rush to defend buildings within `PROC_RESCUE_RANGE` (12) cells (`TechnoClass::Base_Is_Attacked`), so a remote camp doesn't pull in the main base's garrison.

Code (REDALERT):
- `PROCMISSION.H/CPP`: `ProcMissionManager` (generation, objectives, wave director). `Generate()` runs at the end of `Read_Scenario_INI`; `AI()` runs from `LogicClass::AI`.
- `PROCINI.H/CPP`: `ProcPresetLibrary` loader and weighted preset picking

Preset library (data, loaded at mission start via CCFileClass):
- `PROCMISSION.INI`: `[Generator]` settings + `[Easy]/[Normal]/[Hard]` tables (also the format docs)
- `PROCTEAMS.INI`, `PROCBASES.INI`, `PROCOBJECTIVES.INI`: presets, each listed in an index section (`[Teams]`, `[Bases]`, `[Objectives]`; up to 80 / 64 / 64). Editing them needs no rebuild.

Testing (headless, no game UI needed):
- `tools\ProcTest\run_tests.ps1` builds the DLL and `tools\ProcTest\ProcTest.exe` (a minimal GlyphX host), then plays skirmish games through the real DLL and checks the `ProcMission:` debug lines. `-Only <test>`, `-Repeat <n>`, `-NoBuild`. Logs in `tools\ProcTest\run\`.
- `-Balance` runs the squad playthroughs instead: the squad's own units fight it out the way a player would (`fight` / `assault` / `engineer` hook commands) and the run reports win rates (random by nature, so not part of the regular suite). Last measured on Normal: Forward Base 12/12, Tanya raid 5/6, Soviet raid 4/6.
- `tools\ProcTest\sweep_maps.ps1` generates every primary on all 24 skirmish maps and flags crashes, unplaced / out-of-area targets and underpowered bases.
- The harness defaults to the real client's early-win rule (`--no-early-win` turns it off).
- Every objective preset has a test that plays it out: `ProcTest --do frame[+every]:command[:objective[:value]]` calls the test-only export `ProcMission_Test` (end of PROCMISSION.CPP: damage / capture a target with spawned engineers, wipe a side, read credits / units / ownership). The engine never calls it.
- `tools\deploy_procmod.ps1` builds and copies the DLL + presets into the "Procedural Missions" mod (`Documents\CnCRemastered\Mods\Red_Alert\ProceduralMissions\Data`), with `AllMaps=yes`. Close the game first (it locks the DLL).
- In the real client the mod's Data folder is NOT on the CCFileClass search path; files next to `RedAlert.dll` must be opened by full path (see `Load_INI` in PROCINI.CPP). Enable the mod in Options > Mods (client restarts). DLL debug output goes to `<game>\log\CnCDLL_Log_16000.txt`.

Notes:
- Win/lose go through `MPlayer_Defeated()` (TACTION_WIN/LOSE only work in GAME_NORMAL)
- Wave TeamTypes are per-wave and garbage collected (TEAMTYPE_MAX is 60)
- Uses its own `Elapsed` frame counter; the DLL start path never resets `Frame`
- Not saved/loaded (skirmish has no save)

## Key Constraints

- Struct packing: 1-byte alignment for all interface structs
- Exception handling: Disabled in C++ projects
- Multi-byte character set (not Unicode)
- Visual Studio 2017 toolset (v141) with MASM support
- x64 builds may have ASM compatibility issues with ml64.exe

## Debug Output

Use `GlyphX_Debug_Print(const char*)` for debug logging that appears in the remaster's debug console.

## License Notes

GPL v3 with EA additional terms. Cannot use "Command & Conquer" trademarks. Modified versions must be marked as different from original.
