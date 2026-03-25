//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

/*
** BOSOAI.CPP - Adaptive AI system for Red Alert.
**
** See BOSOAI.H for design overview.
*/

#include "function.h"
#include "bosoai.h"
#include "rawfile.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

extern int FastForwardMultiplier;

/*-- Forward declarations --*/
static bool Water_Near_House_Base(HouseClass * house, int radius);

void BosoAIManagerClass::Record_Game_History(const char * uid, int secs, const char * outcome,
                                              const char * faction, const char * map)
{
    int idx;
    if (GameHistoryCount < BOSO_MAX_GAME_HISTORY) {
        idx = GameHistoryCount++;
    } else {
        /*-- Ring-buffer: evict the oldest entry (index 0), shift down --*/
        memmove(&GameHistory[0], &GameHistory[1],
                sizeof(BosoGameHistoryEntry) * (BOSO_MAX_GAME_HISTORY - 1));
        idx = BOSO_MAX_GAME_HISTORY - 1;
    }
    BosoGameHistoryEntry & e = GameHistory[idx];
    memset(&e, 0, sizeof(e));
    strncpy(e.UID,     uid,     BOSO_UID_LEN - 1);
    strncpy(e.Outcome, outcome, sizeof(e.Outcome) - 1);
    strncpy(e.Faction, faction, BOSO_FACTION_LEN - 1);
    strncpy(e.Map,     map,     BOSO_MAP_LEN - 1);
    e.GameSecs = secs;
}

/*===========================================================================
** Static member definitions
===========================================================================*/

/*-- Current game UID and length --*/
char BosoAIManagerClass::CurrentGameUID[BOSO_UID_LEN]               = {0};
int  BosoAIManagerClass::CurrentGameSecs                             = 0;

/*-- Game history log --*/
BosoGameHistoryEntry BosoAIManagerClass::GameHistory[BOSO_MAX_GAME_HISTORY];
int                  BosoAIManagerClass::GameHistoryCount            = 0;

bool        BosoAIManagerClass::IsBosoActive        = false;
bool        BosoAIManagerClass::IsSpectatorMode      = false;
bool        BosoAIManagerClass::IsMultiHouseAI       = false;
bool        BosoAIManagerClass::IsAutoRestart        = false;
bool        BosoAIManagerClass::IsCostZero           = false;
bool        BosoAIManagerClass::IsTraining           = true;
HouseClass *BosoAIManagerClass::BosoHouse            = NULL;

/*-- Limit record persistence --*/
BosoLimitsRecord BosoAIManagerClass::LimitsRecords[BOSO_MAX_LIMITS_RECORDS];
int              BosoAIManagerClass::LimitsCount                = 0;
int              BosoAIManagerClass::FollowLimitsIdx[8]         = {-1,-1,-1,-1,-1,-1,-1,-1};
BosoLimitsRecord BosoAIManagerClass::HouseLimits[8];

/*-- Multi-house tracking --*/
HouseClass * BosoAIManagerClass::BosoHouses[8];
int          BosoAIManagerClass::BosoHouseCount                 = 0;

/*-- Anti-air loss tracking --*/
int  BosoAIManagerClass::AircraftLossCount                      = 0;

/*-- Naval viability cache --*/
bool BosoAIManagerClass::NavalViable[8]                         = {false};

/*-- Game context --*/
char BosoAIManagerClass::CurrentMap[BOSO_MAP_LEN]               = {0};
char BosoAIManagerClass::CurrentFaction[BOSO_FACTION_LEN]       = {0};
char BosoAIManagerClass::CurrentEnemyFaction[BOSO_FACTION_LEN]  = {0};
bool BosoAIManagerClass::GameOverFired                          = false;
long BosoAIManagerClass::GameStartFrame                         = 0;
int  BosoAIManagerClass::GameTimeBonusScore                     = 0;
int  BosoAIManagerClass::StalemateTicks                         = 0;
int  BosoAIManagerClass::HouseDeadlockTicks[8]                  = {0};
long BosoAIManagerClass::StaleCheckFrame                        = 0;
int  BosoAIManagerClass::NoCombatTicks                          = 0;
int  BosoAIManagerClass::PrevHouseStrength[8]                   = {0};

/*-- Global spectator flag --*/
bool BosoSpectatorActive = false;

/*===========================================================================
** File-scope helpers
===========================================================================*/

/*
** Build a full path to a BosoAI data file in the same directory as the DLL.
*/
static void Get_Boso_File_Path(char * path, int pathlen, const char * filename);  // forward decl

/*
** File-based logger: appends a line to BOSOAI_debug.log in the DLL directory.
** Callable from any file via BosoAIManagerClass::Log().
*/
void BosoAIManagerClass::Log(const char * msg)
{
    char path[MAX_PATH];
    Get_Boso_File_Path(path, sizeof(path), "BOSOAI_debug.log");
    FILE * fp = fopen(path, "a");
    if (!fp) return;
    fprintf(fp, "%s\n", msg);
    fflush(fp);
    fclose(fp);
}

#define Boso_Log BosoAIManagerClass::Log

static void Get_Boso_File_Path(char * path, int pathlen, const char * filename)
{
    HMODULE hmod = NULL;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&BosoSpectatorActive,
        &hmod
    );
    GetModuleFileNameA(hmod, path, pathlen);
    char * sep = strrchr(path, '\\');
    if (sep) {
        strncpy(sep + 1, filename, pathlen - (int)(sep - path) - 1);
    } else {
        strncpy(path, filename, pathlen - 1);
    }
    path[pathlen - 1] = '\0';
}

static void Get_Boso_INI_Path(char * path, int pathlen)
{
    Get_Boso_File_Path(path, pathlen, BOSO_INI_FILE);
}

/*
** Return the canonical faction name for a house.
*/
static const char * Get_Faction_Name(HouseClass const * house)
{
    if (!house) return "Unknown";
    HousesType act = house->ActLike;
    if (act >= HOUSE_MULTI1 || act == HOUSE_NONE) {
        return house->Class->IniName;
    }
    HouseClass const * act_house = HouseClass::As_Pointer(act);
    if (act_house) return act_house->Class->IniName;
    return house->Class->IniName;
}

/*===========================================================================
** BosoAIManagerClass - Private helpers
===========================================================================*/

bool BosoAIManagerClass::Is_Boso_House(HouseClass const * h)
{
    if (!h) return false;
    for (int i = 0; i < BosoHouseCount; i++) {
        if (BosoHouses[i] == h) return true;
    }
    return false;
}

void BosoAIManagerClass::Detect_Context(HouseClass * house)
{
    /*-- Map name --*/
    strncpy(CurrentMap, Scen.ScenarioName, BOSO_MAP_LEN - 1);
    CurrentMap[BOSO_MAP_LEN - 1] = '\0';
    char * sep = strrchr(CurrentMap, '\\');
    if (sep) memmove(CurrentMap, sep + 1, strlen(sep));
    char * dot = strrchr(CurrentMap, '.');
    if (dot) *dot = '\0';

    /*-- Our faction --*/
    strncpy(CurrentFaction, Get_Faction_Name(house), BOSO_FACTION_LEN - 1);
    CurrentFaction[BOSO_FACTION_LEN - 1] = '\0';

    /*-- First active enemy faction --*/
    CurrentEnemyFaction[0] = '\0';
    for (HousesType h = HOUSE_FIRST; h < HOUSE_COUNT; h++) {
        HouseClass * hptr = HouseClass::As_Pointer(h);
        if (hptr && hptr->IsActive && !hptr->IsDefeated &&
            hptr != house && !house->Is_Ally(h)) {
            strncpy(CurrentEnemyFaction, Get_Faction_Name(hptr), BOSO_FACTION_LEN - 1);
            CurrentEnemyFaction[BOSO_FACTION_LEN - 1] = '\0';
            break;
        }
    }
    if (CurrentEnemyFaction[0] == '\0') {
        strncpy(CurrentEnemyFaction, "Unknown", BOSO_FACTION_LEN - 1);
    }
}

