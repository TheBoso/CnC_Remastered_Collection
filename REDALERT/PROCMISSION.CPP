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
}

bool ProcMissionManager::Generate(CCINIClass & scenario_ini)
{
    char buf[200];

    Reset();
    GlyphX_Debug_Print("ProcMission: generating procedural mission");

    if (!ProcPresets.Load()) {
        GlyphX_Debug_Print("ProcMission: no base presets loaded (is PROCBASES.INI available?), mission not generated");
        return false;
    }

    Settings = ProcPresets.Settings;
    ProcPresets.Read_Settings(scenario_ini, PROC_OVERRIDE_SECTION, Settings);

    Difficulty = (Settings.Difficulty < 0) ? (DiffType)Random_Pick(0, DIFF_COUNT - 1) : (DiffType)Bound(Settings.Difficulty, 0, DIFF_COUNT - 1);
    Diff = ProcPresets.Difficulty[Difficulty];

    Classify_Houses();
    if (Commander == NULL || EnemyCount == 0) {
        GlyphX_Debug_Print("ProcMission: needs a human player and at least one AI on another team, mission not generated");
        return false;
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
    Place_Objective_Targets();

    ScenarioInit = save_init;

    for (int i = 0; i < EnemyCount; i++) {
        Enemies[i].InitialBuildings = max(1, Enemies[i].House->CurBuildings);
    }

    StartFrame = Elapsed;
    NextWaveFrame = Elapsed + (Diff.FirstWaveSeconds * TICKS_PER_SECOND * Diff.WaveIntervalPct) / 100;
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
    if (stricmp(Settings.Primary, "RANDOM") != 0) {
        primary = ProcPresets.Find_Objective(Settings.Primary);
        if (primary == NULL) {
            char buf[100];
            sprintf(buf, "ProcMission: primary objective '%s' not found, picking at random", Settings.Primary);
            GlyphX_Debug_Print(buf);
        }
    }
    if (primary == NULL) primary = ProcPresets.Pick_Objective(PlayerSide, true, TechLevel, NULL, 0);
    if (primary == NULL) primary = &s_Fallback;

    int secondaries = Random_Pick(Settings.SecondaryMin, Settings.SecondaryMax);
    secondaries = Bound(secondaries, 0, PROC_MAX_OBJECTIVES - 1);

    for (int i = 0; i <= secondaries; i++) {
        ProcObjectivePreset const * preset = (i == 0) ? primary : ProcPresets.Pick_Objective(PlayerSide, false, TechLevel, chosen, ObjectiveCount);
        if (preset == NULL) break;

        ProcObjective & obj = Objectives[ObjectiveCount];
        obj = ProcObjective();
        obj.Preset = preset;
        obj.IsPrimary = (i == 0);
        obj.Minutes = Random_Pick(preset->MinMinutes, preset->MaxMinutes);
        chosen[ObjectiveCount++] = preset;

        char buf[120];
        sprintf(buf, "ProcMission: %s objective %s (%s, %d min)", obj.IsPrimary ? "primary" : "secondary",
            preset->Name, Proc_Objective_Kind_Name(preset->Kind), obj.Minutes);
        GlyphX_Debug_Print(buf);
    }
}

void ProcMissionManager::Build_Enemy_Bases()
{
    char buf[120];

    for (int i = 0; i < EnemyCount; i++) {
        HouseClass * house = Enemies[i].House;
        ProcSideType side = Proc_Side_Of(house);

        // The first enemy always gets a main base; the rest may get outposts.
        ProcBaseRoleType role = (i > 0 && Random_Pick(0, 99) < Diff.OutpostChance) ? PROC_BASE_OUTPOST : PROC_BASE_MAIN;
        ProcBasePreset const * preset = ProcPresets.Pick_Base(side, role, house->Control.TechLevel);
        if (preset == NULL && role == PROC_BASE_OUTPOST) {
            preset = ProcPresets.Pick_Base(side, PROC_BASE_MAIN, house->Control.TechLevel);
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

        sprintf(buf, "ProcMission: enemy %d (%s) base '%s', %d buildings", i, Proc_Side_Name(side), preset->Name, house->CurBuildings);
        GlyphX_Debug_Print(buf);
    }

    /*
    **	Some primary objectives (survival style) hand the human players a ready made base.
    */
    ProcObjectivePreset const * primary = (ObjectiveCount > 0) ? Objectives[0].Preset : NULL;
    if (primary != NULL && primary->PlayerBase[0] != '\0') {
        for (int i = 0; i < PlayerTeamCount; i++) {
            HouseClass * house = PlayerTeam[i];
            if (!house->IsHuman) continue;

            ProcBasePreset const * preset = (stricmp(primary->PlayerBase, "AUTO") == 0)
                ? ProcPresets.Pick_Base(Proc_Side_Of(house), PROC_BASE_DEFEND, house->Control.TechLevel)
                : ProcPresets.Find_Base(primary->PlayerBase);
            if (preset == NULL) continue;

            Clear_Start_Units(house, true);
            Build_Base(house, preset, true);
        }
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

bool ProcMissionManager::Place_Building(HouseClass * house, StructType type, CELL center, int minradius, int maxradius, TARGET * placed)
{
    if (type == STRUCT_NONE || center == -1) return false;

    BuildingTypeClass const & btype = BuildingTypeClass::As_Reference(type);
    for (int attempt = 0; attempt < 80; attempt++) {
        // Widen the search when the preferred ring is crowded.
        int widen = (attempt / 20) * 3;
        CELL cell = (attempt == 0 && minradius == 0) ? center : Random_Cell_Around(center, minradius, maxradius + widen);

        if (!Can_Place_Building(type, cell)) continue;

        BuildingClass * building = (BuildingClass *)btype.Create_One_Of(house);
        if (building == NULL) return false;

        // Placed as a pre-existing structure: no build-up animation, no free refinery harvester.
        ScenarioInit++;
        bool ok = building->Unlimbo(Cell_Coord(cell), DIR_N);
        ScenarioInit--;
        if (ok) {
            if (placed) *placed = building->As_Target();
            return true;
        }
        delete building;
    }
    return false;
}

void ProcMissionManager::Place_Units(HouseClass * house, TechnoTypeClass const * type, int count, CELL center, MissionType mission)
{
    if (type == NULL || center == -1) return;

    bool isvessel = (type->What_Am_I() == RTTI_VESSELTYPE);
    if (type->What_Am_I() == RTTI_AIRCRAFTTYPE) return;     // Aircraft need a helipad / airfield; never placed loose

    for (int i = 0; i < count; i++) {
        CELL cell = isvessel ? Find_Water_Near(center, 24) : Random_Cell_Around(center, 2, 7);
        if (cell == -1) return;

        TechnoClass * obj = (TechnoClass *)type->Create_One_Of(house);
        if (obj == NULL) return;

        if (!Scan_Place_Object(obj, cell)) {
            delete obj;
            continue;
        }
        obj->Assign_Mission(mission);
    }
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

bool ProcMissionManager::Build_Base(HouseClass * house, ProcBasePreset const * preset, bool isplayer)
{
    CELL center = Coord_Cell(house->Center);
    if (!Map.In_Radar(center)) return false;

    for (int e = 0; e < preset->EntryCount; e++) {
        ProcBaseEntry const & entry = preset->Entries[e];
        BuildingTypeClass const & btype = BuildingTypeClass::As_Reference(entry.Type);

        // The tech level and side decide what actually exists in this base.
        if (entry.Type != STRUCT_CONST && !Is_Allowed(btype, house)) continue;

        int count = entry.Count;
        if (entry.IsDefense && !isplayer) {
            count = (count * Diff.DefenseScalePct + 50) / 100;
        }
        for (int n = 0; n < count; n++) {
            Place_Building(house, entry.Type, center, entry.MinRadius, entry.MaxRadius);
        }
    }

    if (preset->IsAutoPower) {
        StructType power = Is_Allowed(BuildingTypeClass::As_Reference(STRUCT_ADVANCED_POWER), house) ? STRUCT_ADVANCED_POWER : STRUCT_POWER;
        for (int n = 0; n < 12 && house->Power < house->Drain + 50; n++) {
            if (!Place_Building(house, power, center, 3, 11)) break;
        }
    }

    if (preset->IsNaval) {
        Place_Naval_Yard(house, center);
    }

    if (house->ActiveBScan & STRUCTF_REFINERY) {
        Place_Units(house, &UnitTypeClass::As_Reference(UNIT_HARVESTER), preset->Harvesters, center, MISSION_HARVEST);
    }

    MissionType guard = isplayer ? MISSION_GUARD : MISSION_GUARD_AREA;
    for (int g = 0; g < preset->GuardCount; g++) {
        TechnoTypeClass const * type = preset->Guards[g].Type;
        if (!Is_Allowed(*type, house)) continue;

        int count = preset->Guards[g].Count;
        if (!isplayer) count = (count * Diff.GuardScalePct + 50) / 100;
        Place_Units(house, type, count, center, guard);
    }
    return true;
}

CELL ProcMissionManager::Find_Neutral_Location(int mindistance) const
{
    for (int attempt = 0; attempt < 300; attempt++) {
        int needed = mindistance - (attempt / 60) * 4;     // Relax the distance on crowded maps
        CELL cell = Clamp_To_Map(Map.MapCellX + Random_Pick(2, Map.MapCellWidth - 3), Map.MapCellY + Random_Pick(2, Map.MapCellHeight - 3));
        if (!Map[cell].Is_Clear_To_Build(SPEED_NONE)) continue;

        bool ok = true;
        for (int i = 0; i < PlayerTeamCount && ok; i++) {
            if (Cell_Dist(cell, Home_Cell(PlayerTeam[i])) < needed) ok = false;
        }
        for (int i = 0; i < EnemyCount && ok; i++) {
            if (Cell_Dist(cell, Enemies[i].Home) < needed) ok = false;
        }
        if (ok) return cell;
    }
    return -1;
}


/*******************************************************************************
 * OBJECTIVES
 ******************************************************************************/

void ProcMissionManager::Place_Objective_Targets()
{
    char buf[160];

    for (int i = 0; i < ObjectiveCount; i++) {
        ProcObjective & obj = Objectives[i];
        ProcObjectivePreset const * preset = obj.Preset;

        if (obj.Minutes > 0) {
            obj.EndFrame = Elapsed + obj.Minutes * TICKS_PER_MINUTE;
            if (obj.IsPrimary) {
                // The primary objective's clock is shown on the sidebar.
                Scen.MissionTimer = obj.Minutes * TICKS_PER_MINUTE;
                Scen.MissionTimer.Start();
            }
        }

        if (preset->TargetCount == 0) continue;

        /*
        **	Pick the owner and the area for the target structure.
        */
        HouseClass * owner = NULL;
        CELL center = -1;
        int minradius = 3;
        int maxradius = 10;
        switch (preset->Owner) {
            case PROC_OWNER_ENEMY: {
                ProcHouseInfo const & enemy = Enemies[Random_Pick(0, EnemyCount - 1)];
                owner = enemy.House;
                center = enemy.Home;
                minradius = 4;
                maxradius = 12;
                break;
            }
            case PROC_OWNER_NEUTRAL:
                owner = HouseClass::As_Pointer(HOUSE_NEUTRAL);
                center = Find_Neutral_Location(22);
                minradius = 0;
                maxradius = 6;
                break;
            case PROC_OWNER_PLAYER:
                owner = Commander;
                center = Home_Cell(Commander);
                minradius = 3;
                maxradius = 9;
                break;
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

        if (owner == NULL || center == -1 || !Place_Building(owner, obj.TargetType, center, minradius, maxradius, &obj.Target)) {
            sprintf(buf, "ProcMission: could not place target for objective %s", preset->Name);
            GlyphX_Debug_Print(buf);
            obj.State = PROC_STATE_FAILED;
            obj.Target = TARGET_NONE;
            continue;
        }

        /*
        **	Garrison. Neutral targets are guarded by an enemy so the guards actually fight.
        */
        ProcTeamPreset const * guard = (preset->Guard[0] != '\0') ? ProcPresets.Find_Team(preset->Guard) : NULL;
        if (guard != NULL) {
            HouseClass * guardhouse = (preset->Owner == PROC_OWNER_NEUTRAL) ? Enemies[Random_Pick(0, EnemyCount - 1)].House : owner;
            MissionType mission = (guardhouse == Commander) ? MISSION_GUARD : MISSION_GUARD_AREA;
            CELL targetcell = As_Cell(obj.Target);
            for (int m = 0; m < guard->MemberCount; m++) {
                if (!Is_Allowed(*guard->Members[m].Type, guardhouse)) continue;
                int count = guard->Members[m].Count;
                if (guardhouse != Commander) count = max(1, (count * Diff.GuardScalePct + 50) / 100);
                Place_Units(guardhouse, guard->Members[m].Type, count, targetcell, mission);
            }
        }

        sprintf(buf, "ProcMission: objective %s target %s at cell %d", preset->Name,
            BuildingTypeClass::As_Reference(obj.TargetType).IniName, (int)As_Cell(obj.Target));
        GlyphX_Debug_Print(buf);
    }

    /*
    **	A primary objective without its target is impossible; fall back to destroying the enemy.
    */
    if (ObjectiveCount > 0 && Objectives[0].State == PROC_STATE_FAILED) {
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
}

void ProcMissionManager::Announce_Objectives()
{
    char buf[PROC_TEXT_LEN + 32];
    for (int i = 0; i < ObjectiveCount; i++) {
        if (Objectives[i].State != PROC_STATE_ACTIVE) continue;
        Objective_Text(Objectives[i], buf, sizeof(buf));
        Message(buf);

        if (Objectives[i].Target != TARGET_NONE) {
            On_Ping(Commander, As_Coord(Objectives[i].Target));
        }
    }
}

static BuildingClass * Live_Target(ProcObjective const & obj)
{
    if (obj.Target == TARGET_NONE) return NULL;
    BuildingClass * building = As_Building(obj.Target);
    if (building == NULL || !building->IsActive || building->IsInLimbo || building->Strength <= 0) return NULL;
    if (building->Class->Type != obj.TargetType) return NULL;
    return building;
}

void ProcMissionManager::Update_Objectives()
{
    for (int i = 0; i < ObjectiveCount && !IsFinished; i++) {
        ProcObjective & obj = Objectives[i];
        if (obj.State != PROC_STATE_ACTIVE) continue;

        bool timeup = (obj.EndFrame != 0 && Elapsed >= obj.EndFrame);
        BuildingClass * target = Live_Target(obj);

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
                else if (Is_Player_Team(target->House)) Complete_Objective(obj);
                else if (timeup) Fail_Objective(obj);
                break;

            case PROC_OBJ_PROTECT:
                if (target == NULL || !Is_Player_Team(target->House)) Fail_Objective(obj);
                else if (timeup) Complete_Objective(obj);
                break;

            default:
                break;
        }
    }
}

void ProcMissionManager::Complete_Objective(ProcObjective & objective)
{
    char buf[PROC_TEXT_LEN + 48];
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
    char buf[PROC_TEXT_LEN + 48];
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

    if (preset->RewardCredits > 0) {
        Commander->Refund_Money(preset->RewardCredits);
    }

    if (preset->RewardUnitCount > 0) {
        CELL home = Home_Cell(Commander);
        for (int i = 0; i < preset->RewardUnitCount; i++) {
            Place_Units(Commander, preset->RewardUnits[i].Type, preset->RewardUnits[i].Count, home, MISSION_GUARD);
        }
        Speak(VOX_REINFORCEMENTS, Commander, Cell_Coord(home));
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
        if (Is_Alive(PlayerTeam[i]) && !attacker->Is_Ally(PlayerTeam[i])) alive[count++] = PlayerTeam[i];
    }
    if (count == 0) return NULL;

    // Mostly go for the human, sometimes for an ally.
    if (Is_Alive(Commander) && Random_Pick(0, 99) < 60) return Commander;
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
    if (preset->IsGathering && victim != NULL) {
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
    if (m == 0) {
        tt->MissionList[m].Mission = TMISSION_ATTACK;
        tt->MissionList[m++].Data.Value = QUARRY_ANYTHING;
    }
    tt->MissionCount = m;

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

    /*
    **	BASE mode: the team rolls out from next to the owning base.
    */
    TeamClass * team = new TeamClass(teamtype);
    if (team == NULL) return false;
    team->Force_Active();

    int placed = 0;
    for (int i = 0; i < teamtype->ClassCount; i++) {
        TechnoTypeClass const * type = teamtype->Members[i].Class;
        RTTIType rtti = type->What_Am_I();
        if (rtti == RTTI_AIRCRAFTTYPE) continue;

        for (int q = 0; q < teamtype->Members[i].Quantity; q++) {
            CELL cell = (rtti == RTTI_VESSELTYPE) ? Find_Water_Near(home, 24) : Random_Cell_Around(home, 3, 7);
            if (cell == -1) break;

            FootClass * obj = (FootClass *)type->Create_One_Of(house);
            if (obj == NULL) break;

            if (!Scan_Place_Object(obj, cell)) {
                delete obj;
                continue;
            }
            team->Add(obj);
            obj->IsInitiated = true;
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
                count++;
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
            delete tt;
            WaveTypes[w] = WaveTypes[--WaveTypeCount];
            w--;
        }
    }
}

void ProcMissionManager::Spawn_Wave(HouseClass * house)
{
    ProcThreatType high = Current_Threat();
    ProcThreatType low = (ProcThreatType)max((int)PROC_THREAT_SCOUT, (int)high - 1);

    ProcTeamPreset const * preset = ProcPresets.Pick_Team(Proc_Side_Of(house), PROC_TEAM_ATTACK, low, high, house->Control.TechLevel);
    if (preset == NULL) return;

    HouseClass * victim = Pick_Victim(house);
    if (victim == NULL) return;

    CELL home = Home_Cell(house);
    TeamTypeClass * tt = Make_Wave_Type(preset, house, victim, home);
    if (tt == NULL) return;

    bool ok = Deliver_Wave(tt, home);

    char buf[128];
    sprintf(buf, "ProcMission: wave %s (%s, threat %d) from %s -> %s", preset->Name, ok ? "sent" : "failed",
        (int)preset->Threat, house->IniName, victim->IniName);
    GlyphX_Debug_Print(buf);

    if (!ok) {
        WaveTypes[--WaveTypeCount] = NULL;
        delete tt;
    }
}

void ProcMissionManager::Director_AI()
{
    if (!Settings.IsWaves || Elapsed < NextWaveFrame) return;

    Collect_Wave_Types();

    if (Active_Waves() < Diff.MaxActiveWaves) {
        for (int tries = 0; tries < EnemyCount; tries++) {
            // Waves come out of a base; a house whose base is gone sends no more.
            ProcHouseInfo const & enemy = Enemies[NextAttacker++ % EnemyCount];
            if (Is_Alive(enemy.House) && enemy.House->CurBuildings > 0) {
                Spawn_Wave(enemy.House);
                break;
            }
        }
        int seconds = Random_Pick(Settings.WaveMinSeconds, Settings.WaveMaxSeconds);
        NextWaveFrame = Elapsed + (seconds * TICKS_PER_SECOND * Diff.WaveIntervalPct) / 100;
    } else {
        // Too many waves alive; look again shortly.
        NextWaveFrame = Elapsed + TICKS_PER_SECOND * 10;
    }
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

    if ((Elapsed % PROC_OBJECTIVE_CHECK_RATE) == 0) {
        Update_Objectives();
    }

    if (!IsFinished) {
        Director_AI();
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
    return Coord_Cell(house->Center);
}

void ProcMissionManager::Message(char const * text) const
{
    GlyphX_Debug_Print(text);
    if (Commander == NULL) return;

    Logic_Switch_Player_Context(Commander);
    On_Message(text, PROC_MESSAGE_SECONDS, -1);
}
