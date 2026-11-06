// PROCMISSION.CPP
//
// Procedural Mission Generator for Red Alert (skirmish).
// See PROCMISSION.H for an overview.
//

#include "function.h"
#include "procmission.h"
#include "procini.h"

extern void GlyphX_Debug_Print(const char *debug_text);
extern void On_Message(const char* message, float timeout_seconds, long long message_id);
extern void On_Ping(const HouseClass* player_ptr, COORDINATE coord);
extern void Logic_Switch_Player_Context(HouseClass *house);


ProcPresetLibrary ProcPresets;
ProcMissionManager ProcMission;

// Waypoint borrowed while an EDGE wave picks its map edge. Restored straight after.
#define PROC_EDGE_WAYPOINT          25

#define PROC_OBJECTIVE_CHECK_RATE   (TICKS_PER_SECOND / 2)
#define PROC_ANNOUNCE_DELAY         (TICKS_PER_SECOND * 3)
#define PROC_MESSAGE_SECONDS        20.0f
#define PROC_WAVE_ACTIVE_TIME       (TICKS_PER_MINUTE * 4)  // After this a wave no longer blocks new ones
#define PROC_STATUS_RATE            TICKS_PER_MINUTE    // Debug status line (also read by tools/ProcTest)
#define PROC_NAVAL_REACH            12                  // A naval wave needs water this close to the victim's base
#define PROC_REVEAL_RADIUS          4                   // Shroud lifted around each objective target when announced
#define PROC_TARGET_REACH           3                   // How far a crowded area may push an objective target out
#define PROC_POINT_RADIUS           4                   // Control: units within this many cells hold a point
#define PROC_FREE_RADIUS            2                   // Rescue: a prisoner is freed when the player's units get this close
#define PROC_EXTRACT_RADIUS         5                   // Rescue: ... and rescued this close to the extraction point
#define PROC_FLEE_RADIUS            6                   // Assassinate: the target runs when the player's units get this close
#define PROC_NEUTRAL_CLEARANCE      20                  // Camps / outposts / neutral targets keep this far from enemy buildings


/*******************************************************************************
 * UTILITIES
 ******************************************************************************/

ProcSideType Proc_Side_Of(HousesType country)
{
    switch (country) {
        case HOUSE_USSR:
        case HOUSE_UKRAINE:
        case HOUSE_BAD:
            return PROC_SIDE_SOVIET;
        default:
            return PROC_SIDE_ALLIED;
    }
}

ProcSideType Proc_Side_Of(HouseClass const * house)
{
    return house ? Proc_Side_Of(house->ActLike) : PROC_SIDE_ANY;
}

static int Cell_Dist(CELL a, CELL b)
{
    int dx = abs(Cell_X(a) - Cell_X(b));
    int dy = abs(Cell_Y(a) - Cell_Y(b));
    return max(dx, dy) + min(dx, dy) / 2;
}

static CELL Clamp_To_Map(int x, int y)
{
    x = Bound(x, Map.MapCellX + 1, Map.MapCellX + Map.MapCellWidth - 2);
    y = Bound(y, Map.MapCellY + 1, Map.MapCellY + Map.MapCellHeight - 2);
    return XY_Cell(x, y);
}

// Cell 'cells' steps along the line from 'from' towards 'to'.
static CELL Toward(CELL from, CELL to, int cells)
{
    int dist = Cell_Dist(from, to);
    if (dist == 0) return from;
    cells = min(cells, dist);
    int x = Cell_X(from) + (Cell_X(to) - Cell_X(from)) * cells / dist;
    int y = Cell_Y(from) + (Cell_Y(to) - Cell_Y(from)) * cells / dist;
    return Clamp_To_Map(x, y);
}

static CELL Random_Cell_Around(CELL center, int minradius, int maxradius)
{
    int r = Random_Pick(minradius, max(minradius, maxradius));
    int dx = Random_Pick(-r, r);
    int dy = (Random_Pick(0, 1) == 0) ? -r : r;
    if (Random_Pick(0, 1) == 0) {
        int t = dx; dx = dy; dy = t;
    }
    return Clamp_To_Map(Cell_X(center) + dx, Cell_Y(center) + dy);
}

static bool Is_Allowed(TechnoTypeClass const & type, HouseClass const * house)
{
    int level = (int)type.Level;
    if (level < 0 || level > house->Control.TechLevel) return false;
    return (type.Get_Ownable() & (1L << house->ActLike)) != 0;
}

static CELL Find_Water_Near(CELL center, int maxradius)
{
    for (int radius = 1; radius <= maxradius; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                CELL cell = XY_Cell(Cell_X(center) + dx, Cell_Y(center) + dy);
                if (Map.In_Radar(cell) && Map[cell].Land_Type() == LAND_WATER && Map[cell].Cell_Object() == NULL) {
                    return cell;
                }
            }
        }
    }
    return -1;
}


/*******************************************************************************
 * SETUP
 ******************************************************************************/

ProcMissionManager::ProcMissionManager()
{
    Reset();
}

void ProcMissionManager::Reset()
{
    IsActive = false;
    IsAnnounced = false;
    IsFinished = false;
    IsSquad = false;
    IsSpectating = false;
    IsHuntingReinforcements = false;
    WaveRatePct = 100;
    ExtraWaves = 0;
    EnemyReinforceMinutes = 0;
    IsPlayerBase = false;
    IsBaseForced = false;
    IsAllyBase = false;
    AnchorCount = 0;
    PatrolCount = 0;
    NextWaypoint = PROC_FIRST_WAYPOINT;
    Settings = ProcSettings();
    Difficulty = DIFF_NORMAL;
    Diff = ProcDifficultyPreset();
    TechLevel = 10;
    PlayerSide = PROC_SIDE_ALLIED;
    Commander = NULL;
    PlayerTeamCount = 0;
    EnemyCount = 0;
    ObjectiveCount = 0;
    Elapsed = 0;
    StartFrame = 0;
    NextWaveFrame = 0;
    NextAttacker = 0;
    WaveCounter = 0;
    WaveTypeCount = 0;
    NextReinforce[0] = NextReinforce[1] = NextReinforce[2] = 0;
    ReinforceCounter = 0;
    ReinforceTypeCount = 0;
    LastWaveFrame = 0;
    NextEventFrame = 0;
    EventsUsed = 0;
    EventsFired = 0;
    SentryCount = 0;
    SentryLostFrame = 0;
    AllyStrength = 0;
    AllyHitFrame = 0;
    PeakBuildings = 0;
}

bool ProcMissionManager::Generate(CCINIClass & scenario_ini)
{
    char buf[200];

    Reset();

    /*
    **	A map opts in with Procedural=yes. With AllMaps=yes every map is procedural unless it
    **	says Procedural=no.
    */
    bool flagged = scenario_ini.Get_Bool("Basic", "Procedural", false);
    if (!flagged && scenario_ini.Is_Present("Basic", "Procedural")) return false;

    if (!ProcPresets.Load()) {
        if (flagged) GlyphX_Debug_Print("ProcMission: no base presets loaded (is PROCBASES.INI available?), mission not generated");
        return false;
    }

    Settings = ProcPresets.Settings;
    ProcPresets.Read_Settings(scenario_ini, PROC_OVERRIDE_SECTION, Settings);

    if (!flagged) {
        if (!Settings.IsAllMaps) return false;

        // AllMaps is a local INI switch; with several humans the other machines may not have it set.
        int humans = 0;
        for (int i = 0; i < Session.Players.Count(); i++) {
            HouseClass * house = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
            if (house && house->IsHuman) humans++;
        }
        if (humans != 1) {
            GlyphX_Debug_Print("ProcMission: AllMaps only applies to games with one human player");
            return false;
        }
    }

    GlyphX_Debug_Print("ProcMission: generating procedural mission");

    Difficulty = (Settings.Difficulty < 0) ? (DiffType)Random_Pick(0, DIFF_COUNT - 1) : (DiffType)Bound(Settings.Difficulty, 0, DIFF_COUNT - 1);
    Diff = ProcPresets.Difficulty[Difficulty];

    Classify_Houses();
    if (Commander == NULL || EnemyCount == 0) {
        GlyphX_Debug_Print("ProcMission: needs a human player and at least one AI on another team, mission not generated");
        return false;
    }

    // Spectating needs someone to watch: an AI on the human's lobby team.
    if (Settings.IsSpectate) {
        for (int i = 0; i < PlayerTeamCount; i++) {
            if (!PlayerTeam[i]->IsHuman) IsSpectating = true;
        }
        GlyphX_Debug_Print(IsSpectating ? "ProcMission: spectator mode" : "ProcMission: Spectate=yes needs an AI on the player's team, playing normally");
    }

    /*
    **	Tech level comes from the lobby unless the generator settings or the map force one.
    */
    if (Settings.TechLevel > 0) {
        // Same 1-10 scale as the lobby slider, converted the same way the lobby value is.
        extern int _build_tech[11];
        TechLevel = _build_tech[Bound(Settings.TechLevel, 1, 10)];
        for (int i = 0; i < Session.Players.Count(); i++) {
            HouseClass * house = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
            if (house) house->Control.TechLevel = TechLevel;
        }
    } else {
        TechLevel = Commander->Control.TechLevel;
    }
    PlayerSide = Proc_Side_Of(Commander);

    sprintf(buf, "ProcMission: side=%s allies=%d enemies=%d tech=%d difficulty=%d",
        Proc_Side_Name(PlayerSide), PlayerTeamCount - 1, EnemyCount, TechLevel, (int)Difficulty);
    GlyphX_Debug_Print(buf);

    /*
    **	Placement runs with ScenarioInit off (as Create_Units does) so cell clearance checks
    **	are real. Buildings switch it back on for their own Unlimbo.
    */
    int save_init = ScenarioInit;
    ScenarioInit = 0;

    Pick_Objectives();
    Build_Enemy_Bases();
    Build_Player_Bases();

    /*
    **	The primary objective can reshape the mission (squad missions): waves on/off or later,
    **	passive enemies, patrols around the targets. Read before placing targets, which may
    **	swap an unplaceable primary for "destroy all" (the squad has already been handed out).
    */
    ProcObjectivePreset const * primary = (ObjectiveCount > 0) ? Objectives[0].Preset : NULL;
    Place_Objective_Targets();
    Place_Sentries();

    int firstwave = Diff.FirstWaveSeconds;
    if (primary != NULL) {
        if (primary->WavesOverride >= 0) Settings.IsWaves = (primary->WavesOverride != 0);
        if (primary->WaveDelaySeconds > 0) firstwave = primary->WaveDelaySeconds;

        if (primary->EnemyProductionOverride >= 0) Settings.IsEnemyProduction = (primary->EnemyProductionOverride != 0);

        // Survival missions turn the pressure up.
        WaveRatePct = primary->WaveRate;
        ExtraWaves = primary->ExtraWaves;
        IsHuntingReinforcements = primary->IsHuntingReinforcements;
    }
    EnemyReinforceMinutes = (primary != NULL && primary->EnemyReinforceMinutes > 0) ? primary->EnemyReinforceMinutes : Diff.EnemyReinforceMinutes;

    /*
    **	A team that starts with a base has defences and factories from the first minute, so the
    **	enemy comes sooner, bigger and more often. Objectives that hand out the base themselves
    **	(survival, protect) carry their own pressure keys and are left as tuned.
    */
    if (IsPlayerBase && !IsBaseForced && !IsSpectating) {
        WaveRatePct = (WaveRatePct * Diff.BaseWaveRatePct) / 100;
        ExtraWaves += Diff.BaseExtraWaves;
        Diff.WaveSizePct = (Diff.WaveSizePct * Diff.BaseWaveSizePct + 50) / 100;
        Diff.StartThreat = (ProcThreatType)min((int)Diff.MaxThreat, (int)Diff.StartThreat + Diff.BaseThreatSteps);
        firstwave = (firstwave * Diff.BaseFirstWavePct) / 100;
        sprintf(buf, "ProcMission: base start: wave rate %d%%, wave size %d%%, max waves %d, start threat %d, first wave %ds",
            WaveRatePct, Diff.WaveSizePct, Diff.MaxActiveWaves + ExtraWaves, (int)Diff.StartThreat, firstwave);
        GlyphX_Debug_Print(buf);
    }

    // A spectated game is AI against AI: both sides build and attack.
    if (IsSpectating) Settings.IsEnemyProduction = true;

    /*
    **	Skirmish AIs run at full IQ: left alone they build, expand and attack on their own,
    **	whatever the mission is. By default they only hold their generated bases and the
    **	director sends every attack, so the presets and difficulty tables decide how hard it is.
    */
    if (!Settings.IsEnemyProduction) {
        Disable_Enemy_Production();
        GlyphX_Debug_Print("ProcMission: enemy production disabled");
    }

    if (primary != NULL) {
        Spawn_Patrols(primary->Patrols, primary->PatrolThreat);
    }

    ScenarioInit = save_init;

    for (int i = 0; i < EnemyCount; i++) {
        Enemies[i].InitialBuildings = max(1, Enemies[i].House->CurBuildings);
    }

    StartFrame = Elapsed;
    NextWaveFrame = Elapsed + (firstwave * TICKS_PER_SECOND * Diff.WaveIntervalPct) / 100;
    IsActive = true;

    GlyphX_Debug_Print("ProcMission: generation complete");
    return true;
}

void ProcMissionManager::Classify_Houses()
{
    PlayerTeamCount = 0;
    EnemyCount = 0;

    // PlayerPtr is the local player straight after GlyphX_Assign_Houses.
    Commander = (PlayerPtr && PlayerPtr->IsHuman) ? PlayerPtr : NULL;
    for (int i = 0; i < Session.Players.Count() && Commander == NULL; i++) {
        HouseClass * house = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
        if (house && house->IsHuman) Commander = house;
    }
    if (Commander == NULL) return;

    for (int i = 0; i < Session.Players.Count(); i++) {
        HouseClass * house = HouseClass::As_Pointer(Session.Players[i]->Player.ID);
        if (house == NULL || house->IsDefeated) continue;

        if (house == Commander || house->Is_Ally(Commander)) {
            if (PlayerTeamCount < PROC_MAX_HOUSES) PlayerTeam[PlayerTeamCount++] = house;
        } else if (EnemyCount < PROC_MAX_HOUSES) {
            Enemies[EnemyCount].House = house;
            Enemies[EnemyCount].Home = Coord_Cell(house->Center);
            EnemyCount++;
        }
    }
}

void ProcMissionManager::Pick_Objectives()
{
    static ProcObjectivePreset s_Fallback;
    if (s_Fallback.Name[0] == '\0') {
        strcpy(s_Fallback.Name, "DEFAULT_DESTROY_BASES");
        s_Fallback.Kind = PROC_OBJ_DESTROY_BASES;
        s_Fallback.Slot = PROC_SLOT_PRIMARY;
        strcpy(s_Fallback.Text, "Destroy all enemy forces.");
    }

    ProcObjectivePreset const * chosen[PROC_MAX_OBJECTIVES];
    ObjectiveCount = 0;

    ProcObjectivePreset const * primary = NULL;
    if (IsSpectating) {
        // The AI can't play objectives: it is a straight fight, without bonuses.
        for (int i = 0; i < ProcPresets.ObjectiveCount && primary == NULL; i++) {
            if (ProcPresets.Objectives[i].Kind == PROC_OBJ_DESTROY_BASES) primary = &ProcPresets.Objectives[i];
        }
        if (primary == NULL) primary = &s_Fallback;
        IsSquad = false;
        IsPlayerBase = false;
        IsAllyBase = (Settings.AllyStart == PROC_START_BASE ||
            (Settings.AllyStart == PROC_START_RANDOM && Random_Pick(0, 99) < Settings.PlayerBaseChance));
        Add_Objective(primary, true, chosen);
        return;
    }
    if (stricmp(Settings.Primary, "RANDOM") != 0) {
        primary = ProcPresets.Find_Objective(Settings.Primary);
        if (primary == NULL) {
            char buf[100];
            sprintf(buf, "ProcMission: primary objective '%s' not found, picking at random", Settings.Primary);
            GlyphX_Debug_Print(buf);
        }
    }
    if (primary == NULL) primary = ProcPresets.Pick_Objective(PlayerSide, true, TechLevel, true, true, NULL, 0);
    if (primary == NULL) primary = &s_Fallback;

    /*
    **  Decide now whether the player's team starts with a base: bonus objectives that put a
    **  structure in the player's base only make sense when there is one. A primary that asks
    **  for a base, or targets a player structure, always gets one. One roll covers all the
    **  humans, so co-op players start alike.
    */
    // A squad mission starts with neither an MCV nor a base.
    IsSquad = primary->IsSquadStart;
    bool forced = primary->PlayerBase[0] != '\0' || (primary->Owner == PROC_OWNER_PLAYER && primary->TargetCount > 0);
    IsBaseForced = !IsSquad && forced;
    IsPlayerBase = !IsSquad && (forced || Settings.PlayerStart == PROC_START_BASE ||
        (Settings.PlayerStart == PROC_START_RANDOM && Random_Pick(0, 99) < Settings.PlayerBaseChance));
    switch (Settings.AllyStart) {
        case PROC_START_BASE:   IsAllyBase = true; break;
        case PROC_START_RANDOM: IsAllyBase = Random_Pick(0, 99) < Settings.PlayerBaseChance; break;
        case PROC_START_SAME:   IsAllyBase = IsPlayerBase; break;
        default:                IsAllyBase = false; break;
    }

    // In a squad mission only an engineer in the squad can capture.
    bool cancapture = true;
    if (IsSquad) {
        cancapture = false;
        ProcTeamPreset const * squad = Find_Squad(primary, PlayerSide);
        for (int m = 0; squad != NULL && m < squad->MemberCount; m++) {
            if (squad->Members[m].Type == &InfantryTypeClass::As_Reference(INFANTRY_RENOVATOR)) cancapture = true;
        }
    }

    int secondaries = Random_Pick(Settings.SecondaryMin, Settings.SecondaryMax);
    secondaries = Bound(secondaries, 0, PROC_MAX_OBJECTIVES - 1);

    Add_Objective(primary, true, chosen);

    // A primary can bring a fixed bonus with it (e.g. squad missions: capture a forward base).
    if (primary->Bonus[0] != '\0') {
        ProcObjectivePreset const * bonus = ProcPresets.Find_Objective(primary->Bonus);
        if (bonus != NULL && bonus->Kind == PROC_OBJ_CAPTURE && !cancapture) {
            char buf[100];
            sprintf(buf, "ProcMission: bonus '%s' needs an engineer the squad doesn't have", primary->Bonus);
            GlyphX_Debug_Print(buf);
        } else if (bonus != NULL && bonus != primary) {
            Add_Objective(bonus, false, chosen);
            secondaries = max(secondaries, 1);
        } else {
            char buf[100];
            sprintf(buf, "ProcMission: bonus objective '%s' not found", primary->Bonus);
            GlyphX_Debug_Print(buf);
        }
    }

    while (ObjectiveCount <= secondaries && ObjectiveCount < PROC_MAX_OBJECTIVES) {
        ProcObjectivePreset const * preset = ProcPresets.Pick_Objective(PlayerSide, false, TechLevel, IsPlayerBase, cancapture, chosen, ObjectiveCount);
        if (preset == NULL) break;
        Add_Objective(preset, false, chosen);
    }
}

void ProcMissionManager::Add_Objective(ProcObjectivePreset const * preset, bool primary, ProcObjectivePreset const ** chosen)
{
    ProcObjective & obj = Objectives[ObjectiveCount];
    obj = ProcObjective();
    obj.Preset = preset;
    obj.IsPrimary = primary;
    obj.Minutes = Random_Pick(preset->MinMinutes, preset->MaxMinutes);
    chosen[ObjectiveCount++] = preset;

    char buf[120];
    sprintf(buf, "ProcMission: %s objective %s (%s, %d min)", primary ? "primary" : "secondary",
        preset->Name, Proc_Objective_Kind_Name(preset->Kind), obj.Minutes);
    GlyphX_Debug_Print(buf);
}

void ProcMissionManager::Build_Enemy_Bases()
{
    char buf[256];

    for (int i = 0; i < EnemyCount; i++) {
        HouseClass * house = Enemies[i].House;
        ProcSideType side = Proc_Side_Of(house);

        // The first enemy always gets a main base; the rest may get outposts.
        ProcBaseRoleType role = (i > 0 && Random_Pick(0, 99) < Diff.OutpostChance) ? PROC_BASE_OUTPOST : PROC_BASE_MAIN;
        ProcBasePreset const * preset = ProcPresets.Pick_Base(side, role, house->Control.TechLevel, Difficulty);
        if (preset == NULL && role == PROC_BASE_OUTPOST) {
            preset = ProcPresets.Pick_Base(side, PROC_BASE_MAIN, house->Control.TechLevel, Difficulty);
        }
        if (preset == NULL) {
            sprintf(buf, "ProcMission: no base preset for %s enemy, keeping lobby start", Proc_Side_Name(side));
            GlyphX_Debug_Print(buf);
            continue;
        }

        Clear_Start_Units(house, Settings.IsKeepStartUnits);
        Build_Base(house, preset, false);

        house->Credits += (preset->Credits * Diff.EnemyCreditsPct) / 100;
        house->IsBaseBuilding = true;

        int len = sprintf(buf, "ProcMission: enemy %d (%s) base '%s', %d buildings (power %d/%d)", i, Proc_Side_Name(side), preset->Name,
            house->CurBuildings, house->Power, house->Drain);

        // What it makes things with, and any superweapons.
        static StructType const s_Listed[] = {STRUCT_CONST, STRUCT_BARRACKS, STRUCT_TENT, STRUCT_WEAP, STRUCT_AIRSTRIP, STRUCT_HELIPAD, STRUCT_REFINERY,
            STRUCT_MSLO, STRUCT_IRON_CURTAIN, STRUCT_CHRONOSPHERE};
        for (int t = 0; t < ARRAY_SIZE(s_Listed) && len < 180; t++) {
            int count = 0;
            for (int b = 0; b < Buildings.Count(); b++) {
                BuildingClass * building = Buildings.Ptr(b);
                if ((HouseClass *)building->House == house && *building == s_Listed[t] && !building->IsInLimbo) count++;
            }
            if (count > 0) len += sprintf(buf + len, " %s=%d", BuildingTypeClass::As_Reference(s_Listed[t]).IniName, count);
        }
        GlyphX_Debug_Print(buf);
    }
}

/*
**	The player's team either starts the normal skirmish way (MCV) or with an established
**	base from a Defend preset. A primary objective with PlayerBase= forces a base for the
**	human players; otherwise PlayerStart / AllyStart decide.
*/
void ProcMissionManager::Build_Player_Bases()
{
    char buf[120];

    // Pick_Objectives has already decided who starts with a base (IsPlayerBase / IsAllyBase).
    ProcObjectivePreset const * primary = (ObjectiveCount > 0) ? Objectives[0].Preset : NULL;
    bool named = (primary != NULL && primary->PlayerBase[0] != '\0' && stricmp(primary->PlayerBase, "AUTO") != 0);

    if (IsSquad && primary != NULL) {
        Build_Squads(primary);
    }

    for (int i = 0; i < PlayerTeamCount; i++) {
        HouseClass * house = PlayerTeam[i];

        // Spectators start with nothing at all.
        if (IsSpectating && house->IsHuman) {
            Clear_Start_Units(house, false);
            house->Credits = 0;
            sprintf(buf, "ProcMission: player %s is spectating", house->IniName);
            GlyphX_Debug_Print(buf);
            continue;
        }
        if (!(house->IsHuman ? IsPlayerBase : IsAllyBase)) continue;

        ProcBasePreset const * preset = NULL;
        if (house->IsHuman && named) {
            preset = ProcPresets.Find_Base(primary->PlayerBase);
        }
        if (preset == NULL) {
            preset = ProcPresets.Pick_Base(Proc_Side_Of(house), PROC_BASE_DEFEND, house->Control.TechLevel);
        }
        if (preset == NULL) continue;

        Clear_Start_Units(house, true);
        Build_Base(house, preset, true);
        if (!house->IsHuman) {
            house->Credits += preset->Credits;
            house->IsBaseBuilding = true;
        }

        sprintf(buf, "ProcMission: %s %s starts with base '%s' (power %d/%d)", house->IsHuman ? "player" : "ally",
            house->IniName, preset->Name, house->Power, house->Drain);
        GlyphX_Debug_Print(buf);
    }
}