/*===========================================================================
** Limits evolution
===========================================================================*/

/*
** Make a BosoLimitsRecord filled with the per-spec default values.
*/
static BosoLimitsRecord Make_Default_Limits(const char * faction)
{
    BosoLimitsRecord r;
    memset(&r, 0, sizeof(r));

    /* Ratio values (scaled by 256: 256 = 1.0) */
    r.DefenseRatio   = BOSO_DEF_DEFENSE_RATIO;
    r.AARatio        = BOSO_DEF_AA_RATIO;
    r.TeslaRatio     = BOSO_DEF_TESLA_RATIO;
    r.BarracksRatio  = BOSO_DEF_BARRACKS_RATIO;
    r.WarRatio       = BOSO_DEF_WAR_RATIO;
    r.HelipadRatio   = BOSO_DEF_HELIPAD_RATIO;
    r.AirstripRatio  = BOSO_DEF_AIRSTRIP_RATIO;
    r.RefineryRatio  = BOSO_DEF_REFINERY_RATIO;

    /* Limits */
    r.DefenseLimit   = BOSO_DEF_DEFENSE_LIMIT;
    r.AALimit        = BOSO_DEF_AA_LIMIT;
    r.TeslaLimit     = BOSO_DEF_TESLA_LIMIT;
    r.BarracksLimit  = BOSO_DEF_BARRACKS_LIMIT;
    r.WarLimit       = BOSO_DEF_WAR_LIMIT;
    r.HelipadLimit   = BOSO_DEF_HELIPAD_LIMIT;
    r.AirstripLimit  = BOSO_DEF_AIRSTRIP_LIMIT;
    r.RefineryLimit  = BOSO_DEF_REFINERY_LIMIT;

    /* Economy */
    r.PowerSurplus   = BOSO_DEF_POWER_SURPLUS;
    r.InfantryReserve= BOSO_DEF_INF_RESERVE;
    r.InfantryBaseMult=BOSO_DEF_INF_MULT;
    r.BaseSizeAdd    = BOSO_DEF_BASE_SIZE_ADD;

    /* Timing */
    r.AttackSecs     = BOSO_DEF_ATTACK_SECS;
    r.AttackDelaySecs= BOSO_DEF_ATTACK_DELAY;
    r.AutocreateSecs = BOSO_DEF_AUTOCREATE_SECS;
    /* Bias (256 = 1.0 = no modification) */
    r.FirepowerBias  = BOSO_DEF_BIAS;
    r.GroundspeedBias= BOSO_DEF_BIAS;
    r.ArmorBias      = BOSO_DEF_BIAS;
    r.CostBias       = BOSO_DEF_BIAS;
    r.BuildSpeedBias = BOSO_DEF_BIAS;

    strncpy(r.Faction, faction, BOSO_FACTION_LEN - 1);
    r.WinScore = 0;
    r.Active   = true;
    return r;
}

int BosoAIManagerClass::House_Slot(HouseClass const * h)
{
    for (int i = 0; i < BosoHouseCount; i++) {
        if (BosoHouses[i] == h) return i;
    }
    return -1;
}

const BosoLimitsRecord & BosoAIManagerClass::Get_House_Limits(HouseClass const * h)
{
    int slot = House_Slot(h);
    if (slot >= 0 && slot < 8) return HouseLimits[slot];
    return HouseLimits[0];
}

BosoLimitsRecord * BosoAIManagerClass::Alloc_Limits(void)
{
    if (LimitsCount < BOSO_MAX_LIMITS_RECORDS) {
        memset(&LimitsRecords[LimitsCount], 0, sizeof(BosoLimitsRecord));
        LimitsRecords[LimitsCount].Active = true;
        return &LimitsRecords[LimitsCount++];
    }
    int worst_idx   = 0;
    int worst_score = LimitsRecords[0].WinScore;
    for (int i = 1; i < BOSO_MAX_LIMITS_RECORDS; i++) {
        if (LimitsRecords[i].WinScore < worst_score) {
            worst_score = LimitsRecords[i].WinScore;
            worst_idx   = i;
        }
    }
    memset(&LimitsRecords[worst_idx], 0, sizeof(BosoLimitsRecord));
    LimitsRecords[worst_idx].Active = true;
    return &LimitsRecords[worst_idx];
}

