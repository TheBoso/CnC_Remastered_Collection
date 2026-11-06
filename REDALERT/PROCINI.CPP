// PROCINI.CPP
//
// Preset library loader for the procedural mission generator.
// Reads PROCMISSION.INI, PROCTEAMS.INI, PROCBASES.INI and PROCOBJECTIVES.INI.
//

#include "function.h"
#include "procmission.h"
#include "procini.h"

extern void GlyphX_Debug_Print(const char *debug_text);


/*******************************************************************************
 * NAME TABLES
 ******************************************************************************/

static char const * s_SideNames[PROC_SIDE_COUNT] = {"Any", "Allied", "Soviet"};
static char const * s_ThreatNames[PROC_THREAT_COUNT] = {"Scout", "Light", "Medium", "Heavy", "Overwhelming"};
static char const * s_TeamRoleNames[PROC_TEAM_ROLE_COUNT] = {"Attack", "Defense", "Patrol", "Squad", "Reinforce"};
static char const * s_BaseRoleNames[PROC_BASE_ROLE_COUNT] = {"Main", "Outpost", "Defend", "Camp", "Sentry"};
static char const * s_KindNames[PROC_OBJ_KIND_COUNT] = {"DestroyBases", "Survive", "Destroy", "Capture", "Protect", "Assassinate", "Rescue", "Control"};
static char const * s_SlotNames[PROC_SLOT_COUNT] = {"Primary", "Secondary", "Either", "Event"};
static char const * s_OwnerNames[PROC_OWNER_COUNT] = {"Enemy", "Neutral", "Player", "Outpost", "Ally"};
static char const * s_EventKindNames[PROC_EVENT_KIND_COUNT] = {"Objective", "Counterattack", "Reinforce"};
static char const * s_WhenNames[PROC_WHEN_COUNT] = {"Any", "Ahead", "Behind", "AllyAttacked", "SentryLost", "Quiet"};
static char const * s_SpawnNames[PROC_SPAWN_COUNT] = {"Base", "Edge"};
static char const * s_StartNames[PROC_START_COUNT] = {"MCV", "Base", "Random", "Same"};
static char const * s_DiffNames[DIFF_COUNT] = {"Easy", "Normal", "Hard"};

static char const * s_QuarryNames[QUARRY_COUNT] = {
    "None", "Anything", "Buildings", "Harvesters", "Infantry", "Vehicles",
    "Vessels", "Factories", "Defense", "Threat", "Power", "Fakes"
};


static int Name_Index(char const * name, char const * const * table, int count, int defvalue)
{
    if (name == NULL || *name == '\0') return defvalue;
    for (int i = 0; i < count; i++) {
        if (stricmp(name, table[i]) == 0) return i;
    }
    return defvalue;
}

char const * Proc_Side_Name(ProcSideType side)
{
    return (side < PROC_SIDE_COUNT) ? s_SideNames[side] : "?";
}

char const * Proc_Objective_Kind_Name(ProcObjectiveKindType kind)
{
    return (kind < PROC_OBJ_KIND_COUNT) ? s_KindNames[kind] : "?";
}


/*******************************************************************************
 * CONSTRUCTORS
 ******************************************************************************/

ProcTeamPreset::ProcTeamPreset() :
    Side(PROC_SIDE_ANY), Role(PROC_TEAM_ATTACK), Threat(PROC_THREAT_LIGHT), Weight(10),
    MemberCount(0), MissionCount(0), IsGathering(true), IsNaval(false), IsShipHunter(false), IsSuicide(false), IsRoundAbout(false)
{
    Name[0] = '\0';
}

ProcBasePreset::ProcBasePreset() :
    Side(PROC_SIDE_ANY), Role(PROC_BASE_MAIN), MinTech(1), Weight(10), EntryCount(0), GuardCount(0),
    Harvesters(1), IsAutoPower(true), IsNaval(false), Credits(5000)
{
    Name[0] = '\0';
    for (int d = 0; d < DIFF_COUNT; d++) DiffWeight[d] = Weight;
}

ProcEventPreset::ProcEventPreset() :
    Kind(PROC_EVENT_OBJECTIVE), PlayerSide(PROC_SIDE_ANY), MinTech(1), Weight(10), When(PROC_WHEN_ANY), MinMinutes(0),
    IsOnce(true), Waves(0), ThreatBoost(0)
{
    Name[0] = '\0';
    Objective[0] = '\0';
    Text[0] = '\0';
}

ProcObjectivePreset::ProcObjectivePreset() :
    Kind(PROC_OBJ_DESTROY_BASES), Slot(PROC_SLOT_EITHER), PlayerSide(PROC_SIDE_ANY), MinTech(1), Weight(10),
    MinMinutes(0), MaxMinutes(0), TargetCount(0), Owner(PROC_OWNER_ENEMY), RewardCredits(0), RewardUnitCount(0), IsRewardReveal(false),
    IsSquadStart(false), SquadCount(0), PlayerCredits(-1), WavesOverride(-1), WaveDelaySeconds(0),
    EnemyProductionOverride(-1), WaveRate(100), ExtraWaves(0), EnemyReinforceMinutes(0), IsHuntingReinforcements(false), Patrols(0), PatrolThreat(PROC_THREAT_OVERWHELMING),
    TargetUnitCount(0), IsConvoy(false), PrisonerCount(0), RescueNeeded(0), PointCount(0)
{
    RewardText[0] = '\0';
    Name[0] = '\0';
    Guard[0] = '\0';
    PlayerBase[0] = '\0';
    OutpostBase[0] = '\0';
    TargetStrength = 100;
    IsAbandoned = false;
    Bonus[0] = '\0';
    Text[0] = '\0';
}