/*
**	Squad missions: every human loses the MCV and lobby units and gets the first Squad= preset
**	that suits their side instead. Squads are special forces, so the tech level is ignored.
*/
void ProcMissionManager::Build_Squads(ProcObjectivePreset const * primary)
{
    char buf[128];

    for (int i = 0; i < PlayerTeamCount; i++) {
        HouseClass * house = PlayerTeam[i];
        if (!house->IsHuman) continue;

        ProcSideType side = Proc_Side_Of(house);
        ProcTeamPreset const * squad = Find_Squad(primary, side);
        if (squad == NULL) {
            sprintf(buf, "ProcMission: no %s squad in '%s', keeping the lobby start", Proc_Side_Name(side), primary->Name);
            GlyphX_Debug_Print(buf);
            continue;
        }

        Clear_Start_Units(house, false);
        CELL home = Home_Cell(house);
        int count = 0;
        for (int m = 0; m < squad->MemberCount; m++) {
            int before = house->CurUnits + house->CurInfantry;
            Place_Units(house, squad->Members[m].Type, squad->Members[m].Count, home, MISSION_GUARD, 0, 2);   // Keep the squad together
            count += house->CurUnits + house->CurInfantry - before;
        }
        if (primary->PlayerCredits >= 0) {
            house->Credits = primary->PlayerCredits;
        }

        sprintf(buf, "ProcMission: player %s starts with squad '%s' (%d units, %ld credits) at cell %d", house->IniName, squad->Name, count, house->Credits, (int)home);
        GlyphX_Debug_Print(buf);
    }
}

ProcTeamPreset const * ProcMissionManager::Find_Squad(ProcObjectivePreset const * primary, ProcSideType side) const
{
    for (int s = 0; s < primary->SquadCount; s++) {
        ProcTeamPreset const * candidate = ProcPresets.Find_Team(primary->Squads[s]);
        if (candidate != NULL && (candidate->Side == PROC_SIDE_ANY || candidate->Side == side)) return candidate;
    }
    return NULL;
}

/*
**	A small enemy outpost around an Owner=Outpost objective's target (placed first, so the
**	outpost can never crowd it out). It never gets a construction yard of its own: if one is
**	wanted, it is the objective target (capture it to start building).
*/
void ProcMissionManager::Build_Outpost(HouseClass * house, ProcObjectivePreset const * preset, CELL center)
{
    ProcBasePreset const * base = NULL;
    bool camp = (stricmp(preset->OutpostBase, "CAMP") == 0);
    if (preset->OutpostBase[0] != '\0' && stricmp(preset->OutpostBase, "AUTO") != 0 && !camp) {
        base = ProcPresets.Find_Base(preset->OutpostBase);
    }
    if (base == NULL) {
        base = ProcPresets.Pick_Base(Proc_Side_Of(house), camp ? PROC_BASE_CAMP : PROC_BASE_OUTPOST, house->Control.TechLevel, Difficulty);
    }
    if (base != NULL) {
        Build_Base(house, base, false, center, true);
    }

    char buf[128];
    sprintf(buf, "ProcMission: outpost '%s' for %s at cell %d", base ? base->Name : "-", preset->Name, (int)center);
    GlyphX_Debug_Print(buf);
}

void ProcMissionManager::Add_Anchor(CELL cell, HouseClass * owner)
{
    if (cell == -1 || owner == NULL || AnchorCount >= PROC_MAX_ANCHORS) return;
    for (int i = 0; i < AnchorCount; i++) {
        if (Cell_Dist(Anchors[i], cell) < 6) return;      // Already covered
    }
    Anchors[AnchorCount] = cell;
    AnchorOwners[AnchorCount] = owner;
    AnchorCount++;
}

/*
**	Generated patrol routes use waypoints from PROC_FIRST_WAYPOINT up, skipping any the map uses.
*/
int ProcMissionManager::Claim_Waypoint(CELL cell)
{
    for (; NextWaypoint < WAYPT_HOME; NextWaypoint++) {
        if (Scen.Waypoint[NextWaypoint] == -1) {
            Scen.Waypoint[NextWaypoint] = cell;
            return NextWaypoint++;
        }
    }
    return -1;
}

/*
**	Patrols walk a loop of three points around the anchors (objective targets and outposts
**	first, then the enemy bases), attacking whatever they see on the way.
*/
void ProcMissionManager::Spawn_Patrols(int count, ProcThreatType maxthreat)
{
    char buf[128];

    int firsthome = AnchorCount;
    for (int i = 0; i < EnemyCount; i++) {
        Add_Anchor(Enemies[i].Home, Enemies[i].House);
    }

    for (int p = 0; p < count && AnchorCount > 0; p++) {
        // One patrol per objective / outpost; any extra patrols guard the enemy bases.
        int a = (p < AnchorCount || firsthome >= AnchorCount) ? p % AnchorCount : firsthome + (p - AnchorCount) % (AnchorCount - firsthome);
        CELL anchor = Anchors[a];
        HouseClass * owner = AnchorOwners[a];
        if (!Is_Alive(owner)) continue;

        ProcTeamPreset const * preset = ProcPresets.Pick_Team(Proc_Side_Of(owner), PROC_TEAM_PATROL, PROC_THREAT_SCOUT,
            (ProcThreatType)min((int)Diff.MaxThreat, (int)maxthreat), owner->Control.TechLevel);
        if (preset == NULL) continue;

        // Three points spread around the anchor.
        int points[3];
        int pointcount = 0;
        int start = Random_Pick(0, 7);
        static int const dx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        static int const dy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        for (int k = 0; k < 3; k++) {
            int dir = (start + k * 3) % 8;
            int radius = Random_Pick(6, 11);
            CELL cell = Clamp_To_Map(Cell_X(anchor) + dx[dir] * radius, Cell_Y(anchor) + dy[dir] * radius);
            cell = Map.Nearby_Location(cell, SPEED_TRACK);
            if (cell <= 0) continue;

            // Never through the player's starting area.
            bool tooclose = false;
            for (int h = 0; h < PlayerTeamCount; h++) {
                if (Cell_Dist(cell, Home_Cell(PlayerTeam[h])) < PROC_PATROL_CLEARANCE) tooclose = true;
            }
            if (tooclose) continue;
            int waypoint = Claim_Waypoint(cell);
            if (waypoint < 0) break;
            points[pointcount++] = waypoint;
        }
        if (pointcount < 2 || TeamTypes.Count() >= TeamTypes.Length()) continue;

        TeamTypeClass * tt = new TeamTypeClass();
        if (tt == NULL) continue;
        sprintf((char *)tt->IniName, "PROCP%02d", p);
        tt->House = owner->Class->House;
        tt->IsAutocreate = false;
        tt->IsPrebuilt = false;
        tt->IsReinforcable = false;
        tt->RecruitPriority = 7;
        tt->MaxAllowed = 1;
        tt->ClassCount = 0;
        for (int m = 0; m < preset->MemberCount && tt->ClassCount < TeamTypeClass::MAX_TEAM_CLASSCOUNT; m++) {
            tt->Members[tt->ClassCount].Class = preset->Members[m].Type;
            tt->Members[tt->ClassCount].Quantity = max(1, (preset->Members[m].Count * Diff.GuardScalePct + 50) / 100);
            tt->ClassCount++;
        }
        int m = 0;
        for (int k = 0; k < pointcount; k++) {
            tt->MissionList[m].Mission = TMISSION_PATROL;
            tt->MissionList[m++].Data.Value = points[k];
        }
        tt->MissionList[m].Mission = TMISSION_LOOP;
        tt->MissionList[m++].Data.Value = 0;
        tt->MissionCount = m;

        if (!Spawn_Team(tt, Scen.Waypoint[points[0]], 0, 3)) {
            delete tt;
            continue;
        }
        PatrolTypes[PatrolCount] = tt;
        PatrolStart[PatrolCount] = Scen.Waypoint[points[0]];
        PatrolCount++;

        sprintf(buf, "ProcMission: patrol %s (%s) around cell %d, %d points", tt->IniName, preset->Name, (int)anchor, pointcount);
        GlyphX_Debug_Print(buf);
    }
}

void ProcMissionManager::Clear_Start_Units(HouseClass * house, bool keepcombat)
{
    for (int i = 0; i < Units.Count(); i++) {
        UnitClass * unit = Units.Ptr(i);
        if (unit->House == house && (!keepcombat || *unit == UNIT_MCV)) {
            delete unit;
            i--;
        }
    }
    if (!keepcombat) {
        for (int i = 0; i < Infantry.Count(); i++) {
            if (Infantry.Ptr(i)->House == house) {
                delete Infantry.Ptr(i);
                i--;
            }
        }
    }
}


/*******************************************************************************
 * BASE BUILDING
 ******************************************************************************/

bool ProcMissionManager::Can_Place_Building(StructType type, CELL cell) const
{
    BuildingTypeClass const & btype = BuildingTypeClass::As_Reference(type);
    SpeedType loco = (btype.Speed == SPEED_FLOAT) ? SPEED_FLOAT : SPEED_NONE;

    short const * list = btype.Occupy_List(true);
    while (*list != REFRESH_EOL) {
        CELL check = cell + *list++;
        if (!Map.In_Radar(check)) return false;
        if (abs(Cell_X(check) - Cell_X(cell)) > 5) return false;        // Wrapped around a map row
        if (!Map[check].Is_Clear_To_Build(loco)) return false;

        // Keep a one cell gap to other buildings so factories and paths are never boxed in.
        for (FacingType face = FACING_N; face < FACING_COUNT; face++) {
            CELL adjacent = Adjacent_Cell(check, face);
            if (Map.In_Radar(adjacent) && Map[adjacent].Cell_Building() != NULL) return false;
        }
    }
    return true;
}

/*
**	'reach' is how far past maxradius a crowded area may push the building. Bases use a lot,
**	objective targets little: a target far outside its base (or camp) is misleading, and its
**	patrols would follow it into the open.
*/
bool ProcMissionManager::Place_Building(HouseClass * house, StructType type, CELL center, int minradius, int maxradius, TARGET * placed, int reach)
{
    if (type == STRUCT_NONE || center == -1) return false;

    // Random spots first, so layouts differ every game.
    for (int attempt = 0; attempt < 80; attempt++) {
        // Widen the search when the preferred ring is crowded.
        int widen = min((attempt / 20) * 3, reach);
        CELL cell = (attempt == 0 && minradius == 0) ? center : Random_Cell_Around(center, minradius, maxradius + widen);
        if (Try_Place_Building(house, type, cell, placed)) return true;
    }

    /*
    **	Crowded base: scan every ring outwards (from a random start on each ring) so a free
    **	spot is found whenever one exists within reach.
    */
    for (int radius = minradius; radius <= maxradius + reach; radius++) {
        int perimeter = max(1, radius * 8);
        int offset = Random_Pick(0, perimeter - 1);
        for (int step = 0; step < perimeter; step++) {
            int pos = (step + offset) % perimeter;
            int side = pos / max(1, radius * 2);
            int along = pos % max(1, radius * 2) - radius;
            int dx = 0, dy = 0;
            switch (side) {
                case 0:  dx = along;   dy = -radius; break;
                case 1:  dx = radius;  dy = along;   break;
                case 2:  dx = -along;  dy = radius;  break;
                default: dx = -radius; dy = -along;  break;
            }
            int x = Cell_X(center) + dx;
            int y = Cell_Y(center) + dy;
            if (x < Map.MapCellX || y < Map.MapCellY || x >= Map.MapCellX + Map.MapCellWidth || y >= Map.MapCellY + Map.MapCellHeight) continue;
            if (Try_Place_Building(house, type, XY_Cell(x, y), placed)) return true;
        }
    }
    return false;
}

bool ProcMissionManager::Try_Place_Building(HouseClass * house, StructType type, CELL cell, TARGET * placed)
{
    if (!Can_Place_Building(type, cell)) return false;

    BuildingClass * building = (BuildingClass *)BuildingTypeClass::As_Reference(type).Create_One_Of(house);
    if (building == NULL) return false;

    // Placed as a pre-existing structure: no build-up animation, no free refinery harvester.
    ScenarioInit++;
    bool ok = building->Unlimbo(Cell_Coord(cell), DIR_N);
    ScenarioInit--;
    if (!ok) {
        delete building;
        return false;
    }

    /*
    **	A building only adds its power output on its first AI tick (when Strength != LastStrength).
    **	Do that update now, exactly as BuildingClass::AI would, so the house's Power is right
    **	while the base is still being generated (auto power, objective targets) and the player
    **	doesn't get a low power warning on the first frame.
    */
    int oldpower = building->Power_Output();
    building->LastStrength = building->Strength;
    house->Adjust_Power(building->Power_Output() - oldpower);

    if (placed) *placed = building->As_Target();
    return true;
}

void ProcMissionManager::Place_Units(HouseClass * house, TechnoTypeClass const * type, int count, CELL center, MissionType mission, int minradius, int maxradius)
{
    if (type == NULL || center == -1) return;

    bool isvessel = (type->What_Am_I() == RTTI_VESSELTYPE);
    if (type->What_Am_I() == RTTI_AIRCRAFTTYPE) return;     // Aircraft need a helipad / airfield; never placed loose

    for (int i = 0; i < count; i++) {
        if (isvessel && Find_Water_Near(center, 24) == -1) return;
        Spawn_One(house, type, center, minradius, maxradius, mission);
    }
}

/*
**	One unit of this type around 'center' (ships on the nearest water), or NULL if there is no room.
*/
TechnoClass * ProcMissionManager::Spawn_One(HouseClass * house, TechnoTypeClass const * type, CELL center, int minradius, int maxradius, MissionType mission)
{
    if (type == NULL || house == NULL || center == -1 || type->What_Am_I() == RTTI_AIRCRAFTTYPE) return NULL;

    bool isvessel = (type->What_Am_I() == RTTI_VESSELTYPE);
    CELL cell = isvessel ? Find_Water_Near(center, 24) : Random_Cell_Around(center, minradius, maxradius);
    if (cell == -1) return NULL;

    TechnoClass * obj = (TechnoClass *)type->Create_One_Of(house);
    if (obj == NULL) return NULL;

    if (!Scan_Place_Object(obj, cell)) {
        delete obj;
        return NULL;
    }
    obj->Assign_Mission(mission);
    return obj;
}

void ProcMissionManager::Place_Naval_Yard(HouseClass * house, CELL center)
{
    StructType type = (Proc_Side_Of(house) == PROC_SIDE_SOVIET) ? STRUCT_SUB_PEN : STRUCT_SHIP_YARD;
    if (!Is_Allowed(BuildingTypeClass::As_Reference(type), house)) return;

    CELL water = Find_Water_Near(center, 20);
    if (water == -1) return;

    for (int radius = 0; radius <= 6; radius++) {
        for (int attempt = 0; attempt < 12; attempt++) {
            CELL cell = (radius == 0) ? water : Random_Cell_Around(water, radius, radius);
            if (!Can_Place_Building(type, cell)) continue;

            BuildingClass * building = (BuildingClass *)BuildingTypeClass::As_Reference(type).Create_One_Of(house);
            if (building == NULL) return;
            ScenarioInit++;
            bool ok = building->Unlimbo(Cell_Coord(cell), DIR_N);
            ScenarioInit--;
            if (ok) return;
            delete building;
        }
    }
}

/*
**	'isplayer' means the base belongs to the player's team: no difficulty scaling.
*/
// Buildings that make things: factories (not the construction yard or naval yards) and refineries.
static bool Is_Production(StructType type)
{
    if (type == STRUCT_REFINERY) return true;
    if (type == STRUCT_CONST || type == STRUCT_SHIP_YARD || type == STRUCT_SUB_PEN) return false;
    return BuildingTypeClass::As_Reference(type).ToBuild != RTTI_NONE;
}

bool ProcMissionManager::Build_Base(HouseClass * house, ProcBasePreset const * preset, bool isplayer, CELL center, bool skipconst)
{
    if (center == -1) center = Coord_Cell(house->Center);
    if (!Map.In_Radar(center)) return false;

    // An enemy main base also gets the difficulty's superweapons.
    ProcBaseEntry entries[PROC_MAX_BASE_ENTRIES + ARRAY_SIZE(Diff.Superweapons)];
    int entrycount = 0;
    for (int e = 0; e < preset->EntryCount; e++) entries[entrycount++] = preset->Entries[e];
    bool mainbase = (!isplayer && preset->Role == PROC_BASE_MAIN);
    if (mainbase) {
        for (int s = 0; s < Diff.SuperweaponCount; s++) {
            ProcBaseEntry & entry = entries[entrycount++];
            entry = ProcBaseEntry();
            entry.Type = Diff.Superweapons[s];
            entry.MinRadius = 5;
            entry.MaxRadius = 11;
        }
    }

    /*
    **	Harder enemy main bases don't hang on a single building: spare construction yards
    **	(so one lucky strike doesn't end its rebuilding) and a minimum of refineries.
    */
    if (mainbase) {
        int yards = 0;
        int after = 0;      // The spares go straight after the first yard, while there's room
        for (int e = 0; e < entrycount; e++) {
            if (entries[e].Type == STRUCT_CONST) {
                yards += entries[e].Count;
                after = e + 1;
            }
        }
        if (yards < Diff.MinConstructionYards && entrycount < ARRAY_SIZE(entries)) {
            for (int e = entrycount; e > after; e--) entries[e] = entries[e - 1];
            entrycount++;
            ProcBaseEntry & entry = entries[after];
            entry = ProcBaseEntry();
            entry.Type = STRUCT_CONST;
            entry.Count = Diff.MinConstructionYards - yards;
            entry.MinRadius = 4;
            entry.MaxRadius = 10;
        }
    }
    int refineries = 0;

    bool refinery = false;
    for (int e = 0; e < entrycount; e++) {
        ProcBaseEntry const & entry = entries[e];
        BuildingTypeClass const & btype = BuildingTypeClass::As_Reference(entry.Type);
        if (entry.Type == STRUCT_CONST && skipconst) continue;

        // The tech level and side decide what actually exists in this base.
        if (entry.Type != STRUCT_CONST && !Is_Allowed(btype, house)) continue;

        int count = entry.Count;
        if (entry.IsDefense && !isplayer) {
            count = (count * Diff.DefenseScalePct + 50) / 100;
        } else if (!isplayer && Is_Production(entry.Type)) {
            // Harder enemies get more factories and refineries; the fraction is a chance of one more.
            int scaled = entry.Count * Diff.FactoryScalePct;
            count = scaled / 100 + ((Random_Pick(0, 99) < scaled % 100) ? 1 : 0);
            count = max(count, min(entry.Count, 1));     // never lose the only one
        }
        for (int n = 0; n < count; n++) {
            TARGET placed = TARGET_NONE;
            if (!Place_Building(house, entry.Type, center, entry.MinRadius, entry.MaxRadius, &placed)) continue;

            // Keep power up as the base grows. When a crowded base has no room left for the
            // power plant a building needs, the building goes instead (the base starts powered).
            if (preset->IsAutoPower) {
                Add_Power(house, center);
                BuildingClass * building = As_Building(placed);
                if (house->Power < house->Drain && building != NULL && building->Class->Drain > 0 && entry.Type != STRUCT_CONST) {
                    building->Limbo();
                    delete building;
                    continue;
                }
            }
            if (entry.Type == STRUCT_REFINERY) {
                refinery = true;
                refineries++;
            }
        }
    }

    // Top the refineries up to the difficulty's minimum, each with a harvester of its own.
    int extraharvesters = 0;
    if (mainbase && Is_Allowed(BuildingTypeClass::As_Reference(STRUCT_REFINERY), house)) {
        for (int tries = 0; refineries < Diff.MinRefineries && tries < Diff.MinRefineries * 2; tries++) {
            if (!Place_Building(house, STRUCT_REFINERY, center, 5, 12)) continue;
            if (preset->IsAutoPower) Add_Power(house, center);
            refinery = true;
            refineries++;
            extraharvesters++;
        }
    }

    if (preset->IsNaval) {
        Place_Naval_Yard(house, center);
    }

    if (preset->IsAutoPower) {
        Add_Power(house, center);

        // Still short (no room for another plant): drop the newest power users until it isn't.
        for (int i = Buildings.Count() - 1; i >= 0 && house->Power < house->Drain; i--) {
            BuildingClass * building = Buildings.Ptr(i);
            if ((HouseClass *)building->House != house || building->Class->Drain <= 0 || *building == STRUCT_CONST) continue;
            building->Limbo();
            delete building;
        }
    }

    // Only bases that got their own refinery (an outpost may share a house with a main base).
    if (refinery) {
        Place_Units(house, &UnitTypeClass::As_Reference(UNIT_HARVESTER), preset->Harvesters + extraharvesters, center, MISSION_HARVEST);
    }

    MissionType guard = house->IsHuman ? MISSION_GUARD : MISSION_GUARD_AREA;
    for (int g = 0; g < preset->GuardCount; g++) {
        TechnoTypeClass const * type = preset->Guards[g].Type;
        if (!Is_Allowed(*type, house)) continue;

        int count = preset->Guards[g].Count;
        if (!isplayer) count = (count * Diff.GuardScalePct + 50) / 100;
        Place_Units(house, type, count, center, guard);
    }
    return true;
}

/*
**	Adds power plants around 'center' until the house's power covers its drain (with a
**	small margin), so bases never start with the low power warning.
*/
void ProcMissionManager::Add_Power(HouseClass * house, CELL center)
{
    StructType power = Is_Allowed(BuildingTypeClass::As_Reference(STRUCT_ADVANCED_POWER), house) ? STRUCT_ADVANCED_POWER : STRUCT_POWER;
    for (int n = 0; n < 12 && house->Power < house->Drain + 50; n++) {
        if (!Place_Building(house, power, center, 3, 11)) break;
    }
}

/*
**	A building of this type the house already owns near 'center' that no other objective uses.
*/
TARGET ProcMissionManager::Find_Existing_Target(HouseClass * house, StructType type, CELL center, int maxdistance) const
{
    if (house == NULL || center == -1) return TARGET_NONE;

    BuildingClass * best = NULL;
    int bestdist = 0;
    for (int i = 0; i < Buildings.Count(); i++) {
        BuildingClass * building = Buildings.Ptr(i);
        if ((HouseClass *)building->House != house || building->Class->Type != type || building->IsInLimbo) continue;

        TARGET target = building->As_Target();
        bool taken = false;
        for (int o = 0; o < ObjectiveCount; o++) {
            if (Objectives[o].Target == target) taken = true;
        }
        if (taken) continue;

        int dist = Cell_Dist(Coord_Cell(building->Center_Coord()), center);
        if (dist > maxdistance) continue;
        if (best == NULL || dist < bestdist) {
            best = building;
            bestdist = dist;
        }
    }
    return best ? best->As_Target() : TARGET_NONE;
}