int BosoAIManagerClass::Mutate_Limits(int parent_idx, const char * faction)
{
    BosoLimitsRecord rec = (parent_idx >= 0 && parent_idx < LimitsCount && LimitsRecords[parent_idx].Active)
        ? LimitsRecords[parent_idx]
        : Make_Default_Limits(faction);

    auto mutate = [](int v, int range, int lo, int hi) -> int {
        int delta  = (rand() % (range * 2 + 1)) - range;
        int result = v + delta;
        if (result < lo) result = lo;
        if (result > hi) result = hi;
        return result;
    };

    /* Ratio values */
    rec.DefenseRatio  = mutate(rec.DefenseRatio,  BOSO_MUT_RATIO, BOSO_MIN_RATIO, BOSO_MAX_RATIO);
    rec.AARatio       = mutate(rec.AARatio,        BOSO_MUT_RATIO, BOSO_MIN_RATIO, BOSO_MAX_RATIO);
    rec.TeslaRatio    = mutate(rec.TeslaRatio,     BOSO_MUT_RATIO, BOSO_MIN_RATIO, BOSO_MAX_RATIO);
    rec.BarracksRatio = mutate(rec.BarracksRatio,  BOSO_MUT_RATIO, BOSO_MIN_RATIO, BOSO_MAX_RATIO);
    rec.WarRatio      = mutate(rec.WarRatio,       BOSO_MUT_RATIO, BOSO_MIN_RATIO, BOSO_MAX_RATIO);
    rec.HelipadRatio  = mutate(rec.HelipadRatio,   BOSO_MUT_RATIO, BOSO_MIN_RATIO, BOSO_MAX_RATIO);
    rec.AirstripRatio = mutate(rec.AirstripRatio,  BOSO_MUT_RATIO, BOSO_MIN_RATIO, BOSO_MAX_RATIO);
    rec.RefineryRatio = mutate(rec.RefineryRatio,  BOSO_MUT_RATIO, BOSO_MIN_RATIO, BOSO_MAX_RATIO);

    /* Limits */
    rec.DefenseLimit  = mutate(rec.DefenseLimit,  BOSO_MUT_DEF_LIMIT,   BOSO_MIN_DEF_LIMIT,   BOSO_MAX_DEF_LIMIT);
    rec.AALimit       = mutate(rec.AALimit,        BOSO_MUT_AA_LIMIT,    BOSO_MIN_AA_LIMIT,    BOSO_MAX_AA_LIMIT);
    rec.TeslaLimit    = mutate(rec.TeslaLimit,     BOSO_MUT_TESLA_LIMIT, BOSO_MIN_TESLA_LIMIT, BOSO_MAX_TESLA_LIMIT);
    rec.BarracksLimit = mutate(rec.BarracksLimit,  BOSO_MUT_PROD_LIMIT,  BOSO_MIN_BARRACKS_LIMIT, BOSO_MAX_BARRACKS_LIMIT);
    rec.WarLimit      = mutate(rec.WarLimit,       BOSO_MUT_PROD_LIMIT,  BOSO_MIN_WAR_LIMIT,   BOSO_MAX_WAR_LIMIT);
    rec.HelipadLimit  = mutate(rec.HelipadLimit,   BOSO_MUT_PROD_LIMIT,  BOSO_MIN_HELIPAD_LIMIT, BOSO_MAX_HELIPAD_LIMIT);
    rec.AirstripLimit = mutate(rec.AirstripLimit,  BOSO_MUT_PROD_LIMIT,  BOSO_MIN_AIRSTRIP_LIMIT, BOSO_MAX_AIRSTRIP_LIMIT);
    rec.RefineryLimit = mutate(rec.RefineryLimit,  BOSO_MUT_PROD_LIMIT,  BOSO_MIN_REFINERY_LIMIT, BOSO_MAX_REFINERY_LIMIT);

    /* Economy */
    rec.PowerSurplus    = mutate(rec.PowerSurplus,    BOSO_MUT_POWER_SURPLUS, BOSO_MIN_POWER_SURPLUS, BOSO_MAX_POWER_SURPLUS);
    rec.InfantryReserve = mutate(rec.InfantryReserve, BOSO_MUT_INF_RESERVE,   BOSO_MIN_INF_RESERVE,   BOSO_MAX_INF_RESERVE);
    rec.InfantryBaseMult= mutate(rec.InfantryBaseMult,BOSO_MUT_INF_MULT,      BOSO_MIN_INF_MULT,      BOSO_MAX_INF_MULT);
    rec.BaseSizeAdd     = mutate(rec.BaseSizeAdd,     BOSO_MUT_BASE_SIZE_ADD, BOSO_MIN_BASE_SIZE_ADD, BOSO_MAX_BASE_SIZE_ADD);

    /* Timing */
    rec.AttackSecs      = mutate(rec.AttackSecs,     BOSO_MUT_ATTACK_SECS,    BOSO_MIN_ATTACK_SECS,    BOSO_MAX_ATTACK_SECS);
    rec.AttackDelaySecs = mutate(rec.AttackDelaySecs,BOSO_MUT_ATTACK_DELAY,   BOSO_MIN_ATTACK_DELAY,   BOSO_MAX_ATTACK_DELAY);
    rec.AutocreateSecs  = mutate(rec.AutocreateSecs, BOSO_MUT_AUTOCREATE_SECS,BOSO_MIN_AUTOCREATE_SECS,BOSO_MAX_AUTOCREATE_SECS);
    /* Bias — only firepower is evolved; the rest remain fixed at 1.0 */
    rec.FirepowerBias   = mutate(rec.FirepowerBias,  BOSO_MUT_FIREPOWER,   BOSO_MIN_FIREPOWER,   BOSO_MAX_FIREPOWER);
    rec.GroundspeedBias = BOSO_DEF_BIAS;
    rec.ArmorBias       = BOSO_DEF_BIAS;
    rec.CostBias        = BOSO_DEF_BIAS;
    rec.BuildSpeedBias  = BOSO_DEF_BIAS;

    rec.WinScore    = 0;
    rec.GamesPlayed = 0;
    strncpy(rec.Faction, faction, BOSO_FACTION_LEN - 1);

    /*-- Dedup: reuse any existing identical record for this faction --*/
    for (int i = 0; i < LimitsCount; i++) {
        BosoLimitsRecord & e = LimitsRecords[i];
        if (!e.Active || stricmp(e.Faction, faction) != 0) continue;
        if (e.DefenseRatio   == rec.DefenseRatio   && e.AARatio      == rec.AARatio      &&
            e.TeslaRatio     == rec.TeslaRatio     && e.BarracksRatio== rec.BarracksRatio &&
            e.WarRatio       == rec.WarRatio        && e.HelipadRatio == rec.HelipadRatio  &&
            e.AirstripRatio  == rec.AirstripRatio  && e.RefineryRatio == rec.RefineryRatio &&
            e.DefenseLimit   == rec.DefenseLimit   && e.AALimit       == rec.AALimit       &&
            e.TeslaLimit     == rec.TeslaLimit     && e.BarracksLimit == rec.BarracksLimit &&
            e.WarLimit       == rec.WarLimit        && e.HelipadLimit  == rec.HelipadLimit  &&
            e.AirstripLimit  == rec.AirstripLimit  && e.RefineryLimit  == rec.RefineryLimit &&
            e.PowerSurplus   == rec.PowerSurplus   && e.InfantryReserve==rec.InfantryReserve &&
            e.InfantryBaseMult==rec.InfantryBaseMult&& e.BaseSizeAdd  == rec.BaseSizeAdd   &&
            e.AttackSecs     == rec.AttackSecs     && e.AttackDelaySecs==rec.AttackDelaySecs&&
            e.AutocreateSecs == rec.AutocreateSecs &&
            e.FirepowerBias  == rec.FirepowerBias) {
            return i;
        }
    }

    BosoLimitsRecord * slot = Alloc_Limits();
    if (!slot) return (parent_idx >= 0) ? parent_idx : -1;
    *slot = rec;
    return (int)(slot - LimitsRecords);
}

int BosoAIManagerClass::Select_Limits(const char * faction, BosoLimitsRecord & out)
{
    int   best_idx   = -1;
    float best_score = -1e30f;
    for (int i = 0; i < LimitsCount; i++) {
        BosoLimitsRecord & r = LimitsRecords[i];
        if (!r.Active || stricmp(r.Faction, faction) != 0) continue;
        float norm = (float)r.WinScore / (float)(r.GamesPlayed > 0 ? r.GamesPlayed : 1);
        if (norm > best_score) { best_score = norm; best_idx = i; }
    }

    int chosen;
    if (best_idx < 0) {
        chosen = Mutate_Limits(-1, faction);
    } else if ((rand() % 100) >= BOSO_EXPLOIT_CHANCE) {
        chosen = Mutate_Limits(best_idx, faction);
        if (chosen < 0) chosen = best_idx;
    } else {
        chosen = best_idx;
    }

    if (chosen >= 0 && chosen < LimitsCount) {
        out = LimitsRecords[chosen];
    } else {
        out = Make_Default_Limits(faction);
    }
    return chosen;
}

void BosoAIManagerClass::Score_Session_Limits(int slot, bool won)
{
    if (slot < 0 || slot >= 8) return;
    int idx = FollowLimitsIdx[slot];
    if (idx >= 0 && idx < LimitsCount) {
        BosoLimitsRecord & r = LimitsRecords[idx];
        if (won) r.WinScore += BOSO_LIMITS_WIN + GameTimeBonusScore;
        else     r.WinScore -= BOSO_LIMITS_LOSS;
        r.GamesPlayed++;
        strncpy(r.LastGameUID, CurrentGameUID, BOSO_UID_LEN - 1);
        r.LastGameSecs = CurrentGameSecs;
    }
}

void BosoAIManagerClass::Apply_Limits_To_House(HouseClass * house, int slot)
{
    if (!house || slot < 0 || slot >= 8) return;
    const BosoLimitsRecord & lim = HouseLimits[slot];

    /* Attack timing — initial wave delay */
    house->Attack = lim.AttackDelaySecs * TICKS_PER_SECOND;

    /* Per-house combat bias (fixed-point via fixed(numerator, denominator)) */
    house->FirepowerBias   = fixed(lim.FirepowerBias,   256);
    house->GroundspeedBias = fixed(lim.GroundspeedBias, 256);
    house->ArmorBias       = fixed(lim.ArmorBias,       256);
    house->BuildSpeedBias  = fixed(lim.BuildSpeedBias,  256);

    /* CostBias: override only if not already forced to zero */
    if (IsCostZero) {
        house->CostBias         = fixed(0);
        house->Credits          = 99999;
        house->Control.InitialCredits = 99999;
    } else {
        house->CostBias = fixed(lim.CostBias, 256);
    }

    house->Control.TechLevel = 10;
}

/*===========================================================================
** Public interface - Lifecycle
===========================================================================*/