ProcDifficultyPreset::ProcDifficultyPreset() :
    EnemyCreditsPct(100), DefenseScalePct(100), GuardScalePct(100), WaveSizePct(100), WaveIntervalPct(100),
    MaxActiveWaves(2), OutpostChance(50), FirstWaveSeconds(240), StartThreat(PROC_THREAT_SCOUT),
    MaxThreat(PROC_THREAT_HEAVY), EscalateMinutes(5), NavalChance(35), PlayerReinforceMinutes(8),
    AllyReinforceMinutes(8), EnemyReinforceMinutes(10), FactoryScalePct(100), SuperweaponCount(0),
    BaseWaveRatePct(100), BaseWaveSizePct(100), BaseFirstWavePct(100), BaseThreatSteps(0), BaseExtraWaves(0),
    MinConstructionYards(1), MinRefineries(1), EventFirstMinutes(6), EventMinMinutes(5), EventMaxMinutes(8),
    SentriesMin(0), SentriesMax(0)
{
}

ProcSettings::ProcSettings() :
    Difficulty(DIFF_NORMAL), SecondaryMin(0), SecondaryMax(2), TechLevel(0), IsWaves(true), IsEnemyProduction(false), IsReinforcements(true),
    IsKeepStartUnits(false), IsAllMaps(false), IsSpectate(false), IsSentries(true), IsEvents(true), WaveMinSeconds(90), WaveMaxSeconds(180), SpawnMode(PROC_SPAWN_BASE),
    PlayerStart(PROC_START_RANDOM), AllyStart(PROC_START_SAME), PlayerBaseChance(35)
{
    strcpy(Primary, "RANDOM");
}

ProcPresetLibrary::ProcPresetLibrary() : TeamCount(0), BaseCount(0), ObjectiveCount(0)
{
}


/*******************************************************************************
 * VALUE PARSERS
 ******************************************************************************/

static TechnoTypeClass const * Techno_From_Name(char const * name)
{
    UnitType unit = UnitTypeClass::From_Name(name);
    if (unit != UNIT_NONE) return &UnitTypeClass::As_Reference(unit);

    InfantryType inf = InfantryTypeClass::From_Name(name);
    if (inf != INFANTRY_NONE) return &InfantryTypeClass::As_Reference(inf);

    VesselType vessel = VesselTypeClass::From_Name(name);
    if (vessel != VESSEL_NONE) return &VesselTypeClass::As_Reference(vessel);

    AircraftType air = AircraftTypeClass::From_Name(name);
    if (air != AIRCRAFT_NONE) return &AircraftTypeClass::As_Reference(air);

    return NULL;
}

/*
**	Splits "A:1:2,B:3" into tokens. Calls back with the colon separated fields of each entry.
*/
typedef void (*ProcFieldCallback)(char ** fields, int fieldcount, void * context);