/*
**	The first of these types (tried from a random one on) that the house already has a free
**	building of, near 'center'.
*/
TARGET ProcMissionManager::Find_Existing_Any(HouseClass * house, StructType const * types, int count, CELL center, int maxdistance, StructType & found) const
{
    int start = (count > 1) ? Random_Pick(0, count - 1) : 0;
    for (int c = 0; c < count; c++) {
        StructType type = types[(start + c) % count];
        TARGET target = Find_Existing_Target(house, type, center, maxdistance);
        if (target != TARGET_NONE) {
            found = type;
            return target;
        }
    }
    return TARGET_NONE;
}

/*
**	Somewhere away from every base. Bases spread well past their centre (defence rings reach
**	~13 cells out), so the enemies' actual buildings must be clear too: a camp or outpost
**	under a main base's turrets is a death trap for a squad.
*/
CELL ProcMissionManager::Find_Neutral_Location(int mindistance, HouseClass const * nearto) const
{
    // With 'nearto', the suitable spot closest to that house (e.g. a squad's foothold).
    CELL best = -1;
    int bestdist = 0;
    CELL home = (nearto != NULL) ? Home_Cell(nearto) : -1;

    for (int attempt = 0; attempt < 300; attempt++) {
        if (best != -1 && (attempt % 60) == 0) break;      // Keep the best spot of the strictest tier that had one
        int relax = (attempt / 60) * 4;                     // Relax the distances on crowded maps
        int needed = mindistance - relax;
        int clearance = max(8, PROC_NEUTRAL_CLEARANCE - relax / 2);
        CELL cell = Clamp_To_Map(Map.MapCellX + Random_Pick(2, Map.MapCellWidth - 3), Map.MapCellY + Random_Pick(2, Map.MapCellHeight - 3));
        if (!Map[cell].Is_Clear_To_Build(SPEED_NONE)) continue;

        bool ok = true;
        for (int i = 0; i < PlayerTeamCount && ok; i++) {
            if (Cell_Dist(cell, Home_Cell(PlayerTeam[i])) < needed) ok = false;
        }
        for (int i = 0; i < EnemyCount && ok; i++) {
            if (Cell_Dist(cell, Enemies[i].Home) < needed) ok = false;
        }
        for (int i = 0; i < Buildings.Count() && ok; i++) {
            BuildingClass const * building = Buildings.Ptr(i);
            HouseClass const * house = building->House;
            if (building->IsInLimbo || house == NULL || Is_Player_Team(house) || house->Class->House == HOUSE_NEUTRAL) continue;
            if (Cell_Dist(cell, Coord_Cell(building->Center_Coord())) < clearance) ok = false;
        }
        if (!ok) continue;
        if (home == -1) return cell;

        int dist = Cell_Dist(cell, home);
        if (best == -1 || dist < bestdist) {
            best = cell;
            bestdist = dist;
        }
    }

    // A small map may have no spot that far out (a squad's far target): settle for the usual distance.
    if (best == -1 && mindistance > 22) return Find_Neutral_Location(22, nearto);
    return best;
}

/*
**	Small fortified enemy posts (Role=Sentry presets) scattered between the bases, so the map
**	isn't empty on the way to the enemy: a turret or two with a few guards, held by the
**	nearest enemy. Placed after the objective targets so they never take a target's spot.
**	Not in squad missions, whose patrols and targets are balanced on their own.
*/
void ProcMissionManager::Place_Sentries()
{
    if (!Settings.IsSentries || IsSquad || EnemyCount == 0 || Diff.SentriesMax <= 0) return;

    char buf[160];
    int wanted = Random_Pick(Diff.SentriesMin, Diff.SentriesMax);

    // Keep clear of the objective targets and outposts, and of each other.
    CELL taken[PROC_MAX_OBJECTIVES * PROC_MAX_POINTS + PROC_MAX_SENTRIES];
    int takencount = 0;
    for (int o = 0; o < ObjectiveCount; o++) {
        if (Objectives[o].Target != TARGET_NONE && takencount < ARRAY_SIZE(taken)) taken[takencount++] = As_Cell(Objectives[o].Target);
        for (int p = 1; p < Objectives[o].PointCount && takencount < ARRAY_SIZE(taken); p++) taken[takencount++] = Objectives[o].Points[p];
    }

    int placed = 0;
    for (int s = 0; s < wanted; s++) {
        CELL center = Find_Sentry_Location(taken, takencount);
        if (center == -1) break;

        int enemy = -1;
        int ownerdist = 0;
        for (int i = 0; i < EnemyCount; i++) {
            int dist = Cell_Dist(center, Enemies[i].Home);
            if (enemy == -1 || dist < ownerdist) {
                enemy = i;
                ownerdist = dist;
            }
        }
        HouseClass * owner = Enemies[enemy].House;
        ProcBasePreset const * preset = ProcPresets.Pick_Base(Proc_Side_Of(owner), PROC_BASE_SENTRY, owner->Control.TechLevel, Difficulty);
        if (preset == NULL) break;

        int before = owner->CurBuildings;
        Build_Base(owner, preset, false, center, true);
        taken[takencount++] = center;
        placed++;

        // The director notices when a post with buildings is wiped out.
        if ((int)owner->CurBuildings > before && SentryCount < PROC_MAX_SENTRIES) {
            SentryCells[SentryCount] = center;
            SentryOwners[SentryCount] = owner;
            IsSentryLost[SentryCount] = false;
            SentryCount++;
        }

        int playerdist = -1;
        for (int i = 0; i < PlayerTeamCount; i++) {
            int dist = Cell_Dist(center, Home_Cell(PlayerTeam[i]));
            if (playerdist == -1 || dist < playerdist) playerdist = dist;
        }
        sprintf(buf, "ProcMission: sentry '%s' (enemy %d, %s) at cell %d, %d buildings, %d cells from the player team, %d from its base",
            preset->Name, enemy, Proc_Side_Name(Proc_Side_Of(owner)), (int)center, owner->CurBuildings - before, playerdist, ownerdist);
        GlyphX_Debug_Print(buf);
    }
    sprintf(buf, "ProcMission: %d/%d sentries placed", placed, wanted);
    GlyphX_Debug_Print(buf);
}

/*
**	A sentry spot: buildable ground the player's team can drive to, well away from its bases,
**	clear of the enemy bases (a post under a main base's guns is just more base), and as far
**	from the other posts and targets as possible so they spread across the map.
*/
CELL ProcMissionManager::Find_Sentry_Location(CELL const * taken, int takencount) const
{
    int zone = -1;
    if (PlayerTeamCount > 0) zone = Map[Home_Cell(PlayerTeam[0])].Zones[MZONE_NORMAL];

    CELL best = -1;
    int bestscore = -1;
    for (int attempt = 0; attempt < 400; attempt++) {
        if (best != -1 && (attempt % 100) == 0) break;      // Keep the best spot of the strictest tier that had one
        int relax = (attempt / 100) * 3;                    // Relax the distances on crowded maps
        CELL cell = Clamp_To_Map(Map.MapCellX + Random_Pick(3, Map.MapCellWidth - 4), Map.MapCellY + Random_Pick(3, Map.MapCellHeight - 4));
        if (!Map[cell].Is_Clear_To_Build(SPEED_NONE)) continue;
        if (zone != -1 && Map[cell].Zones[MZONE_NORMAL] != zone) continue;

        bool ok = true;
        for (int i = 0; i < PlayerTeamCount && ok; i++) {
            if (Cell_Dist(cell, Home_Cell(PlayerTeam[i])) < 22 - relax) ok = false;
        }
        for (int i = 0; i < Buildings.Count() && ok; i++) {
            BuildingClass const * building = Buildings.Ptr(i);
            HouseClass const * house = building->House;
            if (building->IsInLimbo || house == NULL || house->Class->House == HOUSE_NEUTRAL) continue;
            int clearance = Is_Player_Team(house) ? 16 - relax : max(8, 14 - relax);
            if (Cell_Dist(cell, Coord_Cell(building->Center_Coord())) < clearance) ok = false;
        }
        int score = 1000;
        for (int t = 0; t < takencount && ok; t++) {
            int dist = Cell_Dist(cell, taken[t]);
            if (dist < 14 - relax) ok = false;
            score = min(score, dist);
        }
        if (!ok) continue;
        if (takencount == 0) return cell;

        if (score > bestscore) {
            best = cell;
            bestscore = score;
        }
    }
    return best;
}


/*******************************************************************************
 * OBJECTIVES
 ******************************************************************************/

void ProcMissionManager::Place_Objective_Targets(int first)
{
    char buf[160];
    ProcObjectivePreset const * misfits[6];     // Bonuses that did not fit this map
    int misfitcount = 0;

    // 'first': only the objectives from there on (an event's objective, added mid-mission).
    for (int i = first; i < ObjectiveCount; i++) {
        ProcObjective & obj = Objectives[i];
        ProcObjectivePreset const * preset = obj.Preset;

        // Control: the minutes are how long the points must be held, not a time limit; its clock
        // only shows while they are (Update_Control).
        if (obj.Minutes > 0 && preset->Kind != PROC_OBJ_CONTROL) {
            obj.EndFrame = Elapsed + obj.Minutes * TICKS_PER_MINUTE;
            if (obj.IsPrimary) {
                // The primary objective's clock is shown on the sidebar.
                Scen.MissionTimer = obj.Minutes * TICKS_PER_MINUTE;
                Scen.MissionTimer.Start();
            }
        }

        bool unitkind = (preset->Kind == PROC_OBJ_ASSASSINATE || preset->Kind == PROC_OBJ_RESCUE || preset->Kind == PROC_OBJ_CONTROL);
        if (preset->TargetCount == 0 && !unitkind) continue;

        /*
        **	Pick the owner and the area for the target structure.
        */
        HouseClass * owner = NULL;
        CELL center = -1;
        int minradius = 3;
        int maxradius = 10;
        int enemyindex = 0;

        // A squad mission's own bonus (e.g. the construction yard to capture) is the squad's
        // foothold: as close to the squad as is safe, not across the map past the patrols.
        ProcObjectivePreset const * primary = Objectives[0].Preset;
        bool foothold = IsSquad && !obj.IsPrimary && primary->Bonus[0] != '\0' && stricmp(primary->Bonus, preset->Name) == 0;
        HouseClass const * nearto = foothold ? Commander : NULL;

        // Any other target in a squad mission is the far end of the trip, well away from where
        // the squad starts (its patrols would otherwise be on the squad straight away).
        int faraway = (IsSquad && nearto == NULL) ? 20 : 0;

        // A unit to kill, prisoners to free or points to hold: placed their own way.
        if (unitkind) {
            if (!Place_Unit_Objective(obj, faraway, nearto)) {
                sprintf(buf, "ProcMission: could not place target for objective %s", preset->Name);
                GlyphX_Debug_Print(buf);
                obj.Target = TARGET_NONE;
                if (Replace_Misfit(obj, misfits, misfitcount)) {
                    i--;
                    continue;
                }
                obj.State = PROC_STATE_FAILED;
            }
            continue;
        }

        switch (preset->Owner) {
            case PROC_OWNER_ENEMY: {
                enemyindex = Random_Pick(0, EnemyCount - 1);
                ProcHouseInfo const & enemy = Enemies[enemyindex];
                owner = enemy.House;
                center = enemy.Home;
                minradius = 4;
                maxradius = 12;
                break;
            }
            case PROC_OWNER_NEUTRAL:
                owner = HouseClass::As_Pointer(HOUSE_NEUTRAL);
                center = Find_Neutral_Location(22 + faraway, nearto);
                minradius = 0;
                maxradius = 6;
                break;
            case PROC_OWNER_PLAYER:
                owner = Commander;
                center = Home_Cell(Commander);
                minradius = 3;
                maxradius = 9;
                break;
            case PROC_OWNER_OUTPOST:
                owner = Enemies[Random_Pick(0, EnemyCount - 1)].House;
                center = Find_Neutral_Location(20 + faraway, nearto);
                minradius = 0;
                maxradius = 5;
                break;
            case PROC_OWNER_ALLY: {
                // One of an AI ally's own buildings (an ally in distress): the ally whose base is worst hit.
                int worst = 101;
                for (int h = 0; h < PlayerTeamCount; h++) {
                    HouseClass * ally = PlayerTeam[h];
                    if (ally->IsHuman || !Is_Alive(ally) || ally->CurBuildings == 0) continue;
                    long strength = 0, maximum = 0;
                    for (int b = 0; b < Buildings.Count(); b++) {
                        BuildingClass const * building = Buildings.Ptr(b);
                        if ((HouseClass *)building->House != ally || building->IsInLimbo || building->Strength <= 0) continue;
                        strength += building->Strength;
                        maximum += building->Class->MaxStrength;
                    }
                    int health = maximum ? (int)(strength * 100 / maximum) : 100;
                    if (health < worst) {
                        worst = health;
                        owner = ally;
                    }
                }
                if (owner == NULL) break;
                center = Home_Cell(owner);
                minradius = 3;
                maxradius = 9;
                break;
            }
            default:
                break;
        }

        /*
        **	Prefer a target type the owner could really have at this tech level.
        */
        StructType candidates[PROC_MAX_TARGET_TYPES];
        int candidatecount = 0;
        for (int t = 0; t < preset->TargetCount; t++) {
            BuildingTypeClass const & btype = BuildingTypeClass::As_Reference(preset->Targets[t]);
            if (owner == NULL || preset->Owner == PROC_OWNER_NEUTRAL || Is_Allowed(btype, owner)) {
                candidates[candidatecount++] = preset->Targets[t];
            }
        }
        if (candidatecount == 0) {
            memcpy(candidates, preset->Targets, sizeof(StructType) * preset->TargetCount);
            candidatecount = preset->TargetCount;
        }
        obj.TargetType = candidates[Random_Pick(0, candidatecount - 1)];

        // A base target is the base's own building when it has one of the allowed types
        // ("destroy their refinery", "keep your power plant online"); a new one is only added
        // when it has none (crowded bases may have no room).
        bool existing = false;
        bool inbase = (preset->Owner == PROC_OWNER_ENEMY || preset->Owner == PROC_OWNER_PLAYER || preset->Owner == PROC_OWNER_ALLY);
        if (inbase) {
            // Any of the base's own buildings: a crowded base spreads up to 12 cells further out.
            obj.Target = Find_Existing_Any(owner, candidates, candidatecount, center, maxradius + 12, obj.TargetType);
            existing = (obj.Target != TARGET_NONE);
        }

        // Away from the bases a blocked spot (cliffs, water, trees) just means trying elsewhere.
        bool placed = existing;
        bool elsewhere = (preset->Owner == PROC_OWNER_NEUTRAL || preset->Owner == PROC_OWNER_OUTPOST);
        for (int attempt = 0; !placed && owner != NULL && center != -1 && attempt < 5; attempt++) {
            // An abandoned target belongs to no one: the AI neither repairs nor defends it, and
            // units only shoot it when told to (an attack-moving squad would otherwise lock on to it).
            HouseClass * builder = preset->IsAbandoned ? HouseClass::As_Pointer(HOUSE_NEUTRAL) : owner;
            placed = Place_Building(builder, obj.TargetType, center, minradius, maxradius, &obj.Target, PROC_TARGET_REACH);
            if (placed) break;
            if (elsewhere) {
                center = Find_Neutral_Location((preset->Owner == PROC_OWNER_NEUTRAL ? 22 : 20) + faraway, nearto);
            } else if (preset->Owner == PROC_OWNER_ENEMY && attempt + 1 < EnemyCount) {
                // No room in this enemy's base (or its building is taken): try the next enemy.
                enemyindex = (enemyindex + 1) % EnemyCount;
                owner = Enemies[enemyindex].House;
                center = Enemies[enemyindex].Home;
                obj.Target = Find_Existing_Any(owner, candidates, candidatecount, center, maxradius + 12, obj.TargetType);
                placed = existing = (obj.Target != TARGET_NONE);
            } else {
                break;
            }
        }

        if (!placed) {
            sprintf(buf, "ProcMission: could not place target for objective %s", preset->Name);
            GlyphX_Debug_Print(buf);
            obj.Target = TARGET_NONE;
            if (Replace_Misfit(obj, misfits, misfitcount)) {
                i--;
                continue;
            }
            obj.State = PROC_STATE_FAILED;
            continue;
        }

        if (preset->Owner == PROC_OWNER_OUTPOST) {
            Build_Outpost(owner, preset, center);
        }

        // A target added to a base must not leave it short of power.
        if (!existing && inbase) {
            Add_Power(owner, center);
        }

        // A damaged target (e.g. a construction yard one engineer can capture).
        if (preset->TargetStrength < 100) {
            BuildingClass * building = As_Building(obj.Target);
            if (building != NULL) {
                building->Strength = max(1, (int)building->Class->MaxStrength * preset->TargetStrength / 100);
            }
        }

        /*
        **	Garrison. Neutral targets are guarded by an enemy so the guards actually fight.
        */
        Place_Guard(preset, (preset->Owner == PROC_OWNER_NEUTRAL) ? Enemies[Random_Pick(0, EnemyCount - 1)].House : owner, As_Cell(obj.Target));

        // Patrols (squad missions) guard the enemy and neutral targets, but not a capture target:
        // once captured it is the player's, and a patrol looping through it would wreck it.
        if (preset->Owner != PROC_OWNER_PLAYER && preset->Kind != PROC_OBJ_CAPTURE) {
            HouseClass * patroller = (preset->Owner == PROC_OWNER_NEUTRAL) ? Enemies[Random_Pick(0, EnemyCount - 1)].House : owner;
            Add_Anchor(As_Cell(obj.Target), patroller);
        }

        sprintf(buf, "ProcMission: objective %s target %s at cell %d (%d cells from the area centre, radius %d%s)", preset->Name,
            BuildingTypeClass::As_Reference(obj.TargetType).IniName, (int)As_Cell(obj.Target),
            Cell_Dist(As_Cell(obj.Target), center), maxradius + (existing ? 12 : PROC_TARGET_REACH), existing ? ", existing building" : "");
        GlyphX_Debug_Print(buf);
    }

    /*
    **	A primary objective without its target is impossible; fall back to destroying the enemy.
    */
    if (first == 0 && ObjectiveCount > 0 && Objectives[0].State == PROC_STATE_FAILED) {
        GlyphX_Debug_Print("ProcMission: primary objective unusable, falling back to destroy all enemies");
        ProcObjectivePreset const * fallback = NULL;
        for (int i = 0; i < ProcPresets.ObjectiveCount && fallback == NULL; i++) {
            if (ProcPresets.Objectives[i].Kind == PROC_OBJ_DESTROY_BASES) fallback = &ProcPresets.Objectives[i];
        }
        Scen.MissionTimer.Stop();
        if (fallback != NULL) {
            Objectives[0] = ProcObjective();
            Objectives[0].Preset = fallback;
            Objectives[0].IsPrimary = true;
        }
    }
}

/*
**	A bonus that doesn't fit this map is swapped for another one (true: 'obj' now holds it,
**	to be placed again). A primary is left to the destroy-all fallback.
*/
bool ProcMissionManager::Replace_Misfit(ProcObjective & obj, ProcObjectivePreset const ** misfits, int & misfitcount)
{
    if (obj.IsPrimary || obj.IsEvent || misfitcount >= 6) return false;

    ProcObjectivePreset const * preset = obj.Preset;
    misfits[misfitcount++] = preset;
    ProcObjectivePreset const * exclude[PROC_MAX_OBJECTIVES + 6];
    int excludecount = 0;
    for (int o = 0; o < ObjectiveCount; o++) exclude[excludecount++] = Objectives[o].Preset;
    for (int m = 0; m < misfitcount; m++) exclude[excludecount++] = misfits[m];
    ProcObjectivePreset const * other = ProcPresets.Pick_Objective(PlayerSide, false, TechLevel, IsPlayerBase, !IsSquad, exclude, excludecount);
    if (other == NULL) return false;

    char buf[160];
    sprintf(buf, "ProcMission: secondary objective %s replaced by %s", preset->Name, other->Name);
    GlyphX_Debug_Print(buf);
    obj = ProcObjective();
    obj.Preset = other;
    obj.Minutes = Random_Pick(other->MinMinutes, other->MaxMinutes);
    return true;
}

/*
**	The objective's Guard= team around 'cell'. Enemy guards are scaled by the difficulty.
*/
void ProcMissionManager::Place_Guard(ProcObjectivePreset const * preset, HouseClass * house, CELL cell, CELL moveto)
{
    ProcTeamPreset const * guard = (preset->Guard[0] != '\0') ? ProcPresets.Find_Team(preset->Guard) : NULL;
    if (guard == NULL || house == NULL || cell == -1) return;

    MissionType mission = (house == Commander) ? MISSION_GUARD : MISSION_GUARD_AREA;
    for (int m = 0; m < guard->MemberCount; m++) {
        if (!Is_Allowed(*guard->Members[m].Type, house)) continue;
        int count = guard->Members[m].Count;
        if (house != Commander) count = max(1, (count * Diff.GuardScalePct + 50) / 100);
        for (int n = 0; n < count; n++) {
            TechnoClass * unit = Spawn_One(house, guard->Members[m].Type, cell, 2, 7, mission);

            // Escorts travel with what they guard (a convoy).
            if (unit != NULL && moveto != -1) {
                unit->Assign_Mission(MISSION_MOVE);
                unit->Assign_Destination(::As_Target(Map.Nearby_Location(moveto, SPEED_TRACK)));
                unit->Commence();
            }
        }
    }
}