void BosoAIManagerClass::Init(void)
{
    if (IsBosoActive && !GameOverFired) {
        GlyphX_Debug_Print("BosoAI: mid-game quit, saving data before reset");
        Save_Data();
    }

    IsBosoActive        = false;
    IsSpectatorMode     = false;
    IsMultiHouseAI      = false;
    IsAutoRestart       = false;
    IsCostZero          = false;
    IsTraining          = true;
    BosoHouse           = NULL;
    BosoSpectatorActive = false;
    GameStartFrame      = 0;
    GameTimeBonusScore  = 0;
    CurrentGameSecs     = 0;
    CurrentGameUID[0]   = '\0';
    GameOverFired       = false;
    StalemateTicks      = 0;
    memset(HouseDeadlockTicks, 0, sizeof(HouseDeadlockTicks));
    StaleCheckFrame     = 0;
    NoCombatTicks       = 0;
    memset(PrevHouseStrength, 0, sizeof(PrevHouseStrength));
    AircraftLossCount   = 0;
    memset(NavalViable, 0, sizeof(NavalViable));
    BosoHouseCount  = 0;
    CurrentMap[0]   = '\0';
    CurrentFaction[0] = '\0';
    CurrentEnemyFaction[0] = '\0';

    memset(BosoHouses,    0, sizeof(BosoHouses));
    memset(LimitsRecords, 0, sizeof(LimitsRecords));
    memset(HouseLimits,   0, sizeof(HouseLimits));

    LimitsCount = 0;
    for (int i = 0; i < 8; i++) FollowLimitsIdx[i] = -1;
}

void BosoAIManagerClass::Load_Config(void)
{
    char path[MAX_PATH];
    Get_Boso_File_Path(path, sizeof(path), BOSO_CONFIG_FILE);

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        GlyphX_Debug_Print("BosoAI: no BOSOAI_CONFIG.INI found, skipping config load");
        return;
    }

    char bool_buf[8];
    GetPrivateProfileStringA("Config", "Enabled", "no", bool_buf, sizeof(bool_buf), path);
    if (_stricmp(bool_buf, "yes") == 0 || _stricmp(bool_buf, "true") == 0 || atoi(bool_buf) != 0) {
        IsBosoActive = true;

        GetPrivateProfileStringA("Config", "SpectatorMode", "yes", bool_buf, sizeof(bool_buf), path);
        IsSpectatorMode = (_stricmp(bool_buf, "no") != 0 && _stricmp(bool_buf, "false") != 0);

        GetPrivateProfileStringA("Config", "MultiHouseAI", "no", bool_buf, sizeof(bool_buf), path);
        IsMultiHouseAI = (_stricmp(bool_buf, "yes") == 0 || _stricmp(bool_buf, "true") == 0 || atoi(bool_buf) != 0);

        GetPrivateProfileStringA("Config", "AutoRestart", "no", bool_buf, sizeof(bool_buf), path);
        IsAutoRestart = (_stricmp(bool_buf, "yes") == 0 || _stricmp(bool_buf, "true") == 0 || atoi(bool_buf) != 0);

        GetPrivateProfileStringA("Config", "CostZero", "no", bool_buf, sizeof(bool_buf), path);
        IsCostZero = (_stricmp(bool_buf, "yes") == 0 || _stricmp(bool_buf, "true") == 0 || atoi(bool_buf) != 0);

        GetPrivateProfileStringA("Config", "Training", "yes", bool_buf, sizeof(bool_buf), path);
        IsTraining = (_stricmp(bool_buf, "yes") == 0 || _stricmp(bool_buf, "true") == 0 || atoi(bool_buf) != 0);

        GlyphX_Debug_Print("BosoAI: enabled via BOSOAI_CONFIG.INI");
    }

    int ff = (int)GetPrivateProfileIntA("Config", "FastForward", 1, path);
    if (ff > 1) {
        FastForwardMultiplier = ff;
    }
}

void BosoAIManagerClass::On_Scenario_Load(void)
{
    if (!IsBosoActive || !PlayerPtr) return;

    /*-- Append a separator to the log so we keep history across restarts. --*/
    {
        char logpath[MAX_PATH];
        Get_Boso_File_Path(logpath, sizeof(logpath), "BOSOAI_debug.log");
        FILE * lf = fopen(logpath, "a");
        if (lf) { fprintf(lf, "\n=== BosoAI game start ===\n"); fclose(lf); }
    }

    BosoHouse = PlayerPtr;
    BosoHouseCount = 0;
    BosoHouses[BosoHouseCount++] = PlayerPtr;

    {
        char _sl_dbg[128];
        sprintf(_sl_dbg, "BosoAI: On_Scenario_Load Active=%d AutoRestart=%d MultiHouse=%d",
            (int)IsBosoActive, (int)IsAutoRestart, (int)IsMultiHouseAI);
        GlyphX_Debug_Print(_sl_dbg);
        Boso_Log(_sl_dbg);
    }
    {
        char _mp_dbg[96];
        sprintf(_mp_dbg, "On_Scenario_Load: Session.MaxPlayers=%d PlayerPtr=%s",
            Session.MaxPlayers, PlayerPtr ? PlayerPtr->IniName : "NULL");
        Boso_Log(_mp_dbg);
    }

    GameOverFired          = false;
    GameStartFrame         = Frame;
    GameTimeBonusScore     = 0;
    CurrentGameSecs        = 0;
    AircraftLossCount      = 0;

    /*-- Generate a unique game UID: lower 32 bits of time XOR'd with tick counter --*/
    {
        unsigned int t  = (unsigned int)time(NULL);
        unsigned int tc = (unsigned int)GetTickCount();
        sprintf(CurrentGameUID, "%08X%04X", t, (tc >> 4) & 0xFFFF);
    }

    for (int i = 0; i < 8; i++) FollowLimitsIdx[i] = -1;
    memset(HouseLimits, 0, sizeof(HouseLimits));

    Load_Data();
    Detect_Context(PlayerPtr);

    /*-- Cache naval viability for slot 0 (primary house): tech + water within 20 cells of base. --*/
    {
        const BuildingTypeClass & sy = BuildingTypeClass::As_Reference(STRUCT_SHIP_YARD);
        const BuildingTypeClass & sp = BuildingTypeClass::As_Reference(STRUCT_SUB_PEN);
        bool can_build = PlayerPtr->Can_Build(&sy, PlayerPtr->ActLike) ||
                         PlayerPtr->Can_Build(&sp, PlayerPtr->ActLike);
        NavalViable[0] = can_build && Water_Near_House_Base(PlayerPtr, 20);
        GlyphX_Debug_Print(NavalViable[0] ? "BosoAI[0]: naval viable" : "BosoAI[0]: naval not viable");
    }

    /*-- Select limits for the primary house (slot 0) --*/
    FollowLimitsIdx[0] = Select_Limits(CurrentFaction, HouseLimits[0]);
    {
        const BosoLimitsRecord & L = HouseLimits[0];
        char _dbg[256];
        sprintf(_dbg, "BosoAI Limits[0]: DefR=%d AAR=%d TesR=%d BarR=%d WarR=%d AtkS=%d BsAdd=%d FPB=%d CostB=%d",
            L.DefenseRatio, L.AARatio, L.TeslaRatio, L.BarracksRatio, L.WarRatio,
            L.AttackSecs, L.BaseSizeAdd, L.FirepowerBias, L.CostBias);
        GlyphX_Debug_Print(_dbg);
    }

    /*-- Take over the primary house --*/
    if (Session.Type == GAME_NORMAL) {
        PlayerPtr->IsHuman          = false;
        PlayerPtr->IsPlayerControl  = false;
        PlayerPtr->WasHuman         = false;
        PlayerPtr->IQ               = Rule.MaxIQ;
        PlayerPtr->IsBaseBuilding   = true;
        PlayerPtr->IsStarted        = true;
        PlayerPtr->IsAlerted        = true;
        PlayerPtr->Assign_Handicap(DIFF_HARD);
        PlayerPtr->Difficulty       = DIFF_NORMAL;
    } else {
        PlayerPtr->IsHuman          = false;
        PlayerPtr->WasHuman         = true;
        PlayerPtr->IsPlayerControl  = false;
        PlayerPtr->IQ               = Rule.MaxIQ;
        PlayerPtr->IsBaseBuilding   = true;
        PlayerPtr->IsStarted        = true;
        PlayerPtr->IsAlerted        = true;
        PlayerPtr->Assign_Handicap(DIFF_HARD);
        PlayerPtr->Difficulty       = DIFF_NORMAL;
    }

    Apply_Limits_To_House(PlayerPtr, 0);

    /*-- Spectator: reveal map --*/
    if (IsSpectatorMode) {
        PlayerPtr->IsVisionary = true;
        for (CELL cell = 0; cell < MAP_CELL_TOTAL; cell++) {
            Map.Map_Cell(cell, PlayerPtr);
        }
        BosoSpectatorActive = true;
        GlyphX_Debug_Print("BosoAI: spectator mode enabled");
    }

    /*-- Extend to all AI houses in skirmish when MultiHouseAI is enabled --*/
    if (IsMultiHouseAI && Session.Type == GAME_GLYPHX_MULTIPLAYER) {
        for (HousesType h = HOUSE_MULTI1; h < HOUSE_COUNT; h++) {
            HouseClass * hptr = HouseClass::As_Pointer(h);
            char _hdbg[128];
            if (!hptr) { sprintf(_hdbg, "BosoAI Reg[%d]: null ptr", (int)h); GlyphX_Debug_Print(_hdbg); continue; }
            sprintf(_hdbg, "BosoAI Reg[%d]: IsDefeated=%d IsHuman=%d IsPlayerPtr=%d ActLike=%d",
                (int)h, (int)hptr->IsDefeated, (int)hptr->IsHuman, (int)(hptr==PlayerPtr), (int)hptr->ActLike);
            GlyphX_Debug_Print(_hdbg);
            if (hptr->IsDefeated) continue;
            if (hptr == PlayerPtr || hptr->IsHuman) continue;
            if (BosoHouseCount >= 8) break;
            if (hptr->ActLike >= HOUSE_MULTI1 || hptr->ActLike == HOUSE_NONE) continue;

            int slot = BosoHouseCount;
            BosoHouses[BosoHouseCount++] = hptr;
            const char * hfaction = Get_Faction_Name(hptr);
            FollowLimitsIdx[slot] = Select_Limits(hfaction, HouseLimits[slot]);
            Apply_Limits_To_House(hptr, slot);
            {
                const BuildingTypeClass & sy = BuildingTypeClass::As_Reference(STRUCT_SHIP_YARD);
                const BuildingTypeClass & sp = BuildingTypeClass::As_Reference(STRUCT_SUB_PEN);
                bool can_build = hptr->Can_Build(&sy, hptr->ActLike) ||
                                 hptr->Can_Build(&sp, hptr->ActLike);
                NavalViable[slot] = can_build && Water_Near_House_Base(hptr, 20);
            }
            GlyphX_Debug_Print("BosoAI: extended to additional AI house");
        }
        char _dbg[64];
        sprintf(_dbg, "BosoAI: registered %d houses total", BosoHouseCount);
        GlyphX_Debug_Print(_dbg);
    }

    GlyphX_Debug_Print("BosoAI: house takeover complete");
}