static void Parse_List(char const * text, ProcFieldCallback callback, void * context)
{
    char buffer[512];
    strncpy(buffer, text, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char * entry = buffer;
    while (entry != NULL && *entry != '\0') {
        char * next = strchr(entry, ',');
        if (next != NULL) *next++ = '\0';

        char * fields[4];
        int fieldcount = 0;
        char * field = entry;
        while (field != NULL && fieldcount < 4) {
            while (*field == ' ' || *field == '\t') field++;
            char * colon = strchr(field, ':');
            if (colon != NULL) *colon++ = '\0';
            char * end = field + strlen(field);
            while (end > field && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
            fields[fieldcount++] = field;
            field = colon;
        }
        if (fieldcount > 0 && *fields[0] != '\0') {
            callback(fields, fieldcount, context);
        }
        entry = next;
    }
}

struct TechnoListContext {
    ProcTechnoCount * List;
    int Max;
    int * Count;
    char const * Section;
};

static void Techno_List_Callback(char ** fields, int fieldcount, void * context)
{
    TechnoListContext * ctx = (TechnoListContext *)context;
    if (*ctx->Count >= ctx->Max) return;

    TechnoTypeClass const * type = Techno_From_Name(fields[0]);
    if (type == NULL) {
        char buf[128];
        sprintf(buf, "ProcINI: [%s] unknown unit type '%s'", ctx->Section, fields[0]);
        GlyphX_Debug_Print(buf);
        return;
    }
    ProcTechnoCount & item = ctx->List[(*ctx->Count)++];
    item.Type = type;
    item.Count = (fieldcount > 1) ? max(1, atoi(fields[1])) : 1;
}

static void Parse_Techno_List(CCINIClass & ini, char const * section, char const * entry, ProcTechnoCount * list, int maxcount, int & count)
{
    char buffer[512];
    count = 0;
    if (ini.Get_String(section, entry, "", buffer, sizeof(buffer)) > 0) {
        TechnoListContext ctx = {list, maxcount, &count, section};
        Parse_List(buffer, Techno_List_Callback, &ctx);
    }
}

struct BaseEntryContext {
    ProcBasePreset * Base;
    bool IsDefense;
    char const * Section;
};

static void Base_Entry_Callback(char ** fields, int fieldcount, void * context)
{
    BaseEntryContext * ctx = (BaseEntryContext *)context;
    ProcBasePreset & base = *ctx->Base;
    if (base.EntryCount >= PROC_MAX_BASE_ENTRIES) return;

    StructType type = BuildingTypeClass::From_Name(fields[0]);
    if (type == STRUCT_NONE) {
        char buf[128];
        sprintf(buf, "ProcINI: [%s] unknown structure '%s'", ctx->Section, fields[0]);
        GlyphX_Debug_Print(buf);
        return;
    }

    // TYPE[:COUNT[:MINRADIUS[:MAXRADIUS]]]
    ProcBaseEntry & item = base.Entries[base.EntryCount++];
    item.Type = type;
    item.IsDefense = ctx->IsDefense;
    item.Count = (fieldcount > 1) ? max(0, atoi(fields[1])) : 1;
    if (ctx->IsDefense) {
        item.MinRadius = 7;
        item.MaxRadius = 13;
    }
    if (fieldcount > 2) item.MinRadius = max(0, atoi(fields[2]));
    if (fieldcount > 3) item.MaxRadius = max(item.MinRadius, atoi(fields[3]));
}

static void Parse_Base_Entries(CCINIClass & ini, char const * section, char const * entry, ProcBasePreset & base, bool isdefense)
{
    char buffer[512];
    if (ini.Get_String(section, entry, "", buffer, sizeof(buffer)) > 0) {
        BaseEntryContext ctx = {&base, isdefense, section};
        Parse_List(buffer, Base_Entry_Callback, &ctx);
    }
}

struct MissionListContext {
    ProcTeamPreset * Team;
    char const * Section;
};

static void Mission_Callback(char ** fields, int fieldcount, void * context)
{
    MissionListContext * ctx = (MissionListContext *)context;
    ProcTeamPreset & team = *ctx->Team;
    if (team.MissionCount >= PROC_MAX_TEAM_MISSIONS) return;

    ProcTeamMission & mission = team.Missions[team.MissionCount];
    char const * arg = (fieldcount > 1) ? fields[1] : "";

    if (stricmp(fields[0], "Attack") == 0) {
        mission.Mission = TMISSION_ATTACK;
        mission.Data = Name_Index(arg, s_QuarryNames, QUARRY_COUNT, QUARRY_ANYTHING);
    } else if (stricmp(fields[0], "Hunt") == 0) {
        mission.Mission = TMISSION_DO;
        mission.Data = MISSION_HUNT;
    } else if (stricmp(fields[0], "Guard") == 0) {
        // Guard for N tenths of a minute
        mission.Mission = TMISSION_GUARD;
        mission.Data = (*arg != '\0') ? atoi(arg) : 10;
    } else if (stricmp(fields[0], "Loop") == 0) {
        mission.Mission = TMISSION_LOOP;
        mission.Data = (*arg != '\0') ? atoi(arg) : 0;
    } else {
        char buf[128];
        sprintf(buf, "ProcINI: [%s] unknown team mission '%s'", ctx->Section, fields[0]);
        GlyphX_Debug_Print(buf);
        return;
    }
    team.MissionCount++;
}

static void Parse_Name(CCINIClass & ini, char const * section, char const * entry, char * out, int size)
{
    ini.Get_String(section, entry, "", out, size);
    out[size - 1] = '\0';
}

static int Get_Enum(CCINIClass & ini, char const * section, char const * entry, char const * const * table, int count, int defvalue)
{
    char buffer[64];
    ini.Get_String(section, entry, "", buffer, sizeof(buffer));
    return Name_Index(buffer, table, count, defvalue);
}


/*******************************************************************************
 * FILE LOADING
 ******************************************************************************/

extern HINSTANCE ProgramInstance;     // This DLL (set in DllMain)

/*
**	The client loads a mod's RedAlert.dll from the mod's Data folder but does not put that
**	folder on the CCFileClass search path, so look next to the DLL first. Fall back to the
**	normal search (mod paths, content directory), which is what the ProcTest harness uses.
*/
static bool Load_INI(char const * filename, CCINIClass & ini)
{
    char path[_MAX_PATH];
    if (GetModuleFileNameA(ProgramInstance, path, sizeof(path)) > 0) {
        char * slash = strrchr(path, '\\');
        if (slash != NULL && (slash - path) + 1 + strlen(filename) < sizeof(path)) {
            strcpy(slash + 1, filename);
            RawFileClass local(path);
            if (local.Is_Available()) {
                return ini.Load(local, false);
            }
        }
    }

    CCFileClass file(filename);
    if (!file.Is_Available()) {
        char buf[128];
        sprintf(buf, "ProcINI: %s not found (next to RedAlert.dll or on the mod search path)", filename);
        GlyphX_Debug_Print(buf);
        return false;
    }
    return ini.Load(file, false);
}

/*
**	Each preset file has an index section ([Teams], [Bases], [Objectives]) listing the
**	preset sections in the file, in the same style as RULES.INI type lists.
*/
template<class T, class F>
static int Load_Presets(CCINIClass & ini, char const * index, T * presets, int maxcount, F parser)
{
    int count = 0;
    int entries = ini.Entry_Count(index);
    for (int i = 0; i < entries && count < maxcount; i++) {
        char section[PROC_NAME_LEN];
        ini.Get_String(index, ini.Get_Entry(index, i), "", section, sizeof(section));
        if (section[0] == '\0' || !ini.Is_Present(section)) continue;

        T & preset = presets[count];
        preset = T();
        strncpy(preset.Name, section, PROC_NAME_LEN - 1);
        preset.Name[PROC_NAME_LEN - 1] = '\0';
        if (parser(ini, section, preset)) {
            count++;
        }
    }
    return count;
}

static bool Parse_Team(CCINIClass & ini, char const * section, ProcTeamPreset & team)
{
    char buffer[512];

    team.Side = (ProcSideType)Get_Enum(ini, section, "Side", s_SideNames, PROC_SIDE_COUNT, PROC_SIDE_ANY);
    team.Role = (ProcTeamRoleType)Get_Enum(ini, section, "Role", s_TeamRoleNames, PROC_TEAM_ROLE_COUNT, PROC_TEAM_ATTACK);
    team.Threat = (ProcThreatType)Get_Enum(ini, section, "Threat", s_ThreatNames, PROC_THREAT_COUNT, PROC_THREAT_LIGHT);
    team.Weight = ini.Get_Int(section, "Weight", 10);
    team.IsGathering = ini.Get_Bool(section, "Gather", team.Role == PROC_TEAM_ATTACK);
    team.IsSuicide = ini.Get_Bool(section, "Suicide", false);
    team.IsRoundAbout = ini.Get_Bool(section, "RoundAbout", false);

    Parse_Techno_List(ini, section, "Members", team.Members, PROC_MAX_MEMBERS, team.MemberCount);

    // Ships make it a naval team: it can only come from a naval yard, by sea, and doesn't gather on land.
    for (int i = 0; i < team.MemberCount; i++) {
        if (team.Members[i].Type->What_Am_I() == RTTI_VESSELTYPE) team.IsNaval = true;
    }
    if (team.IsNaval) team.IsGathering = false;
    if (ini.Get_String(section, "NavalTarget", "Coast", buffer, sizeof(buffer)) > 0) team.IsShipHunter = (stricmp(buffer, "Ships") == 0);

    if (ini.Get_String(section, "Missions", "Attack:Anything", buffer, sizeof(buffer)) > 0) {
        MissionListContext ctx = {&team, section};
        Parse_List(buffer, Mission_Callback, &ctx);
    }

    return team.MemberCount > 0;
}

static bool Parse_Base(CCINIClass & ini, char const * section, ProcBasePreset & base)
{
    base.Side = (ProcSideType)Get_Enum(ini, section, "Side", s_SideNames, PROC_SIDE_COUNT, PROC_SIDE_ANY);
    base.Role = (ProcBaseRoleType)Get_Enum(ini, section, "Role", s_BaseRoleNames, PROC_BASE_ROLE_COUNT, PROC_BASE_MAIN);
    base.MinTech = ini.Get_Int(section, "MinTech", 1);
    base.Weight = ini.Get_Int(section, "Weight", 10);
    static char const * const s_DiffWeightKeys[DIFF_COUNT] = {"WeightEasy", "WeightNormal", "WeightHard"};
    for (int d = 0; d < DIFF_COUNT; d++) base.DiffWeight[d] = max(0, ini.Get_Int(section, s_DiffWeightKeys[d], base.Weight));
    base.Harvesters = ini.Get_Int(section, "Harvesters", 1);
    base.IsAutoPower = ini.Get_Bool(section, "AutoPower", true);
    base.IsNaval = ini.Get_Bool(section, "Naval", false);
    base.Credits = ini.Get_Int(section, "Credits", 5000);

    Parse_Base_Entries(ini, section, "Structures", base, false);
    Parse_Base_Entries(ini, section, "Defenses", base, true);
    Parse_Techno_List(ini, section, "Guards", base.Guards, PROC_MAX_BASE_GUARDS, base.GuardCount);

    return base.EntryCount > 0;
}

struct TargetListContext {
    ProcObjectivePreset * Objective;
    char const * Section;
};

static void Target_Callback(char ** fields, int, void * context)
{
    TargetListContext * ctx = (TargetListContext *)context;
    ProcObjectivePreset & obj = *ctx->Objective;
    if (obj.TargetCount >= PROC_MAX_TARGET_TYPES) return;

    StructType type = BuildingTypeClass::From_Name(fields[0]);
    if (type == STRUCT_NONE) {
        char buf[128];
        sprintf(buf, "ProcINI: [%s] unknown target structure '%s'", ctx->Section, fields[0]);
        GlyphX_Debug_Print(buf);
        return;
    }
    // An objective nobody can complete is worse than none (e.g. BIO is not Capturable in the rules).
    if (obj.Kind == PROC_OBJ_CAPTURE && !BuildingTypeClass::As_Reference(type).IsCaptureable) {
        char buf[128];
        sprintf(buf, "ProcINI: [%s] target '%s' cannot be captured, skipped", ctx->Section, fields[0]);
        GlyphX_Debug_Print(buf);
        return;
    }
    obj.Targets[obj.TargetCount++] = type;
}

static bool Parse_Objective(CCINIClass & ini, char const * section, ProcObjectivePreset & obj)
{
    char buffer[512];

    obj.Kind = (ProcObjectiveKindType)Get_Enum(ini, section, "Kind", s_KindNames, PROC_OBJ_KIND_COUNT, PROC_OBJ_DESTROY_BASES);
    obj.Slot = (ProcObjectiveSlotType)Get_Enum(ini, section, "Slot", s_SlotNames, PROC_SLOT_COUNT, PROC_SLOT_EITHER);
    obj.PlayerSide = (ProcSideType)Get_Enum(ini, section, "PlayerSide", s_SideNames, PROC_SIDE_COUNT, PROC_SIDE_ANY);
    // Default owners: your base for Protect, a remote camp for Assassinate / Rescue, no one for Control.
    ProcOwnerType owner = PROC_OWNER_ENEMY;
    if (obj.Kind == PROC_OBJ_PROTECT) owner = PROC_OWNER_PLAYER;
    if (obj.Kind == PROC_OBJ_ASSASSINATE || obj.Kind == PROC_OBJ_RESCUE) owner = PROC_OWNER_OUTPOST;
    if (obj.Kind == PROC_OBJ_CONTROL) owner = PROC_OWNER_NEUTRAL;
    obj.Owner = (ProcOwnerType)Get_Enum(ini, section, "Owner", s_OwnerNames, PROC_OWNER_COUNT, owner);
    obj.MinTech = ini.Get_Int(section, "MinTech", 1);
    obj.Weight = ini.Get_Int(section, "Weight", 10);
    obj.MinMinutes = ini.Get_Int(section, "MinMinutes", 0);
    obj.MaxMinutes = max(obj.MinMinutes, ini.Get_Int(section, "MaxMinutes", obj.MinMinutes));
    obj.RewardCredits = ini.Get_Int(section, "RewardCredits", 0);
    obj.TargetStrength = Bound(ini.Get_Int(section, "TargetStrength", 100), 1, 100);
    obj.IsAbandoned = ini.Get_Bool(section, "Abandoned", false);

    Parse_Name(ini, section, "Guard", obj.Guard, sizeof(obj.Guard));
    Parse_Name(ini, section, "PlayerBase", obj.PlayerBase, sizeof(obj.PlayerBase));
    Parse_Name(ini, section, "OutpostBase", obj.OutpostBase, sizeof(obj.OutpostBase));
    Parse_Name(ini, section, "Bonus", obj.Bonus, sizeof(obj.Bonus));
    Parse_Name(ini, section, "Text", obj.Text, sizeof(obj.Text));

    // Squad missions
    ini.Get_String(section, "PlayerStart", "", buffer, sizeof(buffer));
    obj.IsSquadStart = (stricmp(buffer, "Squad") == 0);
    if (ini.Get_String(section, "Squad", "", buffer, sizeof(buffer)) > 0) {
        char * name = strtok(buffer, ",");
        while (name != NULL && obj.SquadCount < PROC_MAX_SQUAD_CHOICES) {
            while (*name == ' ') name++;
            strncpy(obj.Squads[obj.SquadCount], name, PROC_NAME_LEN - 1);
            obj.Squads[obj.SquadCount][PROC_NAME_LEN - 1] = '\0';
            obj.SquadCount++;
            name = strtok(NULL, ",");
        }
    }
    obj.PlayerCredits = ini.Get_Int(section, "PlayerCredits", -1);
    if (ini.Is_Present(section, "Waves")) obj.WavesOverride = ini.Get_Bool(section, "Waves", true) ? 1 : 0;
    obj.WaveDelaySeconds = ini.Get_Int(section, "WaveDelaySeconds", 0);
    if (ini.Is_Present(section, "EnemyProduction")) obj.EnemyProductionOverride = ini.Get_Bool(section, "EnemyProduction", false) ? 1 : 0;
    obj.WaveRate = Bound(ini.Get_Int(section, "WaveRate", 100), 10, 1000);
    obj.ExtraWaves = Bound(ini.Get_Int(section, "ExtraWaves", 0), 0, 5);
    obj.EnemyReinforceMinutes = max(0, ini.Get_Int(section, "EnemyReinforceMinutes", 0));
    obj.IsHuntingReinforcements = ini.Get_Bool(section, "HuntingReinforcements", false);
    obj.Patrols = Bound(ini.Get_Int(section, "Patrols", 0), 0, PROC_MAX_PATROLS);
    obj.PatrolThreat = (ProcThreatType)Get_Enum(ini, section, "PatrolThreat", s_ThreatNames, PROC_THREAT_COUNT, PROC_THREAT_OVERWHELMING);
    Parse_Techno_List(ini, section, "RewardUnits", obj.RewardUnits, PROC_MAX_MEMBERS, obj.RewardUnitCount);
    obj.IsRewardReveal = ini.Get_Bool(section, "RewardReveal", false);
    ini.Get_String(section, "RewardText", "", obj.RewardText, sizeof(obj.RewardText));
    Parse_Techno_List(ini, section, "TargetUnit", obj.TargetUnits, PROC_MAX_TARGET_TYPES, obj.TargetUnitCount);
    obj.IsConvoy = ini.Get_Bool(section, "Convoy", false);
    Parse_Techno_List(ini, section, "Prisoners", obj.Prisoners, PROC_MAX_MEMBERS, obj.PrisonerCount);
    obj.RescueNeeded = max(0, ini.Get_Int(section, "RescueNeeded", 0));
    obj.PointCount = Bound(ini.Get_Int(section, "Points", 0), 0, PROC_MAX_POINTS);

    if (ini.Get_String(section, "Targets", "", buffer, sizeof(buffer)) > 0) {
        TargetListContext ctx = {&obj, section};
        Parse_List(buffer, Target_Callback, &ctx);
    }

    // Structure objectives are useless without a target; timed objectives need a duration.
    bool needstarget = (obj.Kind == PROC_OBJ_DESTROY || obj.Kind == PROC_OBJ_CAPTURE || obj.Kind == PROC_OBJ_PROTECT);
    bool needstime = (obj.Kind == PROC_OBJ_SURVIVE || obj.Kind == PROC_OBJ_PROTECT || obj.Kind == PROC_OBJ_CONTROL);
    if (needstarget && obj.TargetCount == 0) return false;
    if (needstime && obj.MinMinutes <= 0) return false;
    if (obj.Kind == PROC_OBJ_ASSASSINATE && obj.TargetUnitCount == 0) return false;
    if (obj.Kind == PROC_OBJ_RESCUE && obj.PrisonerCount == 0) return false;
    if (obj.Kind == PROC_OBJ_CONTROL && obj.PointCount == 0) return false;
    if (obj.IsSquadStart && obj.SquadCount == 0) return false;
    return true;
}

static bool Parse_Event(CCINIClass & ini, char const * section, ProcEventPreset & event)
{
    char buffer[256];
    event.Kind = (ProcEventKindType)Get_Enum(ini, section, "Kind", s_EventKindNames, PROC_EVENT_KIND_COUNT, PROC_EVENT_OBJECTIVE);
    event.PlayerSide = (ProcSideType)Get_Enum(ini, section, "PlayerSide", s_SideNames, PROC_SIDE_COUNT, PROC_SIDE_ANY);
    event.MinTech = ini.Get_Int(section, "MinTech", 1);
    event.Weight = max(0, ini.Get_Int(section, "Weight", 10));
    event.MinMinutes = max(0, ini.Get_Int(section, "MinMinutes", 0));
    event.IsOnce = ini.Get_Bool(section, "Once", true);
    event.Waves = Bound(ini.Get_Int(section, "Waves", (event.Kind == PROC_EVENT_OBJECTIVE) ? 0 : 1), 0, 4);
    event.ThreatBoost = Bound(ini.Get_Int(section, "ThreatBoost", 0), 0, PROC_THREAT_COUNT - 1);
    Parse_Name(ini, section, "Objective", event.Objective, sizeof(event.Objective));
    ini.Get_String(section, "Text", "", event.Text, sizeof(event.Text));

    // When=Ahead,Quiet: any of them.
    event.When = 0;
    if (ini.Get_String(section, "When", "Any", buffer, sizeof(buffer)) > 0) {
        for (char * token = strtok(buffer, ", "); token != NULL; token = strtok(NULL, ", ")) {
            bool known = false;
            for (int w = 0; w < PROC_WHEN_COUNT; w++) {
                if (stricmp(token, s_WhenNames[w]) == 0) {
                    event.When |= (1 << w);
                    known = true;
                }
            }
            if (!known) {
                char buf[128];
                sprintf(buf, "ProcINI: [%s] unknown When= situation '%s'", section, token);
                GlyphX_Debug_Print(buf);
            }
        }
    }
    if (event.When == 0) event.When = PROC_WHEN_ANY;
    if (event.Kind == PROC_EVENT_OBJECTIVE && event.Objective[0] == '\0') return false;
    return true;
}

static void Parse_Difficulty(CCINIClass & ini, char const * section, ProcDifficultyPreset & diff)
{
    diff.EnemyCreditsPct = ini.Get_Int(section, "EnemyCredits", diff.EnemyCreditsPct);
    diff.DefenseScalePct = ini.Get_Int(section, "DefenseScale", diff.DefenseScalePct);
    diff.GuardScalePct = ini.Get_Int(section, "GuardScale", diff.GuardScalePct);
    diff.WaveSizePct = ini.Get_Int(section, "WaveSize", diff.WaveSizePct);
    diff.WaveIntervalPct = ini.Get_Int(section, "WaveInterval", diff.WaveIntervalPct);
    diff.MaxActiveWaves = ini.Get_Int(section, "MaxActiveWaves", diff.MaxActiveWaves);
    diff.OutpostChance = ini.Get_Int(section, "OutpostChance", diff.OutpostChance);
    diff.FirstWaveSeconds = ini.Get_Int(section, "FirstWaveSeconds", diff.FirstWaveSeconds);
    diff.StartThreat = (ProcThreatType)Get_Enum(ini, section, "StartThreat", s_ThreatNames, PROC_THREAT_COUNT, diff.StartThreat);
    diff.MaxThreat = (ProcThreatType)Get_Enum(ini, section, "MaxThreat", s_ThreatNames, PROC_THREAT_COUNT, diff.MaxThreat);
    diff.EscalateMinutes = max(1, ini.Get_Int(section, "EscalateMinutes", diff.EscalateMinutes));
    diff.NavalChance = Bound(ini.Get_Int(section, "NavalChance", diff.NavalChance), 0, 100);
    diff.PlayerReinforceMinutes = max(0, ini.Get_Int(section, "PlayerReinforceMinutes", diff.PlayerReinforceMinutes));
    diff.AllyReinforceMinutes = max(0, ini.Get_Int(section, "AllyReinforceMinutes", diff.AllyReinforceMinutes));
    diff.EnemyReinforceMinutes = max(0, ini.Get_Int(section, "EnemyReinforceMinutes", diff.EnemyReinforceMinutes));
    diff.FactoryScalePct = max(0, ini.Get_Int(section, "FactoryScale", diff.FactoryScalePct));
    diff.BaseWaveRatePct = max(10, ini.Get_Int(section, "BaseStartWaveRate", diff.BaseWaveRatePct));
    diff.BaseWaveSizePct = max(10, ini.Get_Int(section, "BaseStartWaveSize", diff.BaseWaveSizePct));
    diff.BaseFirstWavePct = max(10, ini.Get_Int(section, "BaseStartFirstWave", diff.BaseFirstWavePct));
    diff.BaseThreatSteps = Bound(ini.Get_Int(section, "BaseStartThreat", diff.BaseThreatSteps), 0, PROC_THREAT_COUNT - 1);
    diff.BaseExtraWaves = Bound(ini.Get_Int(section, "BaseStartExtraWaves", diff.BaseExtraWaves), 0, 5);
    diff.MinConstructionYards = Bound(ini.Get_Int(section, "MinConstructionYards", diff.MinConstructionYards), 1, 4);
    diff.MinRefineries = Bound(ini.Get_Int(section, "MinRefineries", diff.MinRefineries), 0, 4);
    diff.EventFirstMinutes = max(0, ini.Get_Int(section, "EventFirstMinutes", diff.EventFirstMinutes));
    diff.EventMinMinutes = max(1, ini.Get_Int(section, "EventMinMinutes", diff.EventMinMinutes));
    diff.EventMaxMinutes = max(diff.EventMinMinutes, ini.Get_Int(section, "EventMaxMinutes", diff.EventMaxMinutes));
    diff.SentriesMin = Bound(ini.Get_Int(section, "SentriesMin", diff.SentriesMin), 0, PROC_MAX_SENTRIES);
    diff.SentriesMax = Bound(ini.Get_Int(section, "SentriesMax", diff.SentriesMax), diff.SentriesMin, PROC_MAX_SENTRIES);

    char buffer[128];
    if (ini.Get_String(section, "Superweapons", "", buffer, sizeof(buffer)) > 0) {
        diff.SuperweaponCount = 0;
        for (char * token = strtok(buffer, ", "); token != NULL && diff.SuperweaponCount < ARRAY_SIZE(diff.Superweapons); token = strtok(NULL, ", ")) {
            StructType type = BuildingTypeClass::From_Name(token);
            if (type == STRUCT_NONE) {
                char buf[128];
                sprintf(buf, "ProcINI: [%s] unknown superweapon structure '%s'", section, token);
                GlyphX_Debug_Print(buf);
                continue;
            }
            diff.Superweapons[diff.SuperweaponCount++] = type;
        }
    }
}

void ProcPresetLibrary::Read_Settings(CCINIClass & ini, char const * section, ProcSettings & settings) const
{
    if (!ini.Is_Present(section)) return;

    char buffer[64];
    if (ini.Get_String(section, "Difficulty", "", buffer, sizeof(buffer)) > 0) {
        settings.Difficulty = (stricmp(buffer, "Random") == 0) ? -1 : Name_Index(buffer, s_DiffNames, DIFF_COUNT, settings.Difficulty);
    }
    if (ini.Get_String(section, "Primary", "", buffer, sizeof(buffer)) > 0) {
        strncpy(settings.Primary, buffer, PROC_NAME_LEN - 1);
        settings.Primary[PROC_NAME_LEN - 1] = '\0';
    }
    settings.SecondaryMin = ini.Get_Int(section, "SecondaryMin", settings.SecondaryMin);
    settings.SecondaryMax = max(settings.SecondaryMin, ini.Get_Int(section, "SecondaryMax", settings.SecondaryMax));
    settings.TechLevel = ini.Get_Int(section, "TechLevel", settings.TechLevel);
    settings.IsWaves = ini.Get_Bool(section, "Waves", settings.IsWaves);
    settings.IsEnemyProduction = ini.Get_Bool(section, "EnemyProduction", settings.IsEnemyProduction);
    settings.IsReinforcements = ini.Get_Bool(section, "Reinforcements", settings.IsReinforcements);
    settings.IsKeepStartUnits = ini.Get_Bool(section, "KeepStartUnits", settings.IsKeepStartUnits);
    settings.IsAllMaps = ini.Get_Bool(section, "AllMaps", settings.IsAllMaps);
    settings.IsSpectate = ini.Get_Bool(section, "Spectate", settings.IsSpectate);
    settings.IsSentries = ini.Get_Bool(section, "Sentries", settings.IsSentries);
    settings.IsEvents = ini.Get_Bool(section, "Events", settings.IsEvents);
    settings.WaveMinSeconds = max(10, ini.Get_Int(section, "WaveMinSeconds", settings.WaveMinSeconds));
    settings.WaveMaxSeconds = max(settings.WaveMinSeconds, ini.Get_Int(section, "WaveMaxSeconds", settings.WaveMaxSeconds));
    settings.SpawnMode = (ProcSpawnModeType)Get_Enum(ini, section, "SpawnMode", s_SpawnNames, PROC_SPAWN_COUNT, settings.SpawnMode);
    settings.PlayerStart = (ProcStartType)Get_Enum(ini, section, "PlayerStart", s_StartNames, PROC_START_COUNT, settings.PlayerStart);
    settings.AllyStart = (ProcStartType)Get_Enum(ini, section, "AllyStart", s_StartNames, PROC_START_COUNT, settings.AllyStart);
    settings.PlayerBaseChance = Bound(ini.Get_Int(section, "PlayerBaseChance", settings.PlayerBaseChance), 0, 100);

    // "Same" only makes sense for allies.
    if (settings.PlayerStart == PROC_START_SAME) settings.PlayerStart = PROC_START_RANDOM;
}

bool ProcPresetLibrary::Load()
{
    char buf[160];

    Settings = ProcSettings();
    for (int d = 0; d < DIFF_COUNT; d++) {
        Difficulty[d] = ProcDifficultyPreset();
    }
    TeamCount = BaseCount = ObjectiveCount = EventCount = 0;

    {
        CCINIClass ini;
        if (Load_INI(PROC_SETTINGS_INI, ini)) {
            Read_Settings(ini, PROC_GENERATOR_SECTION, Settings);
            for (int d = 0; d < DIFF_COUNT; d++) {
                Parse_Difficulty(ini, s_DiffNames[d], Difficulty[d]);
            }
        }
    }
    {
        CCINIClass ini;
        if (Load_INI(PROC_TEAMS_INI, ini)) {
            TeamCount = Load_Presets(ini, "Teams", Teams, PROC_MAX_TEAM_PRESETS, Parse_Team);
        }
    }
    {
        CCINIClass ini;
        if (Load_INI(PROC_BASES_INI, ini)) {
            BaseCount = Load_Presets(ini, "Bases", Bases, PROC_MAX_BASE_PRESETS, Parse_Base);
        }
    }
    {
        CCINIClass ini;
        if (Load_INI(PROC_OBJECTIVES_INI, ini)) {
            ObjectiveCount = Load_Presets(ini, "Objectives", Objectives, PROC_MAX_OBJECTIVE_PRESETS, Parse_Objective);
        }
    }

    {
        CCINIClass ini;
        if (Load_INI(PROC_EVENTS_INI, ini)) {
            EventCount = Load_Presets(ini, "Events", Events, PROC_MAX_EVENT_PRESETS, Parse_Event);
        }
    }

    sprintf(buf, "ProcINI: loaded %d teams, %d bases, %d objectives, %d events", TeamCount, BaseCount, ObjectiveCount, EventCount);
    GlyphX_Debug_Print(buf);

    return BaseCount > 0;
}


/*******************************************************************************
 * PRESET SELECTION
 ******************************************************************************/

static bool Side_Matches(ProcSideType preset, ProcSideType wanted)
{
    return preset == PROC_SIDE_ANY || wanted == PROC_SIDE_ANY || preset == wanted;
}

/*
**	Weighted random pick over the presets accepted by the filter.
*/
template<class T>
struct PresetWeight {
    int operator()(T const & preset) const { return max(1, preset.Weight); }
};

template<class T, class F, class W>
static T const * Weighted_Pick(T const * presets, int count, F accept, W weight)
{
    int total = 0;
    for (int i = 0; i < count; i++) {
        if (accept(presets[i])) total += weight(presets[i]);
    }
    if (total == 0) return NULL;

    // Random_Pick only has 15 significant bits; build a wider roll for big weight totals.
    int roll = (total <= 0x7FFF) ? Random_Pick(0, total - 1) : (Random_Pick(0, 0x7FFF) * 0x8000 + Random_Pick(0, 0x7FFF)) % total;
    for (int i = 0; i < count; i++) {
        if (!accept(presets[i]) || weight(presets[i]) <= 0) continue;
        roll -= weight(presets[i]);
        if (roll < 0) return &presets[i];
    }
    return NULL;
}

template<class T, class F>
static T const * Weighted_Pick(T const * presets, int count, F accept)
{
    return Weighted_Pick(presets, count, accept, PresetWeight<T>());
}

// A base preset's weight at a difficulty (0 = never at that difficulty).
struct BaseWeight {
    int Difficulty;
    int operator()(ProcBasePreset const & base) const { return (Difficulty < 0) ? max(1, base.Weight) : base.DiffWeight[Difficulty]; }
};

struct TeamFilter {
    ProcSideType Side;
    ProcTeamRoleType Role;
    ProcThreatType MinThreat;
    ProcThreatType MaxThreat;
    int Tech;
    int Naval;

    bool operator()(ProcTeamPreset const & team) const {
        if (!Side_Matches(team.Side, Side) || team.Role != Role) return false;
        if (Naval >= 0 && team.IsNaval != (Naval != 0)) return false;
        if (team.Threat < MinThreat || team.Threat > MaxThreat) return false;

        // Every member must exist at this tech level.
        for (int i = 0; i < team.MemberCount; i++) {
            int level = (int)team.Members[i].Type->Level;
            if (level < 0 || level > Tech) return false;
        }
        return true;
    }
};

ProcTeamPreset const * ProcPresetLibrary::Pick_Team(ProcSideType side, ProcTeamRoleType role, ProcThreatType minthreat, ProcThreatType maxthreat, int tech, int naval) const
{
    TeamFilter filter = {side, role, minthreat, maxthreat, tech, naval};
    ProcTeamPreset const * team = Weighted_Pick(Teams, TeamCount, filter);

    // Nothing fits the threat window at this tech level; take the strongest team that does fit.
    while (team == NULL && filter.MinThreat > PROC_THREAT_SCOUT) {
        filter.MinThreat = (ProcThreatType)(filter.MinThreat - 1);
        team = Weighted_Pick(Teams, TeamCount, filter);
    }
    return team;
}

ProcTeamPreset const * ProcPresetLibrary::Find_Team(char const * name) const
{
    for (int i = 0; i < TeamCount; i++) {
        if (stricmp(Teams[i].Name, name) == 0) return &Teams[i];
    }
    return NULL;
}

struct BaseFilter {
    ProcSideType Side;
    ProcBaseRoleType Role;
    int Tech;

    bool operator()(ProcBasePreset const & base) const {
        return Side_Matches(base.Side, Side) && base.Role == Role && base.MinTech <= Tech;
    }
};

ProcBasePreset const * ProcPresetLibrary::Pick_Base(ProcSideType side, ProcBaseRoleType role, int tech, int difficulty) const
{
    BaseFilter filter = {side, role, tech};
    BaseWeight weight = {(difficulty >= 0 && difficulty < DIFF_COUNT) ? difficulty : -1};
    ProcBasePreset const * base = Weighted_Pick(Bases, BaseCount, filter, weight);

    // Below every preset's MinTech: ignore the tech requirement; the builder skips what isn't available.
    if (base == NULL) {
        filter.Tech = 99;
        base = Weighted_Pick(Bases, BaseCount, filter, weight);
    }
    // Every preset weighted 0 at this difficulty: any will do.
    if (base == NULL) {
        weight.Difficulty = -1;
        base = Weighted_Pick(Bases, BaseCount, filter, weight);
    }
    return base;
}

ProcBasePreset const * ProcPresetLibrary::Find_Base(char const * name) const
{
    for (int i = 0; i < BaseCount; i++) {
        if (stricmp(Bases[i].Name, name) == 0) return &Bases[i];
    }
    return NULL;
}

struct ObjectiveFilter {
    ProcSideType PlayerSide;
    bool IsPrimary;
    int Tech;
    bool IsPlayerBase;
    bool CanCapture;
    ProcObjectivePreset const * const * Exclude;
    int ExcludeCount;

    bool operator()(ProcObjectivePreset const & obj) const {
        if (!Side_Matches(obj.PlayerSide, PlayerSide) || obj.MinTech > Tech) return false;
        if (obj.Slot == PROC_SLOT_EVENT || obj.Owner == PROC_OWNER_ALLY) return false;     // Only events add these
        if (IsPrimary && obj.Slot == PROC_SLOT_SECONDARY) return false;
        if (!IsPrimary && obj.Slot == PROC_SLOT_PRIMARY) return false;

        // A structure "in the player's base" needs a base; with an MCV start it would sit alone.
        if (!IsPlayerBase && obj.Owner == PROC_OWNER_PLAYER && obj.TargetCount > 0) return false;

        // Squad missions without an engineer can't capture anything.
        if (!CanCapture && obj.Kind == PROC_OBJ_CAPTURE) return false;

        // Secondary objectives never end the game by themselves, so these kinds make no sense there.
        if (!IsPrimary && (obj.Kind == PROC_OBJ_DESTROY_BASES || obj.Kind == PROC_OBJ_SURVIVE)) return false;

        for (int i = 0; i < ExcludeCount; i++) {
            if (Exclude[i] == &obj) return false;
        }
        return true;
    }
};

ProcObjectivePreset const * ProcPresetLibrary::Pick_Objective(ProcSideType playerside, bool primary, int tech, bool playerbase, bool cancapture, ProcObjectivePreset const * const * exclude, int excludecount) const
{
    ObjectiveFilter filter = {playerside, primary, tech, playerbase, cancapture, exclude, excludecount};
    return Weighted_Pick(Objectives, ObjectiveCount, filter);
}

ProcObjectivePreset const * ProcPresetLibrary::Find_Objective(char const * name) const
{
    for (int i = 0; i < ObjectiveCount; i++) {
        if (stricmp(Objectives[i].Name, name) == 0) return &Objectives[i];
    }
    return NULL;
}

struct EventFilter {
    ProcEventPreset const * First;
    ProcSideType PlayerSide;
    int Tech;
    int Situation;
    int Minutes;
    unsigned long Used;

    bool operator()(ProcEventPreset const & event) const {
        if (!Side_Matches(event.PlayerSide, PlayerSide) || event.MinTech > Tech || event.Weight <= 0) return false;
        if (event.MinMinutes > Minutes || (event.When & Situation) == 0) return false;
        return (Used & (1UL << (&event - First))) == 0;
    }
};

// Events made for the situation at hand come up more often than the ones that can happen any time.
struct EventWeight {
    int Situation;
    int operator()(ProcEventPreset const & event) const {
        return event.Weight * (((event.When & Situation & ~PROC_WHEN_ANY) != 0) ? 3 : 1);
    }
};

ProcEventPreset const * ProcPresetLibrary::Pick_Event(ProcSideType playerside, int tech, int situation, int minutes, unsigned long used) const
{
    EventFilter filter = {Events, playerside, tech, situation | PROC_WHEN_ANY, minutes, used};
    EventWeight weight = {situation};
    return Weighted_Pick(Events, EventCount, filter, weight);
}

ProcEventPreset const * ProcPresetLibrary::Find_Event(char const * name) const
{
    for (int i = 0; i < EventCount; i++) {
        if (stricmp(Events[i].Name, name) == 0) return &Events[i];
    }
    return NULL;
}