/*
**	Objectives about units and places rather than a structure:
**	  Assassinate  the target unit (TargetUnit=) in a camp away from the bases (Owner=Outpost,
**	               default) or inside an enemy base (Owner=Enemy), with its guards. From a camp
**	               it runs for its main base once the player's units come close or hurt it.
**	  Rescue       the prisoners (Prisoners=), held by no one and asleep, in an enemy camp.
**	               They must be brought back to the player's base (or a squad's start).
**	  Control      Points= spots between the bases, each held by enemy guards (Guard=) and
**	               marked by a neutral building from Targets= when one fits.
*/
bool ProcMissionManager::Place_Unit_Objective(ProcObjective & obj, int faraway, HouseClass const * nearto)
{
    char buf[200];
    ProcObjectivePreset const * preset = obj.Preset;
    if (EnemyCount == 0) return false;

    if (preset->Kind == PROC_OBJ_CONTROL) {
        CELL taken[PROC_MAX_OBJECTIVES + PROC_MAX_POINTS];
        int takencount = 0;
        for (int o = 0; o < ObjectiveCount; o++) {
            if (&Objectives[o] != &obj && Objectives[o].Target != TARGET_NONE) taken[takencount++] = As_Cell(Objectives[o].Target);
        }
        for (int p = 0; p < preset->PointCount; p++) {
            CELL cell = Find_Sentry_Location(taken, takencount);
            if (cell == -1) break;
            taken[takencount++] = cell;
            obj.Points[obj.PointCount] = cell;
            obj.IsPointHeld[obj.PointCount] = false;
            obj.PointCount++;

            // Held by the nearest enemy to begin with.
            int enemy = 0;
            for (int e = 1; e < EnemyCount; e++) {
                if (Cell_Dist(cell, Enemies[e].Home) < Cell_Dist(cell, Enemies[enemy].Home)) enemy = e;
            }
            HouseClass * neutral = HouseClass::As_Pointer(HOUSE_NEUTRAL);
            TARGET marker = TARGET_NONE;
            for (int t = 0; t < preset->TargetCount && marker == TARGET_NONE; t++) {
                Place_Building(neutral, preset->Targets[(t + p) % preset->TargetCount], cell, 0, 2, &marker, PROC_TARGET_REACH);
            }
            Place_Guard(preset, Enemies[enemy].House, cell);
            Add_Anchor(cell, Enemies[enemy].House);

            int playerdist = 1000;
            for (int h = 0; h < PlayerTeamCount; h++) playerdist = min(playerdist, Cell_Dist(cell, Home_Cell(PlayerTeam[h])));
            sprintf(buf, "ProcMission: objective %s: control point %c at cell %d (%d cells from the player team, marker %s, held by enemy %d)",
                preset->Name, 'A' + p, (int)cell, playerdist, marker != TARGET_NONE ? As_Building(marker)->Class->IniName : "none", enemy);
            GlyphX_Debug_Print(buf);
        }
        if (obj.PointCount < min(2, preset->PointCount)) return false;
        obj.Target = ::As_Target(obj.Points[0]);
        return true;
    }

    /*
    **	A convoy drives from the map edge nearest the player's team to an enemy base, right across
    **	the map, with its escorts; it must be stopped before it gets there.
    */
    if (preset->Kind == PROC_OBJ_ASSASSINATE && preset->IsConvoy) {
        int enemy = Random_Pick(0, EnemyCount - 1);
        for (int e = 0; e < EnemyCount && Enemies[enemy].House->CurBuildings == 0; e++) enemy = (enemy + 1) % EnemyCount;
        HouseClass * owner = Enemies[enemy].House;
        CELL destination = Map.Nearby_Location(Enemies[enemy].Home, SPEED_WHEEL);
        CELL start = Find_Edge_Cell(Home_Cell(Commander), MZONE_NORMAL, SPEED_WHEEL);
        if (start == -1 || destination <= 0 || Map[start].Zones[MZONE_NORMAL] != Map[destination].Zones[MZONE_NORMAL]) return false;
        if (Cell_Dist(start, destination) < 30) return false;

        ProcTechnoCount const & pick = preset->TargetUnits[Random_Pick(0, preset->TargetUnitCount - 1)];
        TechnoClass * target = Spawn_One(owner, pick.Type, start, 0, 2, MISSION_GUARD);
        if (target == NULL) return false;
        target->Assign_Mission(MISSION_MOVE);
        target->Assign_Destination(::As_Target(destination));
        target->Commence();
        obj.Target = target->As_Target();
        obj.UnitType = pick.Type;
        obj.Retreat = destination;
        obj.IsFleeing = true;
        Place_Guard(preset, owner, start, destination);

        CELL cell = Coord_Cell(target->Center_Coord());
        sprintf(buf, "ProcMission: objective %s target %s at cell %d (%d cells from the area centre, radius %d), convoy to cell %d (%d cells)",
            preset->Name, pick.Type->IniName, (int)cell, Cell_Dist(cell, start), 5, (int)destination, Cell_Dist(cell, destination));
        GlyphX_Debug_Print(buf);
        return true;
    }

    // A camp away from the bases (or just a spot, Owner=Neutral), or a spot inside an enemy base.
    int enemyindex = Random_Pick(0, EnemyCount - 1);
    HouseClass * owner = Enemies[enemyindex].House;
    CELL center = -1;
    bool camp = (preset->Owner != PROC_OWNER_ENEMY);
    for (int attempt = 0; attempt < 5 && center == -1; attempt++) {
        center = camp ? Find_Neutral_Location(20 + faraway, nearto) : Map.Nearby_Location(Toward(Enemies[enemyindex].Home, Home_Cell(Commander), 4), SPEED_FOOT);
        if (center != -1 && !Map[center].Is_Clear_To_Move(SPEED_FOOT, false, false)) center = -1;
    }
    if (center == -1) return false;
    if (preset->Owner == PROC_OWNER_OUTPOST) Build_Outpost(owner, preset, center);

    if (preset->Kind == PROC_OBJ_ASSASSINATE) {
        ProcTechnoCount const & pick = preset->TargetUnits[Random_Pick(0, preset->TargetUnitCount - 1)];
        TechnoClass * target = Spawn_One(owner, pick.Type, center, 0, 2, MISSION_GUARD);
        if (target == NULL) target = Spawn_One(owner, pick.Type, center, 0, 5, MISSION_GUARD);
        if (target == NULL) return false;
        obj.Target = target->As_Target();
        obj.UnitType = pick.Type;
        obj.Retreat = camp ? Enemies[enemyindex].Home : -1;
        Place_Guard(preset, owner, center);
        Add_Anchor(center, owner);

        CELL cell = Coord_Cell(target->Center_Coord());
        sprintf(buf, "ProcMission: objective %s target %s at cell %d (%d cells from the area centre, radius %d), runs to cell %d",
            preset->Name, pick.Type->IniName, (int)cell, Cell_Dist(cell, center), 5, (int)obj.Retreat);
        GlyphX_Debug_Print(buf);
        return true;
    }

    // Rescue
    HouseClass * neutral = HouseClass::As_Pointer(HOUSE_NEUTRAL);
    for (int i = 0; i < preset->PrisonerCount; i++) {
        for (int n = 0; n < preset->Prisoners[i].Count && obj.PrisonerCount < PROC_MAX_PRISONERS; n++) {
            TechnoClass * prisoner = Spawn_One(neutral, preset->Prisoners[i].Type, center, 0, 2, MISSION_SLEEP);
            if (prisoner == NULL) continue;
            obj.Prisoners[obj.PrisonerCount] = prisoner->As_Target();
            obj.PrisonerTypes[obj.PrisonerCount] = preset->Prisoners[i].Type;
            obj.PrisonerState[obj.PrisonerCount] = PROC_PRISONER_HELD;
            obj.PrisonerCount++;
        }
    }
    if (obj.PrisonerCount == 0) return false;
    obj.Needed = (preset->RescueNeeded > 0) ? min(preset->RescueNeeded, obj.PrisonerCount) : obj.PrisonerCount;
    obj.Extraction = Home_Cell(Commander);
    obj.Target = ::As_Target(center);
    Place_Guard(preset, owner, center);
    Add_Anchor(center, owner);

    sprintf(buf, "ProcMission: objective %s: %d prisoners at cell %d (%d needed), extraction at cell %d (%d cells away)",
        preset->Name, obj.PrisonerCount, (int)center, obj.Needed, (int)obj.Extraction, Cell_Dist(center, obj.Extraction));
    GlyphX_Debug_Print(buf);
    return true;
}

/*
**	Display names for reward units (the DLL has no access to the client's localised names).
**	Anything not listed shows its INI name.
*/
static char const * Unit_Name(TechnoTypeClass const * type, int count)
{
    static struct { char const * IniName; char const * One; char const * Many; } const s_Names[] = {
        {"E1", "Rifle Infantry", "Rifle Infantry"}, {"E2", "Grenadier", "Grenadiers"}, {"E3", "Rocket Soldier", "Rocket Soldiers"},
        {"E4", "Flamethrower", "Flamethrowers"}, {"E6", "Engineer", "Engineers"}, {"E7", "Tanya", "Tanyas"}, {"MEDI", "Medic", "Medics"},
        {"DOG", "Attack Dog", "Attack Dogs"}, {"SPY", "Spy", "Spies"}, {"THF", "Thief", "Thieves"}, {"SHOK", "Shock Trooper", "Shock Troopers"},
        {"MECH", "Mechanic", "Mechanics"}, {"1TNK", "Light Tank", "Light Tanks"}, {"2TNK", "Medium Tank", "Medium Tanks"},
        {"3TNK", "Heavy Tank", "Heavy Tanks"}, {"4TNK", "Mammoth Tank", "Mammoth Tanks"}, {"V2RL", "V2 Rocket Launcher", "V2 Rocket Launchers"},
        {"ARTY", "Artillery", "Artillery"}, {"JEEP", "Ranger", "Rangers"}, {"APC", "APC", "APCs"}, {"MNLY", "Minelayer", "Minelayers"},
        {"HARV", "Ore Truck", "Ore Trucks"}, {"MCV", "MCV", "MCVs"}, {"TTNK", "Tesla Tank", "Tesla Tanks"}, {"CTNK", "Chrono Tank", "Chrono Tanks"},
        {"MRJ", "Radar Jammer", "Radar Jammers"}, {"MGG", "Mobile Gap Generator", "Mobile Gap Generators"}, {"STNK", "Phase Transport", "Phase Transports"},
        {"QTNK", "M.A.D. Tank", "M.A.D. Tanks"}, {"DTRK", "Demolition Truck", "Demolition Trucks"}, {"FTRK", "Flak Truck", "Flak Trucks"},
    };
    for (int i = 0; i < ARRAY_SIZE(s_Names); i++) {
        if (stricmp(s_Names[i].IniName, type->IniName) == 0) return (count == 1) ? s_Names[i].One : s_Names[i].Many;
    }
    return type->IniName;
}

// "$3000, 3 Medium Tanks and a map reveal" (empty if the preset gives nothing).
static void Reward_Text(ProcObjectivePreset const * preset, char * buffer, int size)
{
    if (preset->RewardText[0] != '\0') {
        _snprintf(buffer, size, "%s", preset->RewardText);
        buffer[size - 1] = '\0';
        return;
    }

    char const * parts[PROC_MAX_MEMBERS + 2];
    char storage[PROC_MAX_MEMBERS + 2][48];
    int count = 0;
    if (preset->RewardCredits > 0) {
        sprintf(storage[count], "$%ld", preset->RewardCredits);
        parts[count] = storage[count];
        count++;
    }
    for (int i = 0; i < preset->RewardUnitCount; i++) {
        sprintf(storage[count], "%d %s", preset->RewardUnits[i].Count, Unit_Name(preset->RewardUnits[i].Type, preset->RewardUnits[i].Count));
        parts[count] = storage[count];
        count++;
    }
    if (preset->IsRewardReveal) parts[count++] = "the whole map revealed";

    int out = 0;
    buffer[0] = '\0';
    for (int i = 0; i < count && out < size - 1; i++) {
        char const * joint = (i == 0) ? "" : (i == count - 1) ? " and " : ", ";
        out += _snprintf(buffer + out, size - out, "%s%s", joint, parts[i]);
    }
    buffer[size - 1] = '\0';
}

void ProcMissionManager::Objective_Text(ProcObjective const & objective, char * buffer, int size) const
{
    char const * text = objective.Preset->Text;
    if (*text == '\0') text = objective.Preset->Name;

    // Only %d (minutes) is substituted; anything else is copied verbatim.
    int out = 0;
    out += _snprintf(buffer, size, "%s", objective.IsPrimary ? "Objective: " : "Bonus: ");
    for (char const * p = text; *p != '\0' && out < size - 8; p++) {
        if (p[0] == '%' && p[1] == 'd') {
            out += _snprintf(buffer + out, size - out, "%d", objective.Minutes);
            p++;
        } else {
            buffer[out++] = *p;
        }
    }
    buffer[min(out, size - 1)] = '\0';

    // Bonus objectives say what they are worth.
    if (!objective.IsPrimary) {
        char reward[160];
        Reward_Text(objective.Preset, reward, sizeof(reward));
        out = (int)strlen(buffer);
        if (reward[0] != '\0' && out < size - 12) {
            _snprintf(buffer + out, size - out, " Reward: %s.", reward);
            buffer[size - 1] = '\0';
        }
    }
}

void ProcMissionManager::Announce_Objectives()
{
    // Spectators see the whole battlefield.
    if (IsSpectating) {
        for (int h = 0; h < PlayerTeamCount; h++) {
            if (!PlayerTeam[h]->IsHuman) continue;
            for (CELL cell = 0; cell < MAP_CELL_TOTAL; cell++) {
                Map.Map_Cell(cell, PlayerTeam[h], false, false);
            }
        }
        Message("Spectating: your AI allies take on the enemy.");
    }

    for (int i = 0; i < ObjectiveCount; i++) {
        if (Objectives[i].State == PROC_STATE_ACTIVE) Announce_Objective(i);
    }
}

/*
**	Shows one objective: its text, and where it is (revealed around it, pinged on the radar).
*/
void ProcMissionManager::Announce_Objective(int i)
{
    char buf[PROC_TEXT_LEN + 192];
    Objective_Text(Objectives[i], buf, sizeof(buf));
    Message(buf);

    if (Objectives[i].Target != TARGET_NONE) {
        // Show the player where it is: reveal the area around it for every human on the team.
        for (int h = 0; h < PlayerTeamCount; h++) {
            if (!PlayerTeam[h]->IsHuman) continue;
            Map.Sight_From(As_Cell(Objectives[i].Target), PROC_REVEAL_RADIUS, PlayerTeam[h], false);
            for (int p = 1; p < Objectives[i].PointCount; p++) Map.Sight_From(Objectives[i].Points[p], PROC_REVEAL_RADIUS, PlayerTeam[h], false);
        }
        On_Ping(Commander, As_Coord(Objectives[i].Target));
        for (int p = 1; p < Objectives[i].PointCount; p++) On_Ping(Commander, Cell_Coord(Objectives[i].Points[p]));
    }
}

static BuildingClass * Live_Target(ProcObjective const & obj)
{
    if (obj.Target == TARGET_NONE) return NULL;

    // Fetch without the active check so the log can say why, but never read a deleted
    // building's fields: its slot is free and Class may no longer be valid.
    BuildingClass * building = As_Building(obj.Target, false);
    if (building == NULL || !building->IsActive) {
        GlyphX_Debug_Print("ProcMission: target gone (destroyed)");
        return NULL;
    }
    if (building->Class->Type != obj.TargetType) {
        GlyphX_Debug_Print("ProcMission: target gone (slot reused by another building)");
        return NULL;
    }
    if (building->IsInLimbo || building->Strength <= 0) {
        char dbg[128];
        sprintf(dbg, "ProcMission: target gone (limbo=%d strength=%d owner=%s)", (int)building->IsInLimbo,
            (int)building->Strength, building->House ? building->House->IniName : "?");
        GlyphX_Debug_Print(dbg);
        return NULL;
    }
    return building;
}

void ProcMissionManager::Update_Objectives()
{
    // A spectator owns nothing, so the team has lost once every AI ally is gone.
    if (IsSpectating && ObjectiveCount > 0 && Objectives[0].State == PROC_STATE_ACTIVE) {
        bool allies = false;
        for (int i = 0; i < PlayerTeamCount; i++) {
            if (!PlayerTeam[i]->IsHuman && Is_Alive(PlayerTeam[i])) allies = true;
        }
        if (!allies) {
            Fail_Objective(Objectives[0]);
            return;
        }
    }

    for (int i = 0; i < ObjectiveCount && !IsFinished; i++) {
        ProcObjective & obj = Objectives[i];
        if (obj.State != PROC_STATE_ACTIVE) continue;

        bool timeup = (obj.EndFrame != 0 && Elapsed >= obj.EndFrame);
        ProcObjectiveKindType kind = obj.Preset->Kind;
        bool structure = (kind == PROC_OBJ_DESTROY || kind == PROC_OBJ_CAPTURE || kind == PROC_OBJ_PROTECT);
        BuildingClass * target = structure ? Live_Target(obj) : NULL;

        switch (obj.Preset->Kind) {
            case PROC_OBJ_DESTROY_BASES: {
                bool anyalive = false;
                for (int e = 0; e < EnemyCount; e++) {
                    if (Is_Alive(Enemies[e].House)) anyalive = true;
                }
                if (!anyalive) Complete_Objective(obj);
                break;
            }

            case PROC_OBJ_SURVIVE:
                if (timeup) Complete_Objective(obj);
                break;

            case PROC_OBJ_DESTROY:
                if (target == NULL) Complete_Objective(obj);
                else if (timeup) Fail_Objective(obj);
                break;

            case PROC_OBJ_CAPTURE:
                if (target == NULL) Fail_Objective(obj);
                else if (Is_Player_Team(target->House)) {
                    // The damage was only there so one engineer could take it.
                    if (obj.Preset->TargetStrength < 100) target->Strength = target->Class->MaxStrength;
                    Complete_Objective(obj);
                }
                else if (timeup) Fail_Objective(obj);
                break;

            case PROC_OBJ_PROTECT:
                if (target == NULL || !Is_Player_Team(target->House)) Fail_Objective(obj);
                else if (timeup) Complete_Objective(obj);
                break;

            case PROC_OBJ_ASSASSINATE:
                Update_Assassinate(obj, timeup);
                break;

            case PROC_OBJ_RESCUE:
                Update_Rescue(obj, timeup);
                break;

            case PROC_OBJ_CONTROL:
                Update_Control(obj, timeup);
                break;

            default:
                break;
        }
    }
}

/*
**	The live unit behind 'target', or NULL once it is dead (or its slot holds something else).
*/
static TechnoClass * Live_Unit(TARGET target, TechnoTypeClass const * type, bool carried = false)
{
    if (target == TARGET_NONE) return NULL;
    TechnoClass * techno = As_Techno(target, false);
    if (techno == NULL || !techno->IsActive || techno->Strength <= 0) return NULL;
    if (techno->IsInLimbo && !carried) return NULL;     // 'carried': a passenger in a transport is in limbo too
    if (type != NULL && techno->Techno_Type_Class() != type) return NULL;
    return techno;
}

/*
**	Infantry and vehicles within 'radius' cells: the player's team's (only the humans' with
**	'humansonly'), or the enemies'.
*/
int ProcMissionManager::Count_Feet(CELL center, int radius, bool playerteam, bool humansonly) const
{
    int count = 0;
    for (int i = 0; i < Infantry.Count(); i++) {
        InfantryClass const * foot = Infantry.Ptr(i);
        if (foot->IsInLimbo || foot->Strength <= 0) continue;
        HouseClass const * house = foot->House;
        bool ours = Is_Player_Team(house);
        if (ours != playerteam || house->Class->House == HOUSE_NEUTRAL || (humansonly && !house->IsHuman)) continue;
        if (foot->Class->IsCivilian) continue;          // Freed prisoners hold nothing
        if (Cell_Dist(Coord_Cell(foot->Center_Coord()), center) <= radius) count++;
    }
    for (int i = 0; i < Units.Count(); i++) {
        UnitClass const * foot = Units.Ptr(i);
        if (foot->IsInLimbo || foot->Strength <= 0) continue;
        HouseClass const * house = foot->House;
        bool ours = Is_Player_Team(house);
        if (ours != playerteam || house->Class->House == HOUSE_NEUTRAL || (humansonly && !house->IsHuman)) continue;
        if (Cell_Dist(Coord_Cell(foot->Center_Coord()), center) <= radius) count++;
    }
    return count;
}

/*
**	Kill the target. From a camp it runs for its main base once the player's units close in
**	or hurt it, and digs in there: a hunt across the map rather than a sniping job.
*/
void ProcMissionManager::Update_Assassinate(ProcObjective & obj, bool timeup)
{
    TechnoClass * target = Live_Unit(obj.Target, obj.UnitType);
    if (target == NULL) {
        Complete_Objective(obj);
        return;
    }
    if (timeup) {
        Fail_Objective(obj);
        return;
    }

    // A convoy that gets through is lost; one that stopped (blocked, or done shooting back) drives on.
    if (obj.Preset->IsConvoy) {
        if (Cell_Dist(Coord_Cell(target->Center_Coord()), obj.Retreat) <= 4) {
            Message("The enemy convoy got through.");
            Fail_Objective(obj);
        } else if (target->Get_Mission() != MISSION_MOVE) {
            target->Assign_Mission(MISSION_MOVE);
            target->Assign_Destination(::As_Target(obj.Retreat));
            target->Commence();
        }
        return;
    }
    if (obj.IsFleeing || obj.Retreat == -1) return;

    CELL cell = Coord_Cell(target->Center_Coord());
    bool hurt = (int)target->Strength * 4 < (int)target->Techno_Type_Class()->MaxStrength * 3;
    if (!hurt && Count_Feet(cell, PROC_FLEE_RADIUS, true) == 0) return;

    obj.IsFleeing = true;
    SpeedType speed = (target->What_Am_I() == RTTI_INFANTRY) ? SPEED_FOOT : SPEED_TRACK;
    target->Assign_Mission(MISSION_MOVE);
    target->Assign_Target(TARGET_NONE);
    target->Assign_Destination(::As_Target(Map.Nearby_Location(obj.Retreat, speed)));
    target->Commence();
    On_Ping(Commander, target->Center_Coord());
    Message("The target is running for its base. Stop it!");

    char buf[128];
    sprintf(buf, "ProcMission: %s target flees from cell %d to cell %d (%s)", obj.Preset->Name, (int)cell, (int)obj.Retreat, hurt ? "hurt" : "spotted");
    GlyphX_Debug_Print(buf);
}

/*
**	Prisoners are freed when the humans' units reach them (they become the player's own),
**	and rescued once they are walked to the extraction point (they leave the map there).
*/
void ProcMissionManager::Update_Rescue(ProcObjective & obj, bool timeup)
{
    char buf[160];
    int alive = 0;
    for (int i = 0; i < obj.PrisonerCount; i++) {
        unsigned char & state = obj.PrisonerState[i];
        if (state == PROC_PRISONER_RESCUED || state == PROC_PRISONER_DEAD) continue;

        TechnoClass * prisoner = Live_Unit(obj.Prisoners[i], obj.PrisonerTypes[i], state == PROC_PRISONER_FREE);
        if (prisoner == NULL) {
            state = PROC_PRISONER_DEAD;
            sprintf(buf, "ProcMission: %s prisoner %d (%s) died", obj.Preset->Name, i, obj.PrisonerTypes[i]->IniName);
            GlyphX_Debug_Print(buf);
            continue;
        }
        CELL cell = Coord_Cell(prisoner->Center_Coord());

        if (state == PROC_PRISONER_HELD) {
            // The nearest human unit's owner takes charge of them.
            HouseClass * rescuer = NULL;
            for (int h = 0; h < PlayerTeamCount && rescuer == NULL; h++) {
                if (!PlayerTeam[h]->IsHuman) continue;
                for (int f = 0; f < Infantry.Count() && rescuer == NULL; f++) {
                    InfantryClass const * foot = Infantry.Ptr(f);
                    if ((HouseClass *)foot->House != PlayerTeam[h] || foot->IsInLimbo || foot->Class->IsCivilian) continue;
                    if (Cell_Dist(Coord_Cell(foot->Center_Coord()), cell) <= PROC_FREE_RADIUS) rescuer = PlayerTeam[h];
                }
                for (int f = 0; f < Units.Count() && rescuer == NULL; f++) {
                    UnitClass const * foot = Units.Ptr(f);
                    if ((HouseClass *)foot->House != PlayerTeam[h] || foot->IsInLimbo) continue;
                    if (Cell_Dist(Coord_Cell(foot->Center_Coord()), cell) <= PROC_FREE_RADIUS) rescuer = PlayerTeam[h];
                }
            }
            if (rescuer == NULL) {
                alive++;
                continue;
            }

            // Swap the neutral sleeper for one the rescuer controls.
            prisoner->Limbo();
            delete prisoner;
            TechnoClass * freed = (TechnoClass *)obj.PrisonerTypes[i]->Create_One_Of(rescuer);
            if (freed == NULL || !Scan_Place_Object(freed, cell)) {
                if (freed != NULL) delete freed;
                state = PROC_PRISONER_DEAD;
                continue;
            }
            freed->Assign_Mission(MISSION_GUARD);
            obj.Prisoners[i] = freed->As_Target();
            state = PROC_PRISONER_FREE;
            alive++;
            Message("Prisoner freed. Bring them back home!");
            sprintf(buf, "ProcMission: %s prisoner %d (%s) freed by %s at cell %d", obj.Preset->Name, i, obj.PrisonerTypes[i]->IniName, rescuer->IniName, (int)cell);
            GlyphX_Debug_Print(buf);
            continue;
        }

        // Free: home yet? (Riding in an APC or a Chinook counts once they get out.)
        if (!prisoner->IsInLimbo && Cell_Dist(cell, obj.Extraction) <= PROC_EXTRACT_RADIUS) {
            prisoner->Limbo();
            delete prisoner;
            state = PROC_PRISONER_RESCUED;
            obj.Rescued++;
            sprintf(buf, "Prisoner rescued (%d of %d).", obj.Rescued, obj.Needed);
            Message(buf);
            sprintf(buf, "ProcMission: %s prisoner %d rescued (%d/%d)", obj.Preset->Name, i, obj.Rescued, obj.Needed);
            GlyphX_Debug_Print(buf);
            continue;
        }
        alive++;
    }

    if (obj.Rescued >= obj.Needed) Complete_Objective(obj);
    else if (obj.Rescued + alive < obj.Needed || timeup) Fail_Objective(obj);
}