void BosoAIManagerClass::On_Game_Over(bool player_won)
{
    {
        char _oge_dbg[96];
        sprintf(_oge_dbg, "On_Game_Over called: IsBosoActive=%d GameOverFired=%d player_won=%d",
            (int)IsBosoActive, (int)GameOverFired, (int)player_won);
        Boso_Log(_oge_dbg);
        GlyphX_Debug_Print(_oge_dbg);
    }
    if (!IsBosoActive || GameOverFired) return;
    GameOverFired = true;

    Boso_Log(player_won ? "On_Game_Over: game WON - proceeding" : "On_Game_Over: game LOST - proceeding");
    GlyphX_Debug_Print(player_won ? "BosoAI: game won" : "BosoAI: game lost");

    /*-- Compute elapsed time and time bonus --*/
    {
        long elapsed_ticks = Frame - GameStartFrame;
        CurrentGameSecs    = (int)(elapsed_ticks / TICKS_PER_SECOND);

        if (CurrentGameSecs <= BOSO_TIME_BONUS_MIN_SECS) {
            GameTimeBonusScore = BOSO_TIME_BONUS_MAX;
        } else if (CurrentGameSecs >= BOSO_TIME_BONUS_CAP_SECS) {
            GameTimeBonusScore = 0;
        } else {
            float fraction = (float)(BOSO_TIME_BONUS_CAP_SECS - CurrentGameSecs)
                           / (float)(BOSO_TIME_BONUS_CAP_SECS - BOSO_TIME_BONUS_MIN_SECS);
            GameTimeBonusScore = (int)(BOSO_TIME_BONUS_MAX * fraction);
        }
        char _tdbg[96];
        sprintf(_tdbg, "BosoAI: elapsed %ds, time bonus = %d", CurrentGameSecs, GameTimeBonusScore);
        GlyphX_Debug_Print(_tdbg);
    }

    /*-- In FFA multi-house mode, the game history outcome and faction should
    **   reflect the actual winner, not just the primary house. --*/
    bool hist_won = player_won;
    const char * hist_faction = CurrentFaction;
    if (IsMultiHouseAI) {
        for (int i = 0; i < BosoHouseCount; i++) {
            if (BosoHouses[i] && !BosoHouses[i]->IsDefeated) {
                hist_won     = true;
                hist_faction = Get_Faction_Name(BosoHouses[i]);
                break;
            }
        }
    }
    Record_Game_History(CurrentGameUID, CurrentGameSecs,
                        hist_won ? "W" : "L",
                        hist_faction, CurrentMap);

    /*-- Score each house's limits individually based on its own survival.
    **   Multiple houses may share the same limits record (via Mutate_Limits dedup).
    **   Track which LimitsRecords indices have been scored to avoid double-counting. --*/
    {
        bool record_scored[BOSO_MAX_LIMITS_RECORDS] = {false};
        bool record_won[BOSO_MAX_LIMITS_RECORDS]    = {false};
        for (int i = 0; i < BosoHouseCount; i++) {
            int idx = FollowLimitsIdx[i];
            if (idx < 0 || idx >= LimitsCount) continue;
            if (BosoHouses[i] && !BosoHouses[i]->IsDefeated) record_won[idx] = true;
            record_scored[idx] = true;
        }
        for (int idx = 0; idx < LimitsCount; idx++) {
            if (!record_scored[idx]) continue;
            for (int i = 0; i < BosoHouseCount; i++) {
                if (FollowLimitsIdx[i] == idx) {
                    Score_Session_Limits(i, record_won[idx]);
                    break;
                }
            }
        }
    }

    Boso_Log("On_Game_Over: calling Save_Data");
    Save_Data();
    Boso_Log("On_Game_Over: Save_Data done");

    if (IsAutoRestart) {
        Boso_Log("On_Game_Over: setting PlayerRestarts=true");
        GlyphX_Debug_Print("BosoAI: auto-restart triggered");
        PlayerRestarts = true;
    } else {
        Boso_Log("On_Game_Over: IsAutoRestart=false, NOT setting PlayerRestarts");
    }
    Boso_Log("On_Game_Over: returning normally");
}