/*
**	A point belongs to whoever has units there alone (contested points keep their owner).
**	The clock runs while the player's team holds all of them, and pauses otherwise.
*/
void ProcMissionManager::Update_Control(ProcObjective & obj, bool timeup)
{
    char buf[128];
    bool all = (obj.PointCount > 0);
    for (int p = 0; p < obj.PointCount; p++) {
        int ours = Count_Feet(obj.Points[p], PROC_POINT_RADIUS, true);
        int theirs = Count_Feet(obj.Points[p], PROC_POINT_RADIUS, false);
        bool held = obj.IsPointHeld[p];
        if (ours > 0 && theirs == 0) held = true;
        if (theirs > 0 && ours == 0) held = false;
        if (held != obj.IsPointHeld[p]) {
            obj.IsPointHeld[p] = held;
            sprintf(buf, held ? "Control point %c taken." : "Control point %c lost to the enemy!", 'A' + p);
            Message(buf);
            sprintf(buf, "ProcMission: %s point %c %s (ours %d, theirs %d)", obj.Preset->Name, 'A' + p, held ? "taken" : "lost", ours, theirs);
            GlyphX_Debug_Print(buf);
        }
        if (!obj.IsPointHeld[p]) all = false;
    }

    long needed = (long)obj.Minutes * TICKS_PER_MINUTE;
    if (all) obj.HeldTicks += PROC_OBJECTIVE_CHECK_RATE;
    if (obj.IsPrimary) {
        // The sidebar clock shows the time left to hold, only while it is running.
        if (all) {
            Scen.MissionTimer = max(0L, needed - obj.HeldTicks);
            if (!Scen.MissionTimer.Is_Active()) Scen.MissionTimer.Start();
        } else if (Scen.MissionTimer.Is_Active()) {
            Scen.MissionTimer.Stop();
        }
    }

    if (obj.HeldTicks >= needed) Complete_Objective(obj);
    else if (timeup) Fail_Objective(obj);
}

/*
**	A control point the player's team holds, for a wave to take back (-1 = none).
*/
CELL ProcMissionManager::Contested_Point() const
{
    for (int i = 0; i < ObjectiveCount; i++) {
        ProcObjective const & obj = Objectives[i];
        if (obj.State != PROC_STATE_ACTIVE || obj.Preset->Kind != PROC_OBJ_CONTROL) continue;
        CELL held[PROC_MAX_POINTS];
        int count = 0;
        for (int p = 0; p < obj.PointCount; p++) {
            if (obj.IsPointHeld[p]) held[count++] = obj.Points[p];
        }
        if (count > 0) return held[Random_Pick(0, count - 1)];
    }
    return -1;
}

/*
**	A capture target placed damaged (TargetStrength) is held at that strength until it is
**	captured: its owner would repair it, and stray fire (the player's own attack-moving
**	squad included) would wreck the one building the mission needs.
**
**	In squad missions the enemy doesn't repair objective targets either: a squad without a
**	commando can't out-damage the AI's repairs, which would make the target unkillable.
*/
void ProcMissionManager::Hold_Targets()
{
    for (int i = 0; i < ObjectiveCount; i++) {
        ProcObjective & obj = Objectives[i];
        if (obj.State != PROC_STATE_ACTIVE) continue;
        bool hold = (obj.Preset->Kind == PROC_OBJ_CAPTURE && obj.Preset->TargetStrength < 100);
        bool norepair = hold || (IsSquad && obj.Preset->Kind == PROC_OBJ_DESTROY);
        if (!norepair) continue;

        BuildingClass * target = (obj.Target != TARGET_NONE) ? As_Building(obj.Target, false) : NULL;
        if (target == NULL || !target->IsActive || target->IsInLimbo || target->Class->Type != obj.TargetType) continue;
        if (target->Strength <= 0 || Is_Player_Team(target->House)) continue;

        if (target->IsRepairing) target->Repair(0);
        if (hold) {
            // Never above the starting damage; only topped back up when that is capturable anyway
            // (so an engineer's damage on a healthier target still counts).
            int cap = max(1, (int)target->Class->MaxStrength * obj.Preset->TargetStrength / 100);
            if (target->Strength > cap || fixed(cap, target->Class->MaxStrength) <= EngineerCaptureLevel) target->Strength = cap;
        } else {
            // The AI restarts repairs every frame; just never let the strength go back up.
            if (obj.HeldStrength > 0 && target->Strength > obj.HeldStrength) target->Strength = obj.HeldStrength;
            obj.HeldStrength = target->Strength;
        }
    }
}

void ProcMissionManager::Complete_Objective(ProcObjective & objective)
{
    char buf[PROC_TEXT_LEN + 208];
    objective.State = PROC_STATE_COMPLETE;

    Objective_Text(objective, buf, sizeof(buf));
    strcat(buf, " - COMPLETE");
    Message(buf);

    if (objective.IsPrimary) {
        Player_Wins();
    } else {
        Give_Reward(objective.Preset);
    }
}

void ProcMissionManager::Fail_Objective(ProcObjective & objective)
{
    char buf[PROC_TEXT_LEN + 208];
    objective.State = PROC_STATE_FAILED;

    Objective_Text(objective, buf, sizeof(buf));
    strcat(buf, " - FAILED");
    Message(buf);

    if (objective.IsPrimary) {
        Player_Loses();
    }
}

void ProcMissionManager::Give_Reward(ProcObjectivePreset const * preset)
{
    if (!Is_Alive(Commander)) return;

    char reward[160];
    Reward_Text(preset, reward, sizeof(reward));
    if (reward[0] != '\0') {
        char line[200];
        sprintf(line, "Reward received: %s.", reward);
        Message(line);
    }

    if (preset->IsRewardReveal) {
        for (int h = 0; h < PlayerTeamCount; h++) {
            if (!PlayerTeam[h]->IsHuman) continue;
            for (CELL cell = 0; cell < MAP_CELL_TOTAL; cell++) Map.Map_Cell(cell, PlayerTeam[h], false, false);
        }
    }

    if (preset->RewardCredits > 0) {
        Commander->Refund_Money(preset->RewardCredits);
    }

    if (preset->RewardUnitCount > 0) {
        CELL home = Home_Cell(Commander);
        int before = Commander->CurInfantry + Commander->CurUnits;
        for (int i = 0; i < preset->RewardUnitCount; i++) {
            Place_Units(Commander, preset->RewardUnits[i].Type, preset->RewardUnits[i].Count, home, MISSION_GUARD);
        }
        Speak(VOX_REINFORCEMENTS, Commander, Cell_Coord(home));

        char buf[128];
        sprintf(buf, "ProcMission: reward for %s: %d units at cell %d", preset->Name, Commander->CurInfantry + Commander->CurUnits - before, (int)home);
        GlyphX_Debug_Print(buf);
    }
}

/*
**	Skirmish games end through the multiplayer defeat logic: the game is over once every
**	house left alive is allied, or once no humans are left.
*/
void ProcMissionManager::Player_Wins()
{
    IsFinished = true;
    for (int i = 0; i < EnemyCount; i++) {
        if (Is_Alive(Enemies[i].House)) {
            Logic_Switch_Player_Context(Commander);
            Enemies[i].House->MPlayer_Defeated();
        }
    }
}

void ProcMissionManager::Player_Loses()
{
    IsFinished = true;
    for (int i = 0; i < PlayerTeamCount; i++) {
        if (PlayerTeam[i]->IsHuman && Is_Alive(PlayerTeam[i])) {
            Logic_Switch_Player_Context(Commander);
            PlayerTeam[i]->MPlayer_Defeated();
        }
    }
}


/*******************************************************************************
 * DIRECTOR
 ******************************************************************************/

ProcThreatType ProcMissionManager::Current_Threat() const
{
    int steps = (Elapsed - StartFrame) / (Diff.EscalateMinutes * TICKS_PER_MINUTE);

    // The harder the enemy is hit, the harder it hits back.
    int worst = 0;
    for (int i = 0; i < EnemyCount; i++) {
        int initial = Enemies[i].InitialBuildings;
        int lost = max(0, initial - Enemies[i].House->CurBuildings);
        worst = max(worst, (lost * 100) / initial);
    }
    steps += worst / 34;

    return (ProcThreatType)min((int)Diff.MaxThreat, (int)Diff.StartThreat + steps);
}

HouseClass * ProcMissionManager::Pick_Victim(HouseClass * attacker) const
{
    HouseClass * alive[PROC_MAX_HOUSES];
    int count = 0;
    for (int i = 0; i < PlayerTeamCount; i++) {
        if (Is_Alive(PlayerTeam[i]) && !attacker->Is_Ally(PlayerTeam[i]) && !Is_Spectator(PlayerTeam[i])) alive[count++] = PlayerTeam[i];
    }
    if (count == 0) return NULL;

    // Mostly go for the human, sometimes for an ally.
    if (Is_Alive(Commander) && !IsSpectating && Random_Pick(0, 99) < 60) return Commander;
    return alive[Random_Pick(0, count - 1)];
}

TeamTypeClass * ProcMissionManager::Make_Wave_Type(ProcTeamPreset const * preset, HouseClass * house, HouseClass * victim, CELL home)
{
    if (WaveTypeCount >= PROC_MAX_WAVE_TYPES || TeamTypes.Count() >= TeamTypes.Length()) return NULL;

    TeamTypeClass * tt = new TeamTypeClass();
    if (tt == NULL) return NULL;

    sprintf((char *)tt->IniName, "PROCW%03d", WaveCounter++ % 1000);
    tt->House = house->Class->House;
    tt->IsRoundAbout = preset->IsRoundAbout;
    tt->IsSuicide = preset->IsSuicide;
    tt->IsAutocreate = false;
    tt->IsPrebuilt = false;
    tt->IsReinforcable = false;
    tt->IsTransient = true;
    tt->RecruitPriority = 7;
    tt->InitNum = 0;
    tt->MaxAllowed = 1;
    tt->Origin = -1;

    tt->ClassCount = 0;
    for (int i = 0; i < preset->MemberCount && tt->ClassCount < TeamTypeClass::MAX_TEAM_CLASSCOUNT; i++) {
        tt->Members[tt->ClassCount].Class = preset->Members[i].Type;
        tt->Members[tt->ClassCount].Quantity = max(1, (preset->Members[i].Count * Diff.WaveSizePct + 50) / 100);
        tt->ClassCount++;
    }

    /*
    **	Gathering: form up just outside our own base, then stage outside the victim's
    **	base so the wave arrives together instead of trickling in.
    */
    int m = 0;
    if (preset->IsGathering && victim != NULL && !preset->IsNaval) {
        CELL target = Home_Cell(victim);
        CELL rally = Map.Nearby_Location(Toward(home, target, 6), SPEED_TRACK);
        CELL staging = Map.Nearby_Location(Toward(target, home, 12), SPEED_TRACK);
        if (rally > 0) {
            tt->MissionList[m].Mission = TMISSION_MOVECELL;
            tt->MissionList[m++].Data.Value = rally;
        }
        if (staging > 0) {
            tt->MissionList[m].Mission = TMISSION_MOVECELL;
            tt->MissionList[m++].Data.Value = staging;
        }
    }
    for (int i = 0; i < preset->MissionCount && m < TeamTypeClass::MAX_TEAM_MISSIONS; i++) {
        tt->MissionList[m].Mission = preset->Missions[i].Mission;
        tt->MissionList[m++].Data.Value = preset->Missions[i].Data;
    }
    /*
    **	Finish with Hunt. An Attack mission that finds no target ends the team's mission list,
    **	which disbands the team and leaves the units idling outside the victim's base. Hunt
    **	(a Do mission) never ends, so the wave keeps fighting and stays counted as active.
    */
    bool open_ended = (m > 0 && (tt->MissionList[m - 1].Mission == TMISSION_LOOP || tt->MissionList[m - 1].Mission == TMISSION_DO));
    if (!open_ended && m < TeamTypeClass::MAX_TEAM_MISSIONS) {
        tt->MissionList[m].Mission = TMISSION_DO;
        tt->MissionList[m++].Data.Value = MISSION_HUNT;
    }
    tt->MissionCount = m;

    WaveStart[WaveTypeCount] = Elapsed;
    WaveTypes[WaveTypeCount++] = tt;
    return tt;
}

bool ProcMissionManager::Deliver_Wave(TeamTypeClass * teamtype, CELL home)
{
    HouseClass * house = HouseClass::As_Pointer(teamtype->House);

    if (Settings.SpawnMode == PROC_SPAWN_EDGE) {
        // Do_Reinforcements drives the team in from the map edge nearest the origin waypoint.
        CELL saved = Scen.Waypoint[PROC_EDGE_WAYPOINT];
        Scen.Waypoint[PROC_EDGE_WAYPOINT] = home;
        teamtype->Origin = PROC_EDGE_WAYPOINT;
        bool ok = Do_Reinforcements(teamtype);
        teamtype->Origin = -1;
        Scen.Waypoint[PROC_EDGE_WAYPOINT] = saved;
        return ok;
    }

    // BASE mode: the team rolls out from next to the owning base.
    return Spawn_Team(teamtype, home, 3, 7);
}

/*
**	Creates a team of this type with its members placed around 'center' and sets it going.
*/
bool ProcMissionManager::Spawn_Team(TeamTypeClass * teamtype, CELL center, int minradius, int maxradius)
{
    HouseClass * house = HouseClass::As_Pointer(teamtype->House);
    if (house == NULL || center == -1) return false;

    TeamClass * team = new TeamClass(teamtype);
    if (team == NULL) return false;
    team->Force_Active();

    int placed = 0;
    for (int i = 0; i < teamtype->ClassCount; i++) {
        TechnoTypeClass const * type = teamtype->Members[i].Class;
        RTTIType rtti = type->What_Am_I();
        if (rtti == RTTI_AIRCRAFTTYPE) continue;

        for (int q = 0; q < teamtype->Members[i].Quantity; q++) {
            CELL cell = (rtti == RTTI_VESSELTYPE) ? Find_Water_Near(center, 24) : Random_Cell_Around(center, minradius, maxradius);
            if (cell == -1) break;

            FootClass * obj = (FootClass *)type->Create_One_Of(house);
            if (obj == NULL) break;

            // Join the team while still in limbo, as _Create_Group does. Once placed, the
            // unit idles in Guard Area, which teams refuse to recruit.
            ScenarioInit++;
            bool added = team->Add(obj);
            ScenarioInit--;
            if (!added) {
                delete obj;
                continue;
            }
            obj->IsInitiated = true;

            if (!Scan_Place_Object(obj, cell)) {
                delete obj;         // Also takes it back out of the team
                continue;
            }
            obj->Assign_Mission(MISSION_GUARD);
            placed++;
        }
    }

    if (placed == 0) {
        delete team;
        return false;
    }
    return true;
}

int ProcMissionManager::Active_Waves() const
{
    int count = 0;
    for (int i = 0; i < Teams.Count(); i++) {
        TeamClass * team = Teams.Ptr(i);
        if (!team->Class.Is_Valid()) continue;
        TeamTypeClass * tt = team->Class;
        for (int w = 0; w < WaveTypeCount; w++) {
            if (WaveTypes[w] == tt) {
                // Old waves keep fighting (they end in Hunt) but stop holding a slot, so a wave
                // stuck somewhere can't stall the director.
                if (Elapsed - WaveStart[w] < PROC_WAVE_ACTIVE_TIME) count++;
                break;
            }
        }
    }
    return count;
}

/*
**	Wave team types are made per wave. Delete the ones no live team uses any more, so a long
**	game never runs the TeamTypes heap (TEAMTYPE_MAX) dry.
*/
void ProcMissionManager::Collect_Wave_Types()
{
    for (int w = 0; w < WaveTypeCount; w++) {
        TeamTypeClass * tt = WaveTypes[w];
        bool used = false;
        for (int i = 0; i < Teams.Count() && !used; i++) {
            TeamClass * team = Teams.Ptr(i);
            if (team->Class.Is_Valid() && (TeamTypeClass *)team->Class == tt) used = true;
        }
        if (!used) {
            char buf[80];
            sprintf(buf, "ProcMission: wave %s over after %lds", tt->IniName, (Elapsed - WaveStart[w]) / TICKS_PER_SECOND);
            GlyphX_Debug_Print(buf);
            delete tt;

            --WaveTypeCount;
            WaveTypes[w] = WaveTypes[WaveTypeCount];
            WaveStart[w] = WaveStart[WaveTypeCount];
            w--;
        }
    }
}

static int Naval_Range(ProcTeamPreset const * preset);

/*
**	Sends a wave from this house. Events can pick the victim, send it to a spot (it moves there
**	and holds it) and make it 'boost' threat steps stronger than the director would.
*/
bool ProcMissionManager::Spawn_Wave(HouseClass * house, HouseClass * victim, CELL spot, int boost, char const * why)
{
    ProcThreatType high = (ProcThreatType)min((int)PROC_THREAT_COUNT - 1, (int)Current_Threat() + boost);
    ProcThreatType low = (ProcThreatType)max((int)PROC_THREAT_SCOUT, (int)high - 1);

    if (victim == NULL) victim = Pick_Victim(house);
    if (victim == NULL) return false;

    // Some waves come by sea, from a house that has a naval yard.
    CELL yard = (spot == -1) ? Naval_Yard_Cell(house) : -1;
    ProcTeamPreset const * preset = NULL;
    if (yard != -1 && Random_Pick(0, 99) < Diff.NavalChance) {
        preset = Pick_Naval_Team(house, victim, yard);
    }
    bool naval = (preset != NULL);
    if (preset == NULL) {
        preset = ProcPresets.Pick_Team(Proc_Side_Of(house), PROC_TEAM_ATTACK, low, high, house->Control.TechLevel);
    }
    if (preset == NULL) return false;

    CELL home = Home_Cell(house);
    TeamTypeClass * tt = Make_Wave_Type(preset, house, victim, home);
    if (tt == NULL) return false;

    /*
    **	Ships shell what their guns can reach from the water. Left to pick their own targets they
    **	go for the most valuable building, which may be far inland, and sit offshore for good.
    */
    if (naval && !preset->IsShipHunter) {
        TARGET targets[4];
        int count = Shell_Targets(yard, victim, Naval_Range(preset), targets, ARRAY_SIZE(targets));
        int m = 0;
        for (int i = 0; i < count; i++) {
            tt->MissionList[m].Mission = TMISSION_ATTACKTARCOM;
            tt->MissionList[m++].Data.Value = targets[i];
        }
        tt->MissionList[m].Mission = TMISSION_DO;
        tt->MissionList[m++].Data.Value = MISSION_HUNT;
        tt->MissionCount = m;
    }

    /*
    **	Control missions: most land waves go to retake a point the player holds, and stay to
    **	guard it (a Do mission never ends, so the team keeps the point).
    */
    CELL point = naval ? -1 : (spot != -1) ? spot : Contested_Point();
    if (point != -1 && (spot != -1 || Random_Pick(0, 99) < 70)) {
        CELL spot = Map.Nearby_Location(point, SPEED_TRACK);
        tt->MissionList[0].Mission = TMISSION_MOVECELL;
        tt->MissionList[0].Data.Value = spot;
        tt->MissionList[1].Mission = TMISSION_DO;
        tt->MissionList[1].Data.Value = MISSION_GUARD_AREA;
        tt->MissionCount = 2;
    } else {
        point = -1;
    }

    bool ok = naval ? Spawn_Team(tt, yard, 0, 0) : Deliver_Wave(tt, home);

    char buf[200];
    sprintf(buf, "ProcMission: wave %s (%s, threat %d%s%s%s%s) from %s -> %s", preset->Name, ok ? "sent" : "failed",
        (int)preset->Threat, naval ? ", by sea" : "", (point != -1) ? ((spot != -1) ? ", to a spot" : ", to a control point") : "",
        why ? ", event " : "", why ? why : "", house->IniName, victim->IniName);
    GlyphX_Debug_Print(buf);

    if (!ok) {
        WaveTypes[--WaveTypeCount] = NULL;
        delete tt;
        return false;
    }
    LastWaveFrame = Elapsed;
    return true;
}

/*
**	Naval waves: a house with a shipyard / sub pen can send ships, as long as they can reach the
**	victim. Ships that only fight ships (submarines) go for a victim that has a navy; the rest
**	shell a base within reach of the water: some building has to be in gun range of water the
**	ships can sail to, or they would sit offshore doing nothing.
*/
CELL ProcMissionManager::Naval_Yard_Cell(HouseClass const * house) const
{
    for (int i = 0; i < Buildings.Count(); i++) {
        BuildingClass const * building = Buildings.Ptr(i);
        if ((HouseClass const *)building->House != house || building->IsInLimbo || building->Strength <= 0) continue;
        if (*building == STRUCT_SHIP_YARD || *building == STRUCT_SUB_PEN) return Coord_Cell(building->Center_Coord());
    }
    return -1;
}

bool ProcMissionManager::Can_Reach_By_Sea(CELL yard, HouseClass const * victim) const
{
    CELL from = Find_Water_Near(yard, 6);
    CELL to = Find_Water_Near(Home_Cell(victim), PROC_NAVAL_REACH);
    if (from == -1 || to == -1) return false;
    return Map[from].Zones[MZONE_WATER] == Map[to].Zones[MZONE_WATER];
}

// The longest gun range among the team's ships, in leptons.
static int Naval_Range(ProcTeamPreset const * preset)
{
    int range = 0;
    for (int m = 0; m < preset->MemberCount; m++) {
        TechnoTypeClass const * type = preset->Members[m].Type;
        if (type->What_Am_I() != RTTI_VESSELTYPE || type->PrimaryWeapon == NULL) continue;
        range = max(range, (int)type->PrimaryWeapon->Range);
    }
    return range;
}

// The victim's buildings in gun range of water the ships can sail to (up to max of them).
int ProcMissionManager::Shell_Targets(CELL yard, HouseClass const * victim, int range, TARGET * targets, int max) const
{
    CELL from = Find_Water_Near(yard, 6);
    if (from == -1 || range <= 0) return 0;
    int zone = Map[from].Zones[MZONE_WATER];
    int cells = range / CELL_LEPTON_W;
    int count = 0;

    for (int i = 0; i < Buildings.Count() && count < max; i++) {
        BuildingClass const * building = Buildings.Ptr(i);
        if ((HouseClass const *)building->House != victim || building->IsInLimbo || building->Strength <= 0 || building->Class->IsWall) continue;
        COORDINATE center = building->Center_Coord();
        CELL origin = Coord_Cell(center);
        bool reach = false;
        for (int dy = -cells; dy <= cells && !reach; dy++) {
            for (int dx = -cells; dx <= cells && !reach; dx++) {
                CELL cell = XY_Cell(Cell_X(origin) + dx, Cell_Y(origin) + dy);
                if (!Map.In_Radar(cell) || Map[cell].Land_Type() != LAND_WATER || Map[cell].Zones[MZONE_WATER] != zone) continue;
                if (::Distance(Cell_Coord(cell), center) <= range) reach = true;
            }
        }
        if (reach) targets[count++] = building->As_Target();
    }
    return count;
}

ProcTeamPreset const * ProcMissionManager::Pick_Naval_Team(HouseClass * house, HouseClass * victim, CELL yard) const
{
    bool coast = Can_Reach_By_Sea(yard, victim);
    bool navy = (victim->CurVessels > 0);
    if (!coast && !navy) return NULL;

    for (int tries = 0; tries < 6; tries++) {
        ProcTeamPreset const * preset = ProcPresets.Pick_Team(Proc_Side_Of(house), PROC_TEAM_ATTACK, PROC_THREAT_SCOUT,
            Current_Threat(), house->Control.TechLevel, 1);
        if (preset == NULL) return NULL;
        TARGET target;
        if (preset->IsShipHunter ? navy : (coast && Shell_Targets(yard, victim, Naval_Range(preset), &target, 1) > 0)) return preset;
    }
    return NULL;
}


/*******************************************************************************
 * REINFORCEMENTS
 *
 * Every few minutes (difficulty tables) the director sends a Role=Reinforce team to the
 * humans, to AI allies and to enemies. They drive in from the map edge nearest their base
 * and move to it; an enemy's reinforcements then strengthen its defence. Squad missions send
 * none to the humans, and missions without waves (raids) none to the enemy.
 ******************************************************************************/

void ProcMissionManager::Reinforce_AI()
{
    if (!Settings.IsReinforcements) return;

    for (int group = 0; group < 3; group++) {
        int minutes = (group == 0) ? Diff.PlayerReinforceMinutes : (group == 1) ? Diff.AllyReinforceMinutes : EnemyReinforceMinutes;
        if (minutes <= 0) continue;
        if (group == 0 && (IsSquad || IsSpectating)) continue;
        if (group == 2 && !Settings.IsWaves) continue;

        if (NextReinforce[group] == 0) NextReinforce[group] = StartFrame + minutes * TICKS_PER_MINUTE;
        if (Elapsed < NextReinforce[group]) continue;
        NextReinforce[group] = Elapsed + minutes * TICKS_PER_MINUTE;

        if (group == 2) {
            for (int i = 0; i < EnemyCount; i++) {
                if (Is_Alive(Enemies[i].House) && Enemies[i].House->CurBuildings > 0) Send_Reinforcements(Enemies[i].House);
            }
        } else {
            for (int i = 0; i < PlayerTeamCount; i++) {
                HouseClass * house = PlayerTeam[i];
                if (house->IsHuman == (group == 0) && Is_Alive(house)) Send_Reinforcements(house);
            }
        }
    }
}

/*
**	Members of live teams of this type: how many there are, and how many made it onto the map
**	(Do_Reinforcements reports success even when it could not place anyone).
*/
static int Collect_Members(TeamTypeClass const * teamtype, FootClass ** members, int max)
{
    int count = 0;
    for (int i = 0; i < Units.Count() && count < max; i++) {
        if (Units.Ptr(i)->Team.Is_Valid() && Units.Ptr(i)->Team->Class == (TeamTypeClass *)teamtype) members[count++] = Units.Ptr(i);
    }
    for (int i = 0; i < Infantry.Count() && count < max; i++) {
        if (Infantry.Ptr(i)->Team.Is_Valid() && Infantry.Ptr(i)->Team->Class == (TeamTypeClass *)teamtype) members[count++] = Infantry.Ptr(i);
    }
    for (int i = 0; i < Vessels.Count() && count < max; i++) {
        if (Vessels.Ptr(i)->Team.Is_Valid() && Vessels.Ptr(i)->Team->Class == (TeamTypeClass *)teamtype) members[count++] = Vessels.Ptr(i);
    }
    for (int i = 0; i < Aircraft.Count() && count < max; i++) {
        if (Aircraft.Ptr(i)->Team.Is_Valid() && Aircraft.Ptr(i)->Team->Class == (TeamTypeClass *)teamtype) members[count++] = Aircraft.Ptr(i);
    }
    return count;
}

static int Team_Members(TeamTypeClass const * teamtype, int * onmap)
{
    FootClass * members[64];
    int total = Collect_Members(teamtype, members, ARRAY_SIZE(members));
    *onmap = 0;
    for (int i = 0; i < total; i++) {
        if (!members[i]->IsInLimbo) (*onmap)++;
    }
    return total;
}

// Deletes every team of this type and its members.
static void Delete_Teams(TeamTypeClass const * teamtype)
{
    FootClass * members[64];
    int total = Collect_Members(teamtype, members, ARRAY_SIZE(members));
    for (int i = 0; i < total; i++) {
        delete members[i];
    }
    for (int i = 0; i < Teams.Count(); i++) {
        TeamClass * team = Teams.Ptr(i);
        if (team->Class.Is_Valid() && (TeamTypeClass *)team->Class == teamtype) {
            delete team;
            i--;
        }
    }
}

/*
**	The cell on the playable border nearest 'target' that is in the same movement zone and
**	clear to move onto: where a reinforcement can come onto the map and still reach 'target'.
*/
CELL ProcMissionManager::Find_Edge_Cell(CELL target, MZoneType mzone, SpeedType speed) const
{
    int zone = Map[target].Zones[mzone];
    CELL best = -1;
    int bestdist = 0;
    int width = Map.MapCellWidth;
    int height = Map.MapCellHeight;

    for (int i = 0; i < 2 * (width + height); i++) {
        int x, y;
        if (i < width) { x = i; y = 0; }
        else if (i < 2 * width) { x = i - width; y = height - 1; }
        else if (i < 2 * width + height) { x = 0; y = i - 2 * width; }
        else { x = width - 1; y = i - 2 * width - height; }

        CELL cell = XY_Cell(Map.MapCellX + x, Map.MapCellY + y);
        if (Map[cell].Zones[mzone] != zone || !Map[cell].Is_Clear_To_Move(speed, false, false)) continue;
        int dist = Cell_Dist(cell, target);
        if (best == -1 || dist < bestdist) {
            best = cell;
            bestdist = dist;
        }
    }
    return best;
}

/*
**	A water cell next to land that leads to 'destination', on water that reaches the map edge:
**	where a landing ship can drop vehicles off.
*/
CELL ProcMissionManager::Find_Beach(CELL destination) const
{
    int zone = Map[destination].Zones[MZONE_NORMAL];
    int deadzones[8];
    int deadcount = 0;

    for (int radius = 1; radius <= 24; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                CELL cell = XY_Cell(Cell_X(destination) + dx, Cell_Y(destination) + dy);
                if (!Map.In_Radar(cell) || Map[cell].Land_Type() != LAND_WATER || !Map[cell].Is_Clear_To_Move(SPEED_FLOAT, false, false)) continue;

                bool shore = false;
                for (FacingType f = FACING_N; f < FACING_COUNT && !shore; f++) {
                    CELL land = Adjacent_Cell(cell, f);
                    if (Map.In_Radar(land) && Map[land].Zones[MZONE_NORMAL] == zone && Map[land].Is_Clear_To_Move(SPEED_TRACK, false, false)) shore = true;
                }
                if (!shore) continue;

                // Lakes that never reach the edge are remembered so they are only scanned once.
                int water = Map[cell].Zones[MZONE_WATER];
                bool dead = false;
                for (int d = 0; d < deadcount; d++) {
                    if (deadzones[d] == water) dead = true;
                }
                if (dead) continue;
                if (Find_Edge_Cell(cell, MZONE_WATER, SPEED_FLOAT) != -1) return cell;
                if (deadcount < ARRAY_SIZE(deadzones)) deadzones[deadcount++] = water;
            }
        }
    }
    return -1;
}

static bool Is_Infantry_Team(ProcTeamPreset const * preset)
{
    for (int i = 0; i < preset->MemberCount; i++) {
        if (preset->Members[i].Type->What_Am_I() != RTTI_INFANTRYTYPE) return false;
    }
    return true;
}

/*
**	Paradrop: a Badger flies in from the map edge nearest the drop zone and drops the team's
**	infantry there, like the paratrooper superweapon. The infantry belong to no team: a
**	team's attack-waypoint order would have them shoot at the ground they land on.
*/
int ProcMissionManager::Paradrop(HouseClass * house, TeamTypeClass const * teamtype, CELL destination)
{
    ScenarioInit++;
    AircraftClass * plane = (AircraftClass *)AircraftTypeClass::As_Reference(AIRCRAFT_BADGER).Create_One_Of(house);
    ScenarioInit--;
    if (plane == NULL) return 0;

    int capacity = plane->Class->Max_Passengers();
    int count = 0;
    for (int i = 0; i < teamtype->ClassCount; i++) {
        for (int q = 0; q < teamtype->Members[i].Quantity && count < capacity; q++) {
            ScenarioInit++;
            FootClass * trooper = (FootClass *)teamtype->Members[i].Class->Create_One_Of(house);
            ScenarioInit--;
            if (trooper == NULL) break;
            plane->Attach(trooper);
            count++;
        }
    }
    if (count == 0) {
        delete plane;
        return 0;
    }
    plane->IsALoaner = true;

    // Parachutes drift: drop where there is land all around, or troopers end up in the water.
    CELL drop = Map.Nearby_Location(destination, SPEED_FOOT);
    for (int radius = 0; radius <= 10; radius++) {
        CELL found = -1;
        for (int dy = -radius; dy <= radius && found == -1; dy++) {
            for (int dx = -radius; dx <= radius && found == -1; dx++) {
                if (abs(dx) != radius && abs(dy) != radius) continue;
                CELL cell = XY_Cell(Cell_X(destination) + dx, Cell_Y(destination) + dy);
                bool dry = Map.In_Radar(cell);
                for (int ny = -2; ny <= 2 && dry; ny++) {
                    for (int nx = -2; nx <= 2 && dry; nx++) {
                        CELL around = XY_Cell(Cell_X(cell) + nx, Cell_Y(cell) + ny);
                        if (!Map.In_Radar(around) || Map[around].Land_Type() == LAND_WATER || Map[around].Land_Type() == LAND_RIVER) dry = false;
                    }
                }
                if (dry) found = cell;
            }
        }
        if (found != -1) {
            drop = Map.Nearby_Location(found, SPEED_FOOT);
            break;
        }
    }
    CELL saved = Scen.Waypoint[PROC_EDGE_WAYPOINT];
    Scen.Waypoint[PROC_EDGE_WAYPOINT] = drop;
    CELL edge = Map.Calculated_Cell(SOURCE_NORTH, PROC_EDGE_WAYPOINT, -1, SPEED_WINGED, false);
    Scen.Waypoint[PROC_EDGE_WAYPOINT] = saved;

    ScenarioInit++;
    bool placed = plane->Unlimbo(Cell_Coord(edge), DIR_N);
    ScenarioInit--;
    if (!placed) {
        delete plane;       // Also deletes the cargo
        return 0;
    }
    plane->Assign_Mission(MISSION_ATTACK);
    plane->Assign_Target(::As_Target(drop));
    plane->Commence();
    return count;
}

/*
**	How reinforcements get to a base, in order of preference:
**	  - drive in from the nearest stretch of map edge they can reach the base from;
**	  - vehicles: a landing ship from a water edge to a beach near the base;
**	  - infantry: a Chinook (Allied) lands them, a Badger (Soviet) paradrops them;
**	  - otherwise dropped in next to the base.
**	A team with vehicles and no way in by land or sea is swapped for an infantry team.
*/
bool ProcMissionManager::Send_Reinforcements(HouseClass * house)
{
    if (ReinforceTypeCount >= PROC_MAX_REINFORCE_TYPES || TeamTypes.Count() >= TeamTypes.Length()) return false;

    ProcTeamPreset const * preset = ProcPresets.Pick_Team(Proc_Side_Of(house), PROC_TEAM_REINFORCE, PROC_THREAT_SCOUT,
        Current_Threat(), house->Control.TechLevel);
    if (preset == NULL) return false;

    CELL home = Home_Cell(house);
    CELL destination = Map.Nearby_Location(home, SPEED_TRACK);
    if (destination <= 0) return false;

    char const * how = "from the map edge";
    CELL entry = Find_Edge_Cell(destination, MZONE_NORMAL, SPEED_TRACK);
    CELL beach = -1;
    if (entry == -1 && !Is_Infantry_Team(preset)) {
        beach = Find_Beach(destination);
        if (beach != -1) {
            entry = Find_Edge_Cell(beach, MZONE_WATER, SPEED_FLOAT);
            how = "by landing ship";
        } else {
            ProcTeamPreset const * infantry = NULL;
            for (int tries = 0; tries < 8 && infantry == NULL; tries++) {
                ProcTeamPreset const * candidate = ProcPresets.Pick_Team(Proc_Side_Of(house), PROC_TEAM_REINFORCE, PROC_THREAT_SCOUT,
                    Current_Threat(), house->Control.TechLevel);
                if (candidate != NULL && Is_Infantry_Team(candidate)) infantry = candidate;
            }
            if (infantry != NULL) preset = infantry;
        }
    }
    bool airlift = (entry == -1 && Is_Infantry_Team(preset));
    bool paradrop = airlift && Proc_Side_Of(house) == PROC_SIDE_SOVIET;
    if (airlift) how = paradrop ? "by paradrop" : "by Chinook";

    TeamTypeClass * tt = new TeamTypeClass();
    if (tt == NULL) return false;
    sprintf((char *)tt->IniName, "PROCR%02d", ReinforceCounter++ % 100);
    tt->House = house->Class->House;
    tt->IsAutocreate = false;
    tt->IsPrebuilt = false;
    tt->IsReinforcable = false;
    tt->IsTransient = true;
    tt->RecruitPriority = 7;
    tt->MaxAllowed = 1;
    tt->ClassCount = 0;

    // A transport carries five at most.
    TechnoTypeClass const * transport = NULL;
    if (beach != -1) transport = &VesselTypeClass::As_Reference(VESSEL_TRANSPORT);
    else if (airlift && !paradrop) transport = &AircraftTypeClass::As_Reference(AIRCRAFT_TRANSPORT);
    int room = (transport != NULL) ? transport->Max_Passengers() : 1000;

    bool enemy = !Is_Player_Team(house);
    for (int i = 0; i < preset->MemberCount && tt->ClassCount < TeamTypeClass::MAX_TEAM_CLASSCOUNT - 1 && room > 0; i++) {
        int count = enemy ? max(1, (preset->Members[i].Count * Diff.WaveSizePct + 50) / 100) : preset->Members[i].Count;
        count = min(count, room);
        room -= (transport != NULL) ? count : 0;
        tt->Members[tt->ClassCount].Class = preset->Members[i].Type;
        tt->Members[tt->ClassCount].Quantity = count;
        tt->ClassCount++;
    }
    if (transport != NULL) {
        tt->Members[tt->ClassCount].Class = transport;
        tt->Members[tt->ClassCount].Quantity = 1;
        tt->ClassCount++;
    }

    // Survival missions: the enemy's reinforcements come straight for the player's team.
    bool hunt = enemy && IsHuntingReinforcements;
    int m = 0;
    if (transport != NULL) {
        tt->MissionList[m].Mission = TMISSION_MOVECELL;
        tt->MissionList[m++].Data.Value = (beach != -1) ? beach : destination;
        tt->MissionList[m].Mission = TMISSION_UNLOAD;
        tt->MissionList[m++].Data.Value = 0;
    }
    if (hunt) {
        tt->MissionList[m].Mission = TMISSION_DO;
        tt->MissionList[m++].Data.Value = MISSION_HUNT;
    } else {
        // Then to the base; the team breaks up there and the units guard it.
        tt->MissionList[m].Mission = TMISSION_MOVECELL;
        tt->MissionList[m++].Data.Value = destination;
    }
    tt->MissionCount = m;

    bool ok = false;
    int onmap = 0;
    int total = 0;
    if (paradrop) {
        // Hunting paratroopers are dropped outside the base they are after.
        CELL drop = destination;
        HouseClass * victim = hunt ? Pick_Victim(house) : NULL;
        if (victim != NULL) drop = Map.Nearby_Location(Toward(Home_Cell(victim), home, 8), SPEED_FOOT);
        total = onmap = Paradrop(house, tt, drop);
        ok = (total > 0);
        delete tt;
        tt = NULL;
    } else {
        // Do_Reinforcements comes in from the map edge nearest the origin waypoint.
        CELL saved = Scen.Waypoint[PROC_EDGE_WAYPOINT];
        Scen.Waypoint[PROC_EDGE_WAYPOINT] = (entry != -1) ? entry : destination;
        tt->Origin = PROC_EDGE_WAYPOINT;
        ok = Do_Reinforcements(tt);
        tt->Origin = -1;
        Scen.Waypoint[PROC_EDGE_WAYPOINT] = saved;

        // It reports success even when nobody made it onto the map.
        total = Team_Members(tt, &onmap);
        if (ok && onmap == 0) {
            Delete_Teams(tt);
            ok = false;
        }
        if (!ok) {
            // Dropped in without the transport.
            if (transport != NULL) {
                tt->ClassCount--;
                for (int i = 0; i + 2 < tt->MissionCount; i++) tt->MissionList[i] = tt->MissionList[i + 2];
                tt->MissionCount -= 2;
            }
            ok = Spawn_Team(tt, home, 6, 10);
            how = "dropped in";
            total = Team_Members(tt, &onmap);
        }
    }

    char buf[160];
    sprintf(buf, "ProcMission: reinforcements %s for %s (%s, %s, %d/%d on the map%s)", preset->Name, house->IniName, ok ? "sent" : "failed", how, onmap, total,
        hunt ? ", hunting" : "");
    GlyphX_Debug_Print(buf);

    if (!ok) {
        delete tt;
        return false;
    }
    if (tt != NULL) ReinforceTypes[ReinforceTypeCount++] = tt;

    if (house->IsHuman) {
        Speak(VOX_REINFORCEMENTS, house, Cell_Coord(home));
        if (house == Commander) Message("Reinforcements have arrived.");
    }
    return true;
}

// Like Collect_Wave_Types: free a reinforcement TeamType once its team has broken up.
void ProcMissionManager::Collect_Reinforce_Types()
{
    for (int r = 0; r < ReinforceTypeCount; r++) {
        TeamTypeClass * tt = ReinforceTypes[r];
        bool used = false;
        for (int i = 0; i < Teams.Count() && !used; i++) {
            TeamClass * team = Teams.Ptr(i);
            if (team->Class.Is_Valid() && (TeamTypeClass *)team->Class == tt) used = true;
        }
        if (!used) {
            delete tt;
            ReinforceTypes[r--] = ReinforceTypes[--ReinforceTypeCount];
        }
    }
}

/*
**	The skirmish AI fires its nuke by itself but never uses an Iron Curtain or a Chronosphere,
**	so the director does: the Iron Curtain goes on the toughest vehicle of an attacking wave
**	(or of the house when no wave has one),
**	the Chronosphere jumps a tank to the edge of the base the enemy is after.
*/
void ProcMissionManager::Superweapon_AI()
{
    for (int e = 0; e < EnemyCount; e++) {
        HouseClass * house = Enemies[e].House;
        if (!Is_Alive(house)) continue;

        // The AI launches its own nukes (the moment one is ready); the charge starting over shows it.
        int stage = house->SuperWeapon[SPC_NUCLEAR_BOMB].Is_Present() ? house->SuperWeapon[SPC_NUCLEAR_BOMB].Anim_Stage() : 0;
        if (stage < Enemies[e].NukeStage - 10) {
            char line[80];
            sprintf(line, "ProcMission: %s launched a nuke", house->IniName);
            GlyphX_Debug_Print(line);
        }
        Enemies[e].NukeStage = stage;

        bool iron = house->SuperWeapon[SPC_IRON_CURTAIN].Is_Ready();
        bool chrono = house->SuperWeapon[SPC_CHRONOSPHERE].Is_Ready();
        if (!iron && !chrono) continue;

        // The toughest vehicle: from an attacking wave for the Iron Curtain, any for the Chronosphere.
        UnitClass * best = NULL;
        UnitClass * waved = NULL;
        for (int i = 0; i < Units.Count(); i++) {
            UnitClass * unit = Units.Ptr(i);
            if ((HouseClass *)unit->House != house || unit->IsInLimbo || unit->Strength <= 0 || unit->Class->PrimaryWeapon == NULL) continue;
            if (*unit == UNIT_HARVESTER || *unit == UNIT_MCV || unit->IronCurtainCountDown > 0 || unit->IsMoebius) continue;
            bool inwave = false;
            for (int w = 0; w < WaveTypeCount && unit->Team.Is_Valid(); w++) {
                if ((TeamTypeClass *)unit->Team->Class == WaveTypes[w]) inwave = true;
            }
            if (best == NULL || unit->Strength > best->Strength) best = unit;
            if (inwave && (waved == NULL || unit->Strength > waved->Strength)) waved = unit;
        }

        char buf[128];
        // No wave vehicle out: shield the toughest one anyway, rather than sit on it.
        if (waved == NULL) waved = best;
        if (iron && waved != NULL) {
            waved->IronCurtainCountDown = Rule.IronCurtainDuration * TICKS_PER_MINUTE;
            waved->Mark(MARK_CHANGE);
            Sound_Effect(VOC_IRON1, waved->Center_Coord());
            house->SuperWeapon[SPC_IRON_CURTAIN].Discharged(false);
            sprintf(buf, "ProcMission: %s Iron Curtain on %s at cell %d", house->IniName, waved->Class->IniName, (int)Coord_Cell(waved->Center_Coord()));
            GlyphX_Debug_Print(buf);
            Message("Warning: the enemy has used the Iron Curtain.");
        }

        HouseClass * victim = chrono ? Pick_Victim(house) : NULL;
        if (chrono && best != NULL && victim != NULL) {
            CELL target = Map.Nearby_Location(Toward(Home_Cell(victim), Home_Cell(house), 7), SPEED_TRACK);
            if (target > 0 && Map[target].Is_Clear_To_Move(SPEED_TRACK, false, false)) {
                best->MoebiusCell = Coord_Cell(best->Coord);
                best->Teleport_To(target);
                best->IsMoebius = true;
                best->MoebiusCountDown = Rule.ChronoDuration * TICKS_PER_MINUTE;
                Sound_Effect(VOC_CHRONO, best->Coord);
                best->Assign_Mission(MISSION_HUNT);
                house->SuperWeapon[SPC_CHRONOSPHERE].Discharged(false);
                for (int i = 0; i < Buildings.Count(); i++) {
                    BuildingClass * building = Buildings.Ptr(i);
                    if ((HouseClass *)building->House == house && *building == STRUCT_CHRONOSPHERE && !building->IsInLimbo) building->Begin_Mode(BSTATE_ACTIVE);
                }
                sprintf(buf, "ProcMission: %s Chronosphere sends %s to cell %d (%s)", house->IniName, best->Class->IniName, (int)target, victim->IniName);
                GlyphX_Debug_Print(buf);
                Message("Warning: enemy units are being chronoshifted into the area.");
            }
        }
    }
}