/*===========================================================================
** Stalemate detection
===========================================================================*/

void BosoAIManagerClass::Check_Stalemate(void)
{
    if (!IsBosoActive || GameOverFired) return;
    if (BosoHouseCount == 0) return;

    if (Frame - StaleCheckFrame < TICKS_PER_SECOND) return;
    StaleCheckFrame = Frame;

    /*-- No-combat stalemate: if no house takes any unit/building losses for
    **   BOSO_NO_COMBAT_TIMEOUT_SECS seconds (after an equal grace period),
    **   the match is geographically stuck (e.g. one house enclosed by terrain).
    **   Grace period = same constant, so minimum game time before trigger = 2x. --*/
    {
        int ff = (FastForwardMultiplier > 1) ? FastForwardMultiplier : 1;
        bool in_grace  = (Frame - GameStartFrame < (long)(BOSO_NO_COMBAT_TIMEOUT_SECS * TICKS_PER_SECOND) * ff);
        bool any_losses = false;
        for (int i = 0; i < BosoHouseCount; i++) {
            HouseClass * h = BosoHouses[i];
            if (!h || h->IsDefeated) { PrevHouseStrength[i] = 0; continue; }
            int cur = (int)(h->CurUnits + h->CurBuildings + h->CurInfantry + h->CurVessels);
            if (PrevHouseStrength[i] > 0 && cur < PrevHouseStrength[i]) any_losses = true;
            PrevHouseStrength[i] = cur;
        }
        if (in_grace || any_losses) {
            NoCombatTicks = 0;
        } else {
            NoCombatTicks++;
            if (NoCombatTicks >= BOSO_NO_COMBAT_TIMEOUT_SECS * ff) {
                /*-- Award the win to the strongest alive house so the better
                **   strategy still gets credit even if it was unreachable. --*/
                int winner_slot = -1;
                int best_strength = -1;
                for (int i = 0; i < BosoHouseCount; i++) {
                    HouseClass * h = BosoHouses[i];
                    if (!h || h->IsDefeated) continue;
                    int s = (int)(h->CurUnits + h->CurBuildings + h->CurInfantry + h->CurVessels);
                    if (s > best_strength) { best_strength = s; winner_slot = i; }
                }
                char dbg[192];
                sprintf(dbg, "BosoAI: no combat for %ds - geographic stalemate; winner_slot=%d strength=%d",
                    BOSO_NO_COMBAT_TIMEOUT_SECS, winner_slot, best_strength);
                GlyphX_Debug_Print(dbg);
                Boso_Log(dbg);
                GameOverFired      = true;
                GameTimeBonusScore = 0;
                CurrentGameSecs = (int)((Frame - GameStartFrame) / TICKS_PER_SECOND);
                const char * win_faction = (winner_slot >= 0) ? Get_Faction_Name(BosoHouses[winner_slot]) : CurrentFaction;
                Record_Game_History(CurrentGameUID, CurrentGameSecs, "W", win_faction, CurrentMap);
                for (int i = 0; i < BosoHouseCount; i++) {
                    Score_Session_Limits(i, i == winner_slot);
                }
                Save_Data();
                NoCombatTicks = 0;
                if (IsAutoRestart) {
                    PlayerRestarts = true;
                }
                return;
            }
        }
    }

    /*-- Per-house deadlock: a house with no way to earn or spend resources
    **   cannot recover. Defeat it individually so the match can continue.
    **   Deadlocked = broke AND (no harvester OR no refinery). --*/
    bool all_deadlocked = true;
    for (int i = 0; i < BosoHouseCount; i++) {
        HouseClass * h = BosoHouses[i];
        if (!h || h->IsDefeated) {
            HouseDeadlockTicks[i] = 0;
            continue;
        }
        bool no_harvester = (h->UQuantity[UNIT_HARVESTER] == 0);
        bool no_refinery  = (h->BQuantity[STRUCT_REFINERY] == 0);
        bool broke        = (h->Available_Money() <= BOSO_STALEMATE_MONEY_CAP);
        bool deadlocked   = broke && (no_harvester || no_refinery);

        if (deadlocked) {
            HouseDeadlockTicks[i]++;
            if (HouseDeadlockTicks[i] >= BOSO_STALEMATE_SECS) {
                char dbg[128];
                sprintf(dbg, "BosoAI: house[%s] individually deadlocked %ds - defeating",
                    h->IniName, BOSO_STALEMATE_SECS);
                GlyphX_Debug_Print(dbg);
                Boso_Log(dbg);
                HouseDeadlockTicks[i] = 0;
                h->MPlayer_Defeated();
                return;
            }
        } else {
            HouseDeadlockTicks[i] = 0;
            all_deadlocked = false;
        }
    }

    /*-- Global stalemate fallback: all surviving houses deadlocked simultaneously. --*/
    if (!all_deadlocked) {
        StalemateTicks = 0;
        return;
    }

    StalemateTicks++;

    {
        char dbg[96];
        sprintf(dbg, "BosoAI: stalemate tick %d/%d", StalemateTicks, BOSO_STALEMATE_SECS);
        GlyphX_Debug_Print(dbg);
    }

    if (StalemateTicks < BOSO_STALEMATE_SECS) return;

    GlyphX_Debug_Print("BosoAI: stalemate confirmed - scoring as loss and restarting");
    GameOverFired      = true;
    GameTimeBonusScore = 0;

    CurrentGameSecs = (int)((Frame - GameStartFrame) / TICKS_PER_SECOND);
    Record_Game_History(CurrentGameUID, CurrentGameSecs, "S", CurrentFaction, CurrentMap);

    for (int i = 0; i < BosoHouseCount; i++) {
        Score_Session_Limits(i, false);
    }

    Save_Data();
    StalemateTicks = 0;

    if (IsAutoRestart) {
        PlayerRestarts = true;
    }
}

/*
** Try_Register_House - lazily registers a secondary AI house into the BosoAI
** tracking arrays. Called from HOUSE.CPP AI_Building on the first AI tick of
** each non-primary house when IsMultiHouseAI is true.
*/
void BosoAIManagerClass::Try_Register_House(HouseClass * house)
{
    if (!IsBosoActive || !IsMultiHouseAI || !house) {
        GlyphX_Debug_Print("BosoAI TryReg: early-out inactive/config");
        return;
    }
    if (house->IsDefeated) {
        GlyphX_Debug_Print("BosoAI TryReg: early-out IsDefeated");
        return;
    }
    if (BosoHouseCount >= 8) {
        GlyphX_Debug_Print("BosoAI TryReg: early-out BosoHouseCount>=8");
        return;
    }
    if (house == BosoHouse) {
        GlyphX_Debug_Print("BosoAI TryReg: early-out is BosoHouse");
        return;
    }

    for (int i = 0; i < BosoHouseCount; i++) {
        if (BosoHouses[i] == house) {
            return;  // already registered — silent
        }
    }

    int slot = BosoHouseCount;
    BosoHouses[BosoHouseCount++] = house;
    const char * hfaction = Get_Faction_Name(house);
    FollowLimitsIdx[slot] = Select_Limits(hfaction, HouseLimits[slot]);
    Apply_Limits_To_House(house, slot);

    {
        const BuildingTypeClass & sy = BuildingTypeClass::As_Reference(STRUCT_SHIP_YARD);
        const BuildingTypeClass & sp = BuildingTypeClass::As_Reference(STRUCT_SUB_PEN);
        bool can_build = house->Can_Build(&sy, house->ActLike) ||
                         house->Can_Build(&sp, house->ActLike);
        NavalViable[slot] = can_build && Water_Near_House_Base(house, 20);
    }

    char _dbg[128];
    sprintf(_dbg, "BosoAI: late-registered secondary AI house (slot %d) IsHuman=%d",
            BosoHouseCount - 1, (int)house->IsHuman);
    GlyphX_Debug_Print(_dbg);
}

/*===========================================================================
** PlayerPtr redirect for multi-house FFA
===========================================================================*/

HouseClass * BosoAIManagerClass::Get_Next_Active_House(void)
{
    if (!IsBosoActive || !IsMultiHouseAI) return NULL;
    if (!BosoHouse || !BosoHouse->IsDefeated) return NULL;

    for (int i = 1; i < BosoHouseCount; i++) {
        HouseClass * h = BosoHouses[i];
        if (h && !h->IsDefeated) return h;
    }
    return NULL;
}

bool BosoAIManagerClass::Any_Boso_House_Alive(void)
{
    for (int i = 0; i < BosoHouseCount; i++) {
        if (BosoHouses[i] && !BosoHouses[i]->IsDefeated) return true;
    }
    return false;
}

/*===========================================================================
** Anti-air tracking
===========================================================================*/

void BosoAIManagerClass::On_Unit_Killed_By_Aircraft(void)
{
    AircraftLossCount++;
    if (AircraftLossCount == BOSO_AA_LOSS_THRESHOLD) {
        GlyphX_Debug_Print("BosoAI: aircraft losses exceeded threshold");
    }
}

/*===========================================================================
** Helpers
===========================================================================*/

static bool Water_Near_House_Base(HouseClass * house, int radius)
{
    if (!house) return false;

    int sum_x = 0, sum_y = 0, bcount = 0;
    for (int i = 0; i < Buildings.Count(); i++) {
        BuildingClass * b = Buildings.Ptr(i);
        if (!b || (HouseClass*)b->House != house || b->IsInLimbo) continue;
        CELL c = Coord_Cell(b->Coord);
        sum_x += (int)(c % MAP_CELL_W);
        sum_y += (int)(c / MAP_CELL_W);
        bcount++;
    }
    if (bcount == 0) return false;

    int cx = sum_x / bcount;
    int cy = sum_y / bcount;

    static const int dx4[4] = { 0,  0, -1, 1 };
    static const int dy4[4] = {-1,  1,  0, 0 };

    for (int dy = -radius; dy <= radius; dy++) {
        int ny = cy + dy;
        if (ny < 1 || ny >= MAP_CELL_H - 1) continue;
        for (int dx = -radius; dx <= radius; dx++) {
            int nx = cx + dx;
            if (nx < 1 || nx >= MAP_CELL_W - 1) continue;
            CELL c = (CELL)(ny * MAP_CELL_W + nx);
            if (!Map.In_Radar(c)) continue;
            if (Map[c].Land_Type() == LAND_WATER) continue;

            for (int a = 0; a < 4; a++) {
                CELL adj = (CELL)((ny + dy4[a]) * MAP_CELL_W + (nx + dx4[a]));
                if (Map.In_Radar(adj) && Map[adj].Land_Type() == LAND_WATER) return true;
            }
        }
    }
    return false;
}

bool BosoAIManagerClass::Is_Naval_Viable(HouseClass * house)
{
    if (!house) return false;
    int slot = House_Slot(house);
    if (slot < 0 || slot >= 8) return false;
    return NavalViable[slot];
}

bool BosoAIManagerClass::Is_Spectator_Input_Blocked(void)
{
    return BosoSpectatorActive;
}

/*===========================================================================
** INI Persistence
===========================================================================*/

void BosoAIManagerClass::Load_Data(void)
{
    LimitsCount = 0;
    memset(LimitsRecords, 0, sizeof(LimitsRecords));

    char path[MAX_PATH];
    Get_Boso_INI_Path(path, sizeof(path));

    RawFileClass file(path);
    if (!file.Is_Available()) {
        GlyphX_Debug_Print("BosoAI: no INI file found, starting fresh");
        return;
    }

    CCINIClass ini;
    ini.Load(file, false);

    /*-- Limits records --*/
    int limcount = ini.Get_Int("BosoLimits", "Count", 0);
    for (int i = 0; i < limcount && LimitsCount < BOSO_MAX_LIMITS_RECORDS; i++) {
        char key[64];

        sprintf(key, "Limits_%d_Faction", i);
        char faction[BOSO_FACTION_LEN] = {0};
        ini.Get_String("BosoLimits", key, "", faction, sizeof(faction));
        if (faction[0] == '\0') continue;

        BosoLimitsRecord & r = LimitsRecords[LimitsCount];
        memset(&r, 0, sizeof(r));
        strncpy(r.Faction, faction, BOSO_FACTION_LEN - 1);

#define LGET(field, key_suffix, def) \
        sprintf(key, "Limits_%d_" key_suffix, i); \
        r.field = ini.Get_Int("BosoLimits", key, def)

        LGET(WinScore,       "Score",          0);
        LGET(GamesPlayed,    "Games",          0);

        /* Ratio values */
        LGET(DefenseRatio,   "DefenseRatio",   BOSO_DEF_DEFENSE_RATIO);
        LGET(AARatio,        "AARatio",        BOSO_DEF_AA_RATIO);
        LGET(TeslaRatio,     "TeslaRatio",     BOSO_DEF_TESLA_RATIO);
        LGET(BarracksRatio,  "BarracksRatio",  BOSO_DEF_BARRACKS_RATIO);
        LGET(WarRatio,       "WarRatio",       BOSO_DEF_WAR_RATIO);
        LGET(HelipadRatio,   "HelipadRatio",   BOSO_DEF_HELIPAD_RATIO);
        LGET(AirstripRatio,  "AirstripRatio",  BOSO_DEF_AIRSTRIP_RATIO);
        LGET(RefineryRatio,  "RefineryRatio",  BOSO_DEF_REFINERY_RATIO);

        /* Limits */
        LGET(DefenseLimit,   "DefenseLimit",   BOSO_DEF_DEFENSE_LIMIT);
        LGET(AALimit,        "AALimit",        BOSO_DEF_AA_LIMIT);
        LGET(TeslaLimit,     "TeslaLimit",     BOSO_DEF_TESLA_LIMIT);
        LGET(BarracksLimit,  "BarracksLimit",  BOSO_DEF_BARRACKS_LIMIT);
        LGET(WarLimit,       "WarLimit",       BOSO_DEF_WAR_LIMIT);
        LGET(HelipadLimit,   "HelipadLimit",   BOSO_DEF_HELIPAD_LIMIT);
        LGET(AirstripLimit,  "AirstripLimit",  BOSO_DEF_AIRSTRIP_LIMIT);
        LGET(RefineryLimit,  "RefineryLimit",  BOSO_DEF_REFINERY_LIMIT);

        /* Economy */
        LGET(PowerSurplus,    "PowerSurplus",   BOSO_DEF_POWER_SURPLUS);
        LGET(InfantryReserve, "InfantryReserve",BOSO_DEF_INF_RESERVE);
        LGET(InfantryBaseMult,"InfantryBaseMult",BOSO_DEF_INF_MULT);
        LGET(BaseSizeAdd,    "BaseSizeAdd",    BOSO_DEF_BASE_SIZE_ADD);

        /* Timing */
        LGET(AttackSecs,     "AttackSecs",     BOSO_DEF_ATTACK_SECS);
        LGET(AttackDelaySecs,"AttackDelaySecs",BOSO_DEF_ATTACK_DELAY);
        LGET(AutocreateSecs, "AutocreateSecs", BOSO_DEF_AUTOCREATE_SECS);
        /* Bias */
        LGET(FirepowerBias,  "FirepowerBias",  BOSO_DEF_BIAS);
        LGET(GroundspeedBias,"GroundspeedBias",BOSO_DEF_BIAS);
        LGET(ArmorBias,      "ArmorBias",      BOSO_DEF_BIAS);
        LGET(CostBias,       "CostBias",       BOSO_DEF_BIAS);
        LGET(BuildSpeedBias, "BuildSpeedBias", BOSO_DEF_BIAS);
#undef LGET

        {
            char luid[BOSO_UID_LEN] = {0};
            char lkey[64];
            sprintf(lkey, "Limits_%d_LastUID", i);
            ini.Get_String("BosoLimits", lkey, "", luid, sizeof(luid));
            strncpy(r.LastGameUID, luid, BOSO_UID_LEN - 1);

            sprintf(lkey, "Limits_%d_LastSecs", i);
            r.LastGameSecs = ini.Get_Int("BosoLimits", lkey, 0);
        }

        r.Active = true;
        LimitsCount++;
    }

    /*-- Game history --*/
    GameHistoryCount = 0;
    memset(GameHistory, 0, sizeof(GameHistory));
    int ghcount = ini.Get_Int("BosoGameHistory", "Count", 0);
    for (int i = 0; i < ghcount && GameHistoryCount < BOSO_MAX_GAME_HISTORY; i++) {
        char key[64];
        char uid[BOSO_UID_LEN] = {0};
        sprintf(key, "Game_%d_UID", i);
        ini.Get_String("BosoGameHistory", key, "", uid, sizeof(uid));
        if (uid[0] == '\0') continue;

        BosoGameHistoryEntry & e = GameHistory[GameHistoryCount++];
        memset(&e, 0, sizeof(e));
        strncpy(e.UID, uid, BOSO_UID_LEN - 1);

        sprintf(key, "Game_%d_Secs",    i); e.GameSecs = ini.Get_Int("BosoGameHistory", key, 0);
        sprintf(key, "Game_%d_Outcome", i); ini.Get_String("BosoGameHistory", key, "?", e.Outcome, sizeof(e.Outcome));
        sprintf(key, "Game_%d_Faction", i); ini.Get_String("BosoGameHistory", key, "", e.Faction, BOSO_FACTION_LEN);
        sprintf(key, "Game_%d_Map",     i); ini.Get_String("BosoGameHistory", key, "", e.Map, BOSO_MAP_LEN);
    }

    GlyphX_Debug_Print("BosoAI: data loaded");
}