void ProcMissionManager::Director_AI()
{
    // Frequent, so "wave ... over after" reports the real lifetime.
    if ((Elapsed % (TICKS_PER_SECOND * 2)) == 0) {
        Collect_Wave_Types();
        Collect_Reinforce_Types();
    }

    Reinforce_AI();

    if ((Elapsed % (TICKS_PER_SECOND * 2)) == 0) {
        Superweapon_AI();
        Track_Situation();
    }
    if ((Elapsed % (TICKS_PER_SECOND * 5)) == 0) {
        Events_AI();
    }

    if (!Settings.IsWaves || Elapsed < NextWaveFrame) return;

    if (Active_Waves() < Diff.MaxActiveWaves + ExtraWaves) {
        for (int tries = 0; tries < EnemyCount; tries++) {
            // Waves come out of a base; a house whose base is gone sends no more.
            ProcHouseInfo const & enemy = Enemies[NextAttacker++ % EnemyCount];
            if (Is_Alive(enemy.House) && enemy.House->CurBuildings > 0) {
                Spawn_Wave(enemy.House);
                break;
            }
        }
        int seconds = Random_Pick(Settings.WaveMinSeconds, Settings.WaveMaxSeconds);
        NextWaveFrame = Elapsed + (seconds * TICKS_PER_SECOND * Diff.WaveIntervalPct) / WaveRatePct;
    } else {
        // Too many waves alive; look again shortly.
        NextWaveFrame = Elapsed + TICKS_PER_SECOND * 10;
    }
}


/*******************************************************************************
 * EVENTS
 *
 * Every few minutes the director throws a twist (PROCEVENTS.INI), picked for the
 * situation: an ally under attack calls for help, a player who is ahead gets a
 * counter-attack, one who is behind gets help, a quiet spell gets a convoy to hunt.
 ******************************************************************************/

/*
**	What the situation reads need over time: the humans' peak building count, damage to the
**	AI allies' bases, and sentry posts that have fallen.
*/
void ProcMissionManager::Track_Situation()
{
    char buf[128];

    int buildings = 0;
    long allies = 0;
    for (int h = 0; h < PlayerTeamCount; h++) {
        if (PlayerTeam[h]->IsHuman) buildings += PlayerTeam[h]->CurBuildings;
    }
    PeakBuildings = max(PeakBuildings, buildings);

    for (int i = 0; i < Buildings.Count(); i++) {
        BuildingClass const * building = Buildings.Ptr(i);
        if (building->IsInLimbo || building->Strength <= 0) continue;
        HouseClass const * house = building->House;
        if (!house->IsHuman && Is_Player_Team(house)) allies += building->Strength;
    }
    // A drop of 2% (losses count, repairs don't hide a new hit for long).
    if (AllyStrength > 0 && allies < AllyStrength - max(1L, AllyStrength / 50)) {
        if (AllyHitFrame == 0 || Elapsed - AllyHitFrame >= TICKS_PER_MINUTE) {
            sprintf(buf, "ProcMission: AI allies' bases under attack (strength %ld -> %ld)", AllyStrength, allies);
            GlyphX_Debug_Print(buf);
        }
        AllyHitFrame = Elapsed;
    }
    AllyStrength = allies;

    for (int s = 0; s < SentryCount; s++) {
        if (IsSentryLost[s]) continue;
        bool standing = false;
        for (int i = 0; i < Buildings.Count() && !standing; i++) {
            BuildingClass const * building = Buildings.Ptr(i);
            if ((HouseClass *)building->House != SentryOwners[s] || building->IsInLimbo || building->Strength <= 0) continue;
            if (Cell_Dist(Coord_Cell(building->Center_Coord()), SentryCells[s]) <= 5) standing = true;
        }
        if (!standing) {
            IsSentryLost[s] = true;
            SentryLostFrame = Elapsed;
            sprintf(buf, "ProcMission: sentry post at cell %d destroyed", (int)SentryCells[s]);
            GlyphX_Debug_Print(buf);
        }
    }
}

int ProcMissionManager::Situation() const
{
    int situation = PROC_WHEN_ANY;

    for (int i = 0; i < EnemyCount; i++) {
        int initial = Enemies[i].InitialBuildings;
        if (initial > 0 && (initial - (int)Enemies[i].House->CurBuildings) * 4 >= initial) situation |= PROC_WHEN_AHEAD;
    }

    int buildings = 0;
    for (int h = 0; h < PlayerTeamCount; h++) {
        if (PlayerTeam[h]->IsHuman) buildings += PlayerTeam[h]->CurBuildings;
    }
    if (PeakBuildings >= 4 && buildings * 10 < PeakBuildings * 7) situation |= PROC_WHEN_BEHIND;

    if (AllyHitFrame != 0 && Elapsed - AllyHitFrame < TICKS_PER_MINUTE) situation |= PROC_WHEN_ALLY_ATTACKED;
    if (SentryLostFrame != 0 && Elapsed - SentryLostFrame < 2 * TICKS_PER_MINUTE) situation |= PROC_WHEN_SENTRY_LOST;
    if (Active_Waves() == 0 && Elapsed - LastWaveFrame > TICKS_PER_MINUTE) situation |= PROC_WHEN_QUIET;
    return situation;
}

static void Situation_Names(int situation, char * buffer)
{
    static char const * const names[PROC_WHEN_COUNT] = {"any", "ahead", "behind", "ally attacked", "sentry lost", "quiet"};
    buffer[0] = '\0';
    for (int w = 1; w < PROC_WHEN_COUNT; w++) {
        if ((situation & (1 << w)) == 0) continue;
        if (buffer[0] != '\0') strcat(buffer, ", ");
        strcat(buffer, names[w]);
    }
    if (buffer[0] == '\0') strcpy(buffer, "nothing special");
}

void ProcMissionManager::Events_AI()
{
    if (!Settings.IsEvents || IsSquad || IsSpectating || IsFinished || ProcPresets.EventCount == 0 || EnemyCount == 0) return;

    if (NextEventFrame == 0) NextEventFrame = StartFrame + (long)Diff.EventFirstMinutes * TICKS_PER_MINUTE;
    if (Elapsed < NextEventFrame) return;

    int situation = Situation();
    int minutes = (int)((Elapsed - StartFrame) / TICKS_PER_MINUTE);
    for (int tries = 0; tries < 4; tries++) {
        ProcEventPreset const * event = ProcPresets.Pick_Event(PlayerSide, TechLevel, situation, minutes, EventsUsed);
        if (event == NULL) break;
        unsigned long bit = 1UL << (event - ProcPresets.Events);

        if (Fire_Event(event)) {
            if (event->IsOnce) EventsUsed |= bit;
            EventsFired++;
            NextEventFrame = Elapsed + (long)Random_Pick(Diff.EventMinMinutes, Diff.EventMaxMinutes) * TICKS_PER_MINUTE;
            return;
        }
        // An objective that doesn't fit this map won't fit later either; waves and help can wait.
        if (event->Kind == PROC_EVENT_OBJECTIVE) EventsUsed |= bit;
        else break;
    }
    NextEventFrame = Elapsed + TICKS_PER_SECOND * 30;
}

/*
**	Adds a Slot=Event objective mid-mission and places it. Returns its index, or -1.
*/
int ProcMissionManager::Add_Event_Objective(ProcObjectivePreset const * preset)
{
    // Finished event objectives make room for new ones.
    for (int i = 1; i < ObjectiveCount; i++) {
        if (Objectives[i].IsEvent && Objectives[i].State != PROC_STATE_ACTIVE) {
            for (int j = i; j < ObjectiveCount - 1; j++) Objectives[j] = Objectives[j + 1];
            ObjectiveCount--;
            i--;
        }
    }
    if (ObjectiveCount >= PROC_MAX_OBJECTIVES) return -1;
    for (int i = 0; i < ObjectiveCount; i++) {
        if (Objectives[i].Preset == preset) return -1;      // Already on
    }

    int index = ObjectiveCount++;
    ProcObjective & obj = Objectives[index];
    obj = ProcObjective();
    obj.Preset = preset;
    obj.IsEvent = true;
    obj.Minutes = Random_Pick(preset->MinMinutes, preset->MaxMinutes);
    Place_Objective_Targets(index);

    if (obj.State != PROC_STATE_ACTIVE || (obj.Target == TARGET_NONE && preset->TargetCount > 0 && preset->Kind != PROC_OBJ_CONTROL)) {
        ObjectiveCount--;
        return -1;
    }
    return index;
}

bool ProcMissionManager::Fire_Event(ProcEventPreset const * event)
{
    char buf[256];
    char situation[96];
    Situation_Names(Situation(), situation);
    bool ok = false;

    switch (event->Kind) {
        case PROC_EVENT_OBJECTIVE: {
            ProcObjectivePreset const * preset = ProcPresets.Find_Objective(event->Objective);
            int index = (preset != NULL) ? Add_Event_Objective(preset) : -1;
            if (index < 0) {
                sprintf(buf, "ProcMission: event %s: objective %s %s", event->Name, event->Objective, preset ? "could not be placed" : "not found");
                GlyphX_Debug_Print(buf);
                return false;
            }
            if (event->Text[0] != '\0') Message(event->Text);
            Announce_Objective(index);

            // Waves after it: an ally's base in distress is attacked, anything else is contested.
            ProcObjective const & obj = Objectives[index];
            BuildingClass * building = (preset->Owner == PROC_OWNER_ALLY) ? As_Building(obj.Target) : NULL;
            HouseClass * victim = (building != NULL) ? (HouseClass *)building->House : NULL;
            CELL where = (victim != NULL) ? Home_Cell(victim) : As_Cell(obj.Target);
            int enemy = -1;
            for (int e = 0; e < EnemyCount; e++) {
                if (!Is_Alive(Enemies[e].House) || Enemies[e].House->CurBuildings == 0) continue;
                if (enemy == -1 || Cell_Dist(where, Enemies[e].Home) < Cell_Dist(where, Enemies[enemy].Home)) enemy = e;
            }
            for (int n = 0; n < event->Waves && enemy != -1; n++) {
                Spawn_Wave(Enemies[enemy].House, (victim != NULL) ? victim : Commander, (victim != NULL) ? -1 : where, 0, event->Name);
            }
            ok = true;
            break;
        }

        case PROC_EVENT_COUNTERATTACK: {
            // From the enemy with the most left to throw.
            int enemy = -1;
            for (int e = 0; e < EnemyCount; e++) {
                if (!Is_Alive(Enemies[e].House) || Enemies[e].House->CurBuildings == 0) continue;
                if (enemy == -1 || Enemies[e].House->CurBuildings > Enemies[enemy].House->CurBuildings) enemy = e;
            }
            if (enemy == -1 || !Is_Alive(Commander)) return false;
            for (int n = 0; n < max(1, event->Waves); n++) {
                if (Spawn_Wave(Enemies[enemy].House, Commander, -1, event->ThreatBoost, event->Name)) ok = true;
            }
            if (ok && event->Text[0] != '\0') Message(event->Text);
            break;
        }

        case PROC_EVENT_REINFORCE:
            for (int n = 0; n < max(1, event->Waves); n++) {
                for (int h = 0; h < PlayerTeamCount; h++) {
                    if (PlayerTeam[h]->IsHuman && Is_Alive(PlayerTeam[h]) && Send_Reinforcements(PlayerTeam[h])) ok = true;
                }
            }
            if (ok && event->Text[0] != '\0') Message(event->Text);
            break;

        default:
            break;
    }

    sprintf(buf, "ProcMission: event %s %s at %lds (situation: %s)", event->Name, ok ? "fired" : "failed", (Elapsed - StartFrame) / TICKS_PER_SECOND, situation);
    GlyphX_Debug_Print(buf);
    return ok;
}


/*******************************************************************************
 * RUNTIME
 ******************************************************************************/

void ProcMissionManager::AI()
{
    if (!IsActive || IsFinished) return;
    Elapsed++;

    if (!IsAnnounced && Elapsed - StartFrame >= PROC_ANNOUNCE_DELAY) {
        IsAnnounced = true;
        Announce_Objectives();
    }

    Hold_Targets();

    if ((Elapsed % PROC_OBJECTIVE_CHECK_RATE) == 0) {
        Update_Objectives();
    }

    if (!IsFinished) {
        Director_AI();
    }

    if ((Elapsed % PROC_STATUS_RATE) == 0) {
        Print_Status();
    }
}

/*
**	One line summary of the mission state, e.g.
**	"ProcMission: status t=120s threat=1 waves=1/2 types=2 enemies=[18,9] player=12 objectives=[A,C]"
*/
void ProcMissionManager::Print_Status() const
{
    char buf[256];
    int len = sprintf(buf, "ProcMission: status t=%lds threat=%d waves=%d/%d types=%d enemies=[",
        Elapsed / TICKS_PER_SECOND, (int)Current_Threat(), Active_Waves(), Diff.MaxActiveWaves + ExtraWaves, WaveTypeCount);
    for (int i = 0; i < EnemyCount && len < 200; i++) {
        len += sprintf(buf + len, "%s%d%s", i ? "," : "", Enemies[i].House->CurBuildings, Is_Alive(Enemies[i].House) ? "" : "x");
    }
    len += sprintf(buf + len, "] player=%d", Commander ? (int)Commander->CurBuildings : -1);

    // Patrols still alive, and how many have walked away from where they started.
    if (PatrolCount > 0) {
        int alive = 0;
        int moved = 0;
        for (int i = 0; i < Teams.Count(); i++) {
            TeamClass * team = Teams.Ptr(i);
            if (!team->Class.Is_Valid() || team->Total == 0) continue;
            TeamTypeClass * tt = team->Class;
            for (int p = 0; p < PatrolCount; p++) {
                if (PatrolTypes[p] != tt) continue;
                alive++;
                if (team->Zone != TARGET_NONE && Cell_Dist(As_Cell(team->Zone), PatrolStart[p]) > 4) moved++;
            }
        }
        len += sprintf(buf + len, " patrols=%d/%d moved=%d", alive, PatrolCount, moved);
    }
    len += sprintf(buf + len, " objectives=[");
    static char const s_State[] = {'A', 'C', 'F'};
    for (int i = 0; i < ObjectiveCount && len < 240; i++) {
        len += sprintf(buf + len, "%s%c", i ? "," : "", s_State[Objectives[i].State]);
    }
    len += sprintf(buf + len, "]");

    // Spectating: the AI allies' buildings (the human has none).
    if (IsSpectating) {
        len += sprintf(buf + len, " allies=[");
        int n = 0;
        for (int i = 0; i < PlayerTeamCount && len < 240; i++) {
            if (PlayerTeam[i]->IsHuman) continue;
            len += sprintf(buf + len, "%s%d%s", n++ ? "," : "", PlayerTeam[i]->CurBuildings, Is_Alive(PlayerTeam[i]) ? "" : "x");
        }
        strcpy(buf + len, "]");
    }
    GlyphX_Debug_Print(buf);
}

/*
**	Below IQProduction the AI never builds, expands or sends out attack teams. Everything else
**	stays (all other IQ gates are lower): units guard and fight back, harvesters harvest and
**	damaged buildings get repaired.
*/
void ProcMissionManager::Disable_Enemy_Production()
{
    for (int i = 0; i < EnemyCount; i++) {
        HouseClass * house = Enemies[i].House;
        house->IsBaseBuilding = false;
        house->IQ = min(house->IQ, Rule.IQProduction - 1);
    }
}

bool ProcMissionManager::Is_Player_Team(HouseClass const * house) const
{
    if (house == NULL || Commander == NULL) return false;
    return house == Commander || house->Is_Ally(Commander);
}

bool ProcMissionManager::Is_Alive(HouseClass const * house) const
{
    return house != NULL && !house->IsDefeated;
}

CELL ProcMissionManager::Home_Cell(HouseClass const * house) const
{
    for (int i = 0; i < EnemyCount; i++) {
        if (Enemies[i].House == house) return Enemies[i].Home;
    }
    if (house->CurBuildings > 0) return Coord_Cell(house->Center);

    // No base (squad missions, or an MCV not deployed yet): Center only follows buildings,
    // so use the middle of the house's units instead.
    long x = 0;
    long y = 0;
    int count = 0;
    for (int layer = 0; layer < LAYER_COUNT; layer++) {
        for (int i = 0; i < Map.Layer[layer].Count(); i++) {
            ObjectClass * obj = Map.Layer[layer][i];
            if (!obj->Is_Foot() || ((TechnoClass *)obj)->House != house) continue;
            x += Coord_X(obj->Center_Coord());
            y += Coord_Y(obj->Center_Coord());
            count++;
        }
    }
    if (count > 0) return Map.Nearby_Location(Coord_Cell(XY_Coord(x / count, y / count)), SPEED_FOOT);
    return Coord_Cell(house->Center);
}

void ProcMissionManager::Message(char const * text) const
{
    GlyphX_Debug_Print(text);
    if (Commander == NULL) return;

    Logic_Switch_Player_Context(Commander);
    On_Message(text, PROC_MESSAGE_SECONDS, -1);
}


/*******************************************************************************
 * TEST HOOK
 *
 * Lets the headless harness (tools/ProcTest) play out objectives. The game engine
 * never calls it. Objectives are named by preset (PRIMARY = the primary objective).
 *
 *   damage   <objective> <pct>   damage the target by pct % of its maximum strength
 *   capture  <objective> <n>     spawn n player engineers beside the target, ordered to capture it
 *   clear    <objective> <r>     destroy every enemy unit within r cells of the target (its guards)
 *   advance  <objective>         like a player would: attack-move (guard area) the commander's
 *                                units to the target, fighting whatever they meet; engineers
 *                                stop 12 cells short (out of the guards' reach) and wait
 *   engineer <objective>         send one of the commander's own engineers to capture the target
 *   attack   <objective> [r]     order the commander's units (all but the engineers) to attack the
 *                                target; with r, only the units within r cells of it
 *   fight    <objective>         like a player: every unit attacks the nearest enemy soldier or
 *                                vehicle within 10 cells (focus on what shoots back, not on
 *                                buildings); the rest advance as above
 *   assault  <objective>         fight while enemy units are within 8 cells of the target, then
 *                                attack the target itself
 *   near     <objective> <r>     returns the enemy units (and defensive structures x100) within r cells
 *   distance <objective>         returns the commander's closest unit's distance to the target
 *   list     <objective> <r>     prints every object within r cells of the target (debugging)
 *   wipe     PLAYER|ALLY|ENEMY   destroy every object of the human players / their AI allies / the enemies
 *   credits                      returns the commander's money
 *   units                        returns the commander's unit count (infantry, vehicles, ships, aircraft)
 *   buildings                    returns the commander's building count
 *   health                       returns the commander's buildings' total strength, in % of their maximum
 *   owner    <objective>         returns 1 if the target belongs to the player's team, 0 if not, -1 if gone
 *   revealed <objective>         returns 1 if the commander can see the target's cell (not shrouded)
 *   naval                        prints each enemy's naval yard and whether its ships reach the
 *                                commander's base; returns how many can attack by sea
 *   fleet                        prints every enemy ship (debugging naval waves)
 *   mapped                       returns the % of the map's cells the commander has revealed
 *   athome   [r]                 returns the commander's ground units standing within r (15) cells of its base
 *   stranded                     returns (and prints) the ground units of any house that are outside
 *                                the playable area or standing on water
 *
 *   Unit objectives:
 *   kill     <objective>         Assassinate: kill the target outright
 *   hurt     <objective> <pct>   Assassinate: damage the target by pct % (it should run); returns its strength
 *   fleeing  <objective>         Assassinate: 1 once the target has run for its base
 *   approach <objective>         Assassinate: put two of the commander's riflemen 3-4 cells from the target
 *   free     <objective>         Rescue: put a rifleman of the commander's next to every held prisoner
 *   escort   <objective>         Rescue: order the freed prisoners to walk to the extraction point
 *   deliver  <objective>         Rescue: move the freed prisoners next to the extraction point at once
 *   prisoners <objective>        Rescue: returns rescued x100 + freed x10 + still held
 *   kill     <objective>         Rescue: kill every prisoner still alive
 *   occupy   <objective> <p>     Control: kill the enemy units at point p (-1 = every point) and put
 *                                three of the commander's riflemen there
 *   contest  <objective> <p>     Control: kill the player's units at point p and put three enemy riflemen there
 *   points   <objective>         Control: returns how many points the player's team holds
 *
 *   Events:
 *   event    <event>             fire this PROCEVENTS.INI event now; returns 1 if it fired
 *   situation                    returns the director's situation (PROC_WHEN_* bits)
 *   hurtally ALL <pct>           damage every AI ally building by pct % of its strength
 *
 * Returns -1 for an unknown command or objective.
 ******************************************************************************/