void BosoAIManagerClass::Save_Data(void)
{
    char path[MAX_PATH];
    Get_Boso_INI_Path(path, sizeof(path));

    FILE * fp = fopen(path, "w");
    if (!fp) {
        GlyphX_Debug_Print("BosoAI: failed to open INI for writing");
        return;
    }

    fprintf(fp, "; BOSOAI.INI - BosoAI learning data\n");
    fprintf(fp, "; This file is auto-generated. Edit with care.\n\n");

    /*-- Limits records --*/
    fprintf(fp, "[BosoLimits]\n");
    int active = 0;
    for (int i = 0; i < LimitsCount; i++) if (LimitsRecords[i].Active) active++;
    fprintf(fp, "Count=%d\n", active);
    int idx = 0;
    for (int i = 0; i < LimitsCount; i++) {
        BosoLimitsRecord & r = LimitsRecords[i];
        if (!r.Active) continue;
        fprintf(fp, "Limits_%d_Faction=%s\n",       idx, r.Faction);
        fprintf(fp, "Limits_%d_Score=%d\n",          idx, r.WinScore);
        fprintf(fp, "Limits_%d_Games=%d\n",          idx, r.GamesPlayed);
        if (r.LastGameUID[0]) {
            fprintf(fp, "Limits_%d_LastUID=%s\n",    idx, r.LastGameUID);
            fprintf(fp, "Limits_%d_LastSecs=%d\n",   idx, r.LastGameSecs);
        }
        /* Ratios */
        fprintf(fp, "Limits_%d_DefenseRatio=%d\n",  idx, r.DefenseRatio);
        fprintf(fp, "Limits_%d_AARatio=%d\n",        idx, r.AARatio);
        fprintf(fp, "Limits_%d_TeslaRatio=%d\n",     idx, r.TeslaRatio);
        fprintf(fp, "Limits_%d_BarracksRatio=%d\n",  idx, r.BarracksRatio);
        fprintf(fp, "Limits_%d_WarRatio=%d\n",       idx, r.WarRatio);
        fprintf(fp, "Limits_%d_HelipadRatio=%d\n",   idx, r.HelipadRatio);
        fprintf(fp, "Limits_%d_AirstripRatio=%d\n",  idx, r.AirstripRatio);
        fprintf(fp, "Limits_%d_RefineryRatio=%d\n",  idx, r.RefineryRatio);
        /* Limits */
        fprintf(fp, "Limits_%d_DefenseLimit=%d\n",  idx, r.DefenseLimit);
        fprintf(fp, "Limits_%d_AALimit=%d\n",        idx, r.AALimit);
        fprintf(fp, "Limits_%d_TeslaLimit=%d\n",     idx, r.TeslaLimit);
        fprintf(fp, "Limits_%d_BarracksLimit=%d\n",  idx, r.BarracksLimit);
        fprintf(fp, "Limits_%d_WarLimit=%d\n",       idx, r.WarLimit);
        fprintf(fp, "Limits_%d_HelipadLimit=%d\n",   idx, r.HelipadLimit);
        fprintf(fp, "Limits_%d_AirstripLimit=%d\n",  idx, r.AirstripLimit);
        fprintf(fp, "Limits_%d_RefineryLimit=%d\n",  idx, r.RefineryLimit);
        /* Economy */
        fprintf(fp, "Limits_%d_PowerSurplus=%d\n",   idx, r.PowerSurplus);
        fprintf(fp, "Limits_%d_InfantryReserve=%d\n",idx, r.InfantryReserve);
        fprintf(fp, "Limits_%d_InfantryBaseMult=%d\n",idx,r.InfantryBaseMult);
        fprintf(fp, "Limits_%d_BaseSizeAdd=%d\n",    idx, r.BaseSizeAdd);
        /* Timing */
        fprintf(fp, "Limits_%d_AttackSecs=%d\n",     idx, r.AttackSecs);
        fprintf(fp, "Limits_%d_AttackDelaySecs=%d\n",idx, r.AttackDelaySecs);
        fprintf(fp, "Limits_%d_AutocreateSecs=%d\n", idx, r.AutocreateSecs);
        /* Bias */
        fprintf(fp, "Limits_%d_FirepowerBias=%d\n",  idx, r.FirepowerBias);
        fprintf(fp, "Limits_%d_GroundspeedBias=%d\n",idx, r.GroundspeedBias);
        fprintf(fp, "Limits_%d_ArmorBias=%d\n",      idx, r.ArmorBias);
        fprintf(fp, "Limits_%d_CostBias=%d\n",       idx, r.CostBias);
        fprintf(fp, "Limits_%d_BuildSpeedBias=%d\n", idx, r.BuildSpeedBias);
        idx++;
    }

    /*-- Game history --*/
    fprintf(fp, "\n[BosoGameHistory]\n");
    fprintf(fp, "Count=%d\n", GameHistoryCount);
    for (int i = 0; i < GameHistoryCount; i++) {
        const BosoGameHistoryEntry & e = GameHistory[i];
        fprintf(fp, "Game_%d_UID=%s\n",     i, e.UID);
        fprintf(fp, "Game_%d_Secs=%d\n",    i, e.GameSecs);
        fprintf(fp, "Game_%d_Outcome=%s\n", i, e.Outcome);
        fprintf(fp, "Game_%d_Faction=%s\n", i, e.Faction);
        fprintf(fp, "Game_%d_Map=%s\n",     i, e.Map);
    }

    fclose(fp);
    GlyphX_Debug_Print("BosoAI: data saved");
}