int ProcMissionManager::Test_Command(char const * command, char const * objective, int value)
{
    if (!IsActive || command == NULL || Commander == NULL) return -1;
    if (objective == NULL) objective = "";

    if (stricmp(command, "credits") == 0) return (int)Commander->Available_Money();

    if (stricmp(command, "situation") == 0) return Situation();
    if (stricmp(command, "event") == 0) {
        ProcEventPreset const * event = ProcPresets.Find_Event(objective);
        if (event == NULL) return -1;
        bool ok = Fire_Event(event);
        if (ok) EventsFired++;
        return ok ? 1 : 0;
    }
    if (stricmp(command, "hurtally") == 0) {
        TARGET victims[200];
        int count = 0;
        for (int i = 0; i < Buildings.Count() && count < ARRAY_SIZE(victims); i++) {
            BuildingClass * building = Buildings.Ptr(i);
            if (building->IsInLimbo || building->Strength <= 0 || building->House->IsHuman || !Is_Player_Team(building->House)) continue;
            victims[count++] = building->As_Target();
        }
        for (int i = 0; i < count; i++) {
            BuildingClass * building = As_Building(victims[i]);
            if (building == NULL || building->Strength <= 0) continue;
            int damage = max(1, (int)building->Class->MaxStrength * value / 100);
            if (damage >= building->Strength) damage = building->Strength - 1;      // Hurt, not destroyed
            if (damage > 0) building->Take_Damage(damage, 0, WARHEAD_HE, NULL, true);
        }
        return count;
    }
    if (stricmp(command, "units") == 0) return Commander->CurInfantry + Commander->CurUnits + Commander->CurVessels + Commander->CurAircraft;
    if (stricmp(command, "buildings") == 0) return Commander->CurBuildings;

    if (stricmp(command, "fleet") == 0) {
        CELL home = Home_Cell(Commander);
        for (int i = 0; i < Vessels.Count(); i++) {
            VesselClass * vessel = Vessels.Ptr(i);
            if (vessel->IsInLimbo || Is_Player_Team(vessel->House)) continue;
            char buf[200];
            ObjectClass * tarcom = Target_Legal(vessel->TarCom) ? As_Object(vessel->TarCom) : NULL;
            sprintf(buf, "ProcTest: %s %s cell %d dist=%d mission=%s tarcom=%s@%d (%d cells, range %d) navcom=%d team=%s", vessel->House->IniName, vessel->Class->IniName,
                (int)Coord_Cell(vessel->Center_Coord()), Cell_Dist(Coord_Cell(vessel->Center_Coord()), home), MissionClass::Mission_Name(vessel->Get_Mission()),
                tarcom ? tarcom->Class_Of().IniName : "-", tarcom ? (int)Coord_Cell(tarcom->Center_Coord()) : -1,
                tarcom ? (int)(vessel->Distance(tarcom) / CELL_LEPTON_W) : -1, vessel->Class->PrimaryWeapon ? (int)vessel->Class->PrimaryWeapon->Range / CELL_LEPTON_W : 0,
                (int)Target_Legal(vessel->NavCom), vessel->Team.Is_Valid() ? vessel->Team->Class->IniName : "-");
            GlyphX_Debug_Print(buf);
        }
        return Vessels.Count();
    }

    if (stricmp(command, "naval") == 0) {
        int count = 0;
        for (int i = 0; i < EnemyCount; i++) {
            CELL yard = Naval_Yard_Cell(Enemies[i].House);
            bool reach = (yard != -1) && Can_Reach_By_Sea(yard, Commander);
            TARGET targets[20];
            int inrange[3] = {0, 0, 0};
            static int const ranges[3] = {5, 9, 20};
            for (int r = 0; r < 3 && yard != -1; r++) inrange[r] = Shell_Targets(yard, Commander, ranges[r] * CELL_LEPTON_W, targets, ARRAY_SIZE(targets));
            CELL water = (yard != -1) ? Find_Water_Near(yard, 6) : -1;
            char buf[200];
            sprintf(buf, "ProcTest: %s naval yard at %d (water %d zone %d), reaches the player by sea: %s (player water near base: %d), buildings in range 5/9/20: %d/%d/%d",
                Enemies[i].House->IniName, (int)yard, (int)water, water != -1 ? (int)Map[water].Zones[MZONE_WATER] : -1, reach ? "yes" : "no",
                (int)Find_Water_Near(Home_Cell(Commander), PROC_NAVAL_REACH), inrange[0], inrange[1], inrange[2]);
            GlyphX_Debug_Print(buf);
            if (reach) count++;
        }
        return count;
    }

    if (stricmp(command, "health") == 0) {
        long strength = 0;
        long maximum = 0;
        for (int i = 0; i < Buildings.Count(); i++) {
            BuildingClass * building = Buildings.Ptr(i);
            if ((HouseClass *)building->House != Commander || building->IsInLimbo || building->Strength <= 0) continue;
            strength += building->Strength;
            maximum += building->Class->MaxStrength;
        }
        return maximum ? (int)((strength * 100) / maximum) : 0;
    }

    if (stricmp(command, "athome") == 0) {
        CELL home = Home_Cell(Commander);
        int count = 0;
        for (int layer = 0; layer < LAYER_COUNT; layer++) {
            for (int i = 0; i < Map.Layer[layer].Count(); i++) {
                ObjectClass * obj = Map.Layer[layer][i];
                RTTIType rtti = obj->What_Am_I();
                if ((rtti != RTTI_UNIT && rtti != RTTI_INFANTRY) || (HouseClass *)((TechnoClass *)obj)->House != Commander) continue;
                if (Cell_Dist(Coord_Cell(obj->Center_Coord()), home) <= (value > 0 ? value : 15)) count++;
            }
        }
        return count;
    }

    if (stricmp(command, "stranded") == 0) {
        int count = 0;
        for (int layer = 0; layer < LAYER_COUNT; layer++) {
            for (int i = 0; i < Map.Layer[layer].Count(); i++) {
                ObjectClass * obj = Map.Layer[layer][i];
                RTTIType rtti = obj->What_Am_I();
                if (rtti != RTTI_UNIT && rtti != RTTI_INFANTRY) continue;
                CELL cell = Coord_Cell(obj->Center_Coord());
                bool outside = !Map.In_Radar(cell);
                bool wet = !outside && Map[cell].Land_Type() == LAND_WATER;
                if (!outside && !wet) continue;
                count++;
                char buf[160];
                FootClass * foot = (FootClass *)obj;
                sprintf(buf, "ProcTest: stranded %s %s at cell %d (%s) team=%s mission=%s", ((TechnoClass *)obj)->House->IniName, obj->Class_Of().IniName,
                    (int)cell, outside ? "off the map" : "on water", foot->Team.Is_Valid() ? foot->Team->Class->IniName : "-", MissionClass::Mission_Name(foot->Get_Mission()));
                GlyphX_Debug_Print(buf);
            }
        }
        return count;
    }

    if (stricmp(command, "mapped") == 0) {
        int total = 0;
        int mapped = 0;
        for (CELL cell = 0; cell < MAP_CELL_TOTAL; cell++) {
            if (!Map.In_Radar(cell)) continue;
            total++;
            if (Map[cell].Is_Mapped(Commander)) mapped++;
        }
        return total ? (mapped * 100) / total : 0;
    }

    if (stricmp(command, "wipe") == 0) {
        bool player = (stricmp(objective, "PLAYER") == 0);
        bool ally = (stricmp(objective, "ALLY") == 0);
        TARGET victims[1000];
        int count = 0;
        for (int layer = 0; layer < LAYER_COUNT; layer++) {
            for (int i = 0; i < Map.Layer[layer].Count() && count < ARRAY_SIZE(victims); i++) {
                ObjectClass * obj = Map.Layer[layer][i];
                if (!obj->Is_Techno()) continue;
                HouseClass * house = ((TechnoClass *)obj)->House;
                bool match = player ? (house->IsHuman && Is_Player_Team(house)) : ally ? (!house->IsHuman && Is_Player_Team(house)) :
                    !Is_Player_Team(house) && house->Class->House != HOUSE_NEUTRAL;
                if (match) victims[count++] = obj->As_Target();
            }
        }
        for (int i = 0; i < count; i++) {
            TechnoClass * techno = As_Techno(victims[i]);
            if (techno == NULL || !techno->IsActive || techno->IsInLimbo || techno->Strength <= 0) continue;
            int damage = techno->Strength;
            techno->Take_Damage(damage, 0, WARHEAD_HE, NULL, true);
        }
        return count;
    }

    ProcObjective * obj = NULL;
    for (int i = 0; i < ObjectiveCount; i++) {
        if ((stricmp(objective, "PRIMARY") == 0 && Objectives[i].IsPrimary) || stricmp(objective, Objectives[i].Preset->Name) == 0) {
            obj = &Objectives[i];
            break;
        }
    }
    if (obj == NULL) return -1;

    /*
    **	Unit objectives.
    */
    ProcObjectiveKindType kind = obj->Preset->Kind;
    if (kind == PROC_OBJ_ASSASSINATE) {
        TechnoClass * unit = Live_Unit(obj->Target, obj->UnitType);
        if (stricmp(command, "fleeing") == 0) return obj->IsFleeing ? 1 : 0;
        if (stricmp(command, "approach") == 0) {
            if (unit == NULL) return -1;
            int spawned = 0;
            for (int n = 0; n < 2; n++) {
                if (Spawn_One(Commander, &InfantryTypeClass::As_Reference(INFANTRY_E1), Coord_Cell(unit->Center_Coord()), 3, 4, MISSION_GUARD) != NULL) spawned++;
            }
            return spawned;
        }
        if (stricmp(command, "kill") == 0 || stricmp(command, "hurt") == 0) {
            if (unit == NULL) return -1;
            int damage = (stricmp(command, "kill") == 0) ? (int)unit->Strength : max(1, (int)unit->Techno_Type_Class()->MaxStrength * value / 100);
            unit->Take_Damage(damage, 0, WARHEAD_HE, NULL, true);
            unit = Live_Unit(obj->Target, obj->UnitType);
            return (unit != NULL) ? (int)unit->Strength : 0;
        }
    }

    if (kind == PROC_OBJ_RESCUE) {
        int held = 0, freed = 0, done = 0;
        for (int i = 0; i < obj->PrisonerCount; i++) {
            TechnoClass * prisoner = Live_Unit(obj->Prisoners[i], obj->PrisonerTypes[i]);
            unsigned char state = obj->PrisonerState[i];
            if (state == PROC_PRISONER_RESCUED) done++;
            if (prisoner == NULL) continue;
            CELL cell = Coord_Cell(prisoner->Center_Coord());
            if (state == PROC_PRISONER_HELD) {
                held++;
                if (stricmp(command, "free") == 0) Spawn_One(Commander, &InfantryTypeClass::As_Reference(INFANTRY_E1), cell, 1, 1, MISSION_GUARD);
            } else if (state == PROC_PRISONER_FREE) {
                freed++;
                if (stricmp(command, "escort") == 0) {
                    prisoner->Assign_Mission(MISSION_MOVE);
                    prisoner->Assign_Target(TARGET_NONE);
                    prisoner->Assign_Destination(::As_Target(Map.Nearby_Location(obj->Extraction, SPEED_FOOT)));
                    prisoner->Commence();
                } else if (stricmp(command, "deliver") == 0) {
                    prisoner->Limbo();
                    if (!Scan_Place_Object(prisoner, Random_Cell_Around(obj->Extraction, 1, 3))) prisoner->Unlimbo(Cell_Coord(cell), DIR_N);
                    prisoner->Assign_Mission(MISSION_GUARD);
                }
            }
        }
        if (stricmp(command, "kill") == 0) {
            for (int i = 0; i < obj->PrisonerCount; i++) {
                TechnoClass * prisoner = Live_Unit(obj->Prisoners[i], obj->PrisonerTypes[i]);
                if (prisoner != NULL) {
                    int damage = prisoner->Strength;
                    prisoner->Take_Damage(damage, 0, WARHEAD_HE, NULL, true);
                }
            }
            return held + freed;
        }
        if (stricmp(command, "free") == 0) return held;
        if (stricmp(command, "escort") == 0 || stricmp(command, "deliver") == 0) return freed;
        if (stricmp(command, "prisoners") == 0) return done * 100 + freed * 10 + held;
    }

    if (kind == PROC_OBJ_CONTROL) {
        if (stricmp(command, "points") == 0) {
            int held = 0;
            for (int p = 0; p < obj->PointCount; p++) held += obj->IsPointHeld[p] ? 1 : 0;
            return held;
        }
        bool occupy = (stricmp(command, "occupy") == 0);
        if (occupy || stricmp(command, "contest") == 0) {
            int done = 0;
            for (int p = 0; p < obj->PointCount; p++) {
                if (value >= 0 && value < obj->PointCount && p != value) continue;
                CELL point = obj->Points[p];

                // Clear the other side off the point...
                TARGET victims[100];
                int count = 0;
                for (int layer = 0; layer < LAYER_COUNT; layer++) {
                    for (int i = 0; i < Map.Layer[layer].Count() && count < ARRAY_SIZE(victims); i++) {
                        ObjectClass * object = Map.Layer[layer][i];
                        if (!object->Is_Foot()) continue;
                        HouseClass * house = ((TechnoClass *)object)->House;
                        if (house->Class->House == HOUSE_NEUTRAL || Is_Player_Team(house) != !occupy) continue;
                        if (Cell_Dist(Coord_Cell(object->Center_Coord()), point) <= PROC_POINT_RADIUS + 3) victims[count++] = object->As_Target();
                    }
                }
                for (int i = 0; i < count; i++) {
                    TechnoClass * techno = As_Techno(victims[i]);
                    if (techno == NULL || !techno->IsActive || techno->IsInLimbo || techno->Strength <= 0) continue;
                    int damage = techno->Strength;
                    techno->Take_Damage(damage, 0, WARHEAD_HE, NULL, true);
                }

                // ... and put our own riflemen on it.
                HouseClass * house = occupy ? Commander : Enemies[0].House;
                for (int n = 0; n < 3; n++) {
                    if (Spawn_One(house, &InfantryTypeClass::As_Reference(INFANTRY_E1), point, 0, 2, MISSION_GUARD) != NULL) done++;
                }
            }
            return done;
        }
    }

    BuildingClass * target = (obj->Target != TARGET_NONE) ? As_Building(obj->Target) : NULL;
    if (target != NULL && (!target->IsActive || target->IsInLimbo || target->Class->Type != obj->TargetType)) target = NULL;

    if (stricmp(command, "revealed") == 0) {
        if (target == NULL) return -1;
        return Map[Coord_Cell(target->Center_Coord())].Is_Mapped(Commander) ? 1 : 0;
    }

    if (stricmp(command, "owner") == 0) {
        if (target == NULL) return -1;
        return Is_Player_Team(target->House) ? 1 : 0;
    }

    if (stricmp(command, "damage") == 0) {
        if (target == NULL) return -1;
        int damage = max(1, (int)target->Class->MaxStrength * value / 100);
        target->Take_Damage(damage, 0, WARHEAD_HE, NULL, true);
        return target->IsActive ? (int)target->Strength : 0;
    }

    if (stricmp(command, "clear") == 0) {
        // Around the target structure, or the objective's spot (a rescue camp, a control point).
        CELL center = (target != NULL) ? Coord_Cell(target->Center_Coord()) : (obj->Target != TARGET_NONE) ? As_Cell(obj->Target) : -1;
        if (center == -1) return -1;
        TARGET victims[200];
        int count = 0;
        for (int layer = 0; layer < LAYER_COUNT; layer++) {
            for (int i = 0; i < Map.Layer[layer].Count() && count < ARRAY_SIZE(victims); i++) {
                ObjectClass * object = Map.Layer[layer][i];
                if (!object->Is_Foot()) continue;
                HouseClass * house = ((TechnoClass *)object)->House;
                if (Is_Player_Team(house) || house->Class->House == HOUSE_NEUTRAL) continue;
                if (Cell_Dist(Coord_Cell(object->Center_Coord()), center) <= value) victims[count++] = object->As_Target();
            }
        }
        for (int i = 0; i < count; i++) {
            TechnoClass * techno = As_Techno(victims[i]);
            if (techno == NULL || !techno->IsActive || techno->IsInLimbo || techno->Strength <= 0) continue;
            int damage = techno->Strength;
            techno->Take_Damage(damage, 0, WARHEAD_HE, NULL, true);
        }
        return count;
    }

    if (stricmp(command, "list") == 0) {
        if (target == NULL) return -1;
        CELL center = Coord_Cell(target->Center_Coord());
        int count = 0;
        for (int layer = 0; layer < LAYER_COUNT; layer++) {
            for (int i = 0; i < Map.Layer[layer].Count(); i++) {
                ObjectClass * object = Map.Layer[layer][i];
                if (!object->Is_Techno()) continue;
                TechnoClass * techno = (TechnoClass *)object;
                int dist = Cell_Dist(Coord_Cell(object->Center_Coord()), center);
                if (dist > value) continue;
                char buf[128];
                sprintf(buf, "ProcTest: %s %s hp=%d dist=%d mission=%s", techno->House->IniName, techno->Techno_Type_Class()->IniName,
                    (int)techno->Strength, dist, MissionClass::Mission_Name(techno->Get_Mission()));
                GlyphX_Debug_Print(buf);
                count++;
            }
        }
        return count;
    }

    if (stricmp(command, "near") == 0 || stricmp(command, "distance") == 0) {
        if (target == NULL) return -1;
        CELL center = Coord_Cell(target->Center_Coord());
        if (stricmp(command, "distance") == 0) {
            int best = -1;
            for (int layer = 0; layer < LAYER_COUNT; layer++) {
                for (int i = 0; i < Map.Layer[layer].Count(); i++) {
                    ObjectClass * object = Map.Layer[layer][i];
                    if (!object->Is_Foot() || (HouseClass *)((TechnoClass *)object)->House != Commander) continue;
                    int dist = Cell_Dist(Coord_Cell(object->Center_Coord()), center);
                    if (best < 0 || dist < best) best = dist;
                }
            }
            return best;
        }
        int units = 0;
        int defenses = 0;
        for (int layer = 0; layer < LAYER_COUNT; layer++) {
            for (int i = 0; i < Map.Layer[layer].Count(); i++) {
                ObjectClass * object = Map.Layer[layer][i];
                if (!object->Is_Techno()) continue;
                HouseClass * house = ((TechnoClass *)object)->House;
                if (Is_Player_Team(house) || house->Class->House == HOUSE_NEUTRAL) continue;
                if (Cell_Dist(Coord_Cell(object->Center_Coord()), center) > value) continue;
                if (object->Is_Foot()) units++;
                else if (((TechnoClass *)object)->Is_Weapon_Equipped()) defenses++;
            }
        }
        return units + defenses * 100;
    }

    if (stricmp(command, "fight") == 0) {
        if (target == NULL) return -1;
        int engaged = 0;
        for (int i = 0; i < Infantry.Count(); i++) {
            InfantryClass * infantry = Infantry.Ptr(i);
            if ((HouseClass *)infantry->House != Commander || infantry->IsInLimbo || !infantry->IsActive || !infantry->Is_Weapon_Equipped()) continue;
            if (*infantry == INFANTRY_RENOVATOR) continue;

            // Keep shooting a soldier / vehicle it already has.
            TechnoClass const * current = As_Techno(infantry->TarCom);
            if (current != NULL && current->IsActive && current->Is_Foot() && current->Strength > 0) continue;

            FootClass * nearest = NULL;
            int best = 0;
            CELL here = Coord_Cell(infantry->Center_Coord());
            for (int layer = 0; layer < LAYER_COUNT; layer++) {
                for (int o = 0; o < Map.Layer[layer].Count(); o++) {
                    ObjectClass * object = Map.Layer[layer][o];
                    if (!object->Is_Foot() || object->IsInLimbo || object->Strength <= 0) continue;
                    HouseClass * house = ((TechnoClass *)object)->House;
                    if (Is_Player_Team(house) || house->Class->House == HOUSE_NEUTRAL) continue;
                    int dist = Cell_Dist(here, Coord_Cell(object->Center_Coord()));
                    if (dist <= 10 && (nearest == NULL || dist < best)) {
                        nearest = (FootClass *)object;
                        best = dist;
                    }
                }
            }
            if (nearest == NULL) continue;
            infantry->Assign_Mission(MISSION_ATTACK);
            infantry->Assign_Target(nearest->As_Target());
            infantry->Commence();
            engaged++;
        }
        return engaged + Test_Command("advance", objective, 0);
    }

    if (stricmp(command, "assault") == 0) {
        // Clear the guards first, then demolish.
        if (target == NULL) return -1;
        if (Test_Command("near", objective, 8) % 100 > 0) return Test_Command("fight", objective, 0);
        return Test_Command("attack", objective, 0);
    }

    if (stricmp(command, "attack") == 0) {
        if (target == NULL) return -1;
        CELL center = Coord_Cell(target->Center_Coord());
        int ordered = 0;
        for (int i = 0; i < Infantry.Count(); i++) {
            InfantryClass * infantry = Infantry.Ptr(i);
            if ((HouseClass *)infantry->House != Commander || infantry->IsInLimbo || !infantry->IsActive || *infantry == INFANTRY_RENOVATOR) continue;
            if (!infantry->Is_Weapon_Equipped()) continue;
            if (value > 0 && Cell_Dist(Coord_Cell(infantry->Center_Coord()), center) > value) continue;
            if (infantry->Get_Mission() == MISSION_ATTACK && infantry->TarCom == target->As_Target()) continue;
            infantry->Assign_Mission(MISSION_ATTACK);
            infantry->Assign_Target(target->As_Target());
            infantry->Commence();
            ordered++;
        }
        for (int i = 0; i < Units.Count(); i++) {
            UnitClass * unit = Units.Ptr(i);
            if ((HouseClass *)unit->House != Commander || unit->IsInLimbo || !unit->IsActive || !unit->Is_Weapon_Equipped()) continue;
            if (value > 0 && Cell_Dist(Coord_Cell(unit->Center_Coord()), center) > value) continue;
            unit->Assign_Mission(MISSION_ATTACK);
            unit->Assign_Target(target->As_Target());
            unit->Commence();
            ordered++;
        }
        return ordered;
    }

    if (stricmp(command, "advance") == 0 || stricmp(command, "engineer") == 0) {
        if (target == NULL) return -1;
        bool engineers = (stricmp(command, "engineer") == 0);
        if (engineers && Is_Player_Team(target->House)) return 0;     // Already ours
        CELL center = Coord_Cell(target->Center_Coord());
        int ordered = 0;
        for (int i = 0; i < Infantry.Count() && !(engineers && ordered > 0); i++) {
            InfantryClass * infantry = Infantry.Ptr(i);
            if ((HouseClass *)infantry->House != Commander || infantry->IsInLimbo || !infantry->IsActive) continue;
            bool isengineer = (*infantry == INFANTRY_RENOVATOR);
            if (engineers && !isengineer) continue;
            if (engineers) {
                ActionType action = infantry->What_Action(target);
                if (action != ACTION_CAPTURE && action != ACTION_DAMAGE) {
                    char buf[128];
                    sprintf(buf, "ProcTest: an engineer can't be ordered to capture %s (action %d)", target->Class->IniName, (int)action);
                    GlyphX_Debug_Print(buf);
                    return -2;
                }
                infantry->Assign_Mission(MISSION_CAPTURE);
                infantry->Assign_Target(TARGET_NONE);
                infantry->Assign_Destination(target->As_Target());
            } else if (isengineer) {
                CELL wait = Toward(center, Coord_Cell(infantry->Center_Coord()), 12);
                infantry->Assign_Mission(MISSION_MOVE);
                infantry->Assign_Target(TARGET_NONE);
                infantry->Assign_Destination(::As_Target(Map.Nearby_Location(wait, SPEED_FOOT)));
            } else {
                // Already attack-moving / attacking: re-ordering would make it drop its target.
                if (infantry->Get_Mission() == MISSION_GUARD_AREA || infantry->Get_Mission() == MISSION_ATTACK) continue;

                // Guard area, set up the way a player's order is (EventClass MEGAMISSION).
                TARGET spot = ::As_Target(Map.Nearby_Location(center, SPEED_FOOT));
                infantry->Assign_Mission(MISSION_GUARD_AREA);
                infantry->Assign_Target(TARGET_NONE);
                infantry->Assign_Destination(spot);
                infantry->ArchiveTarget = spot;
            }
            infantry->Commence();
            ordered++;
        }
        for (int i = 0; i < Units.Count() && !engineers; i++) {
            UnitClass * unit = Units.Ptr(i);
            if ((HouseClass *)unit->House != Commander || unit->IsInLimbo || !unit->IsActive) continue;
            if (unit->Get_Mission() == MISSION_GUARD_AREA || unit->Get_Mission() == MISSION_ATTACK) continue;
            TARGET spot = ::As_Target(Map.Nearby_Location(center, SPEED_TRACK));
            unit->Assign_Mission(MISSION_GUARD_AREA);
            unit->Assign_Target(TARGET_NONE);
            unit->Assign_Destination(spot);
            unit->ArchiveTarget = spot;
            unit->Commence();
            ordered++;
        }
        return ordered;
    }

    if (stricmp(command, "capture") == 0) {
        if (target == NULL) return -1;
        int spawned = 0;
        for (int i = 0; i < max(1, value); i++) {
            CELL cell = Map.Nearby_Location(Coord_Cell(target->Center_Coord()), SPEED_FOOT);
            InfantryClass * engineer = new InfantryClass(INFANTRY_RENOVATOR, Commander->Class->House);
            if (engineer == NULL) break;
            if (!engineer->Unlimbo(Cell_Coord(cell), DIR_N)) {
                delete engineer;
                continue;
            }

            // Only what a player could order: the action the cursor would show over the target.
            ActionType action = engineer->What_Action(target);
            if (action != ACTION_CAPTURE && action != ACTION_DAMAGE) {
                char buf[128];
                sprintf(buf, "ProcTest: an engineer can't be ordered to capture %s (action %d)", target->Class->IniName, (int)action);
                GlyphX_Debug_Print(buf);
                return -2;
            }
            engineer->Assign_Mission(MISSION_CAPTURE);
            engineer->Assign_Target(TARGET_NONE);
            engineer->Assign_Destination(target->As_Target());
            engineer->Commence();
            spawned++;
        }
        return spawned;
    }

    return -1;
}

extern "C" __declspec(dllexport) int __cdecl ProcMission_Test(char const * command, char const * objective, int value)
{
    return ProcMission.Test_Command(command, objective, value);
}
