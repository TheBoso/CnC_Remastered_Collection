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

/*-- Building order persistence --*/
BosoBuildOrderRecord BosoAIManagerClass::BuildOrders[BOSO_MAX_BUILD_ORDERS];
int                  BosoAIManagerClass::BuildOrderCount        = 0;

/*-- Building order session tracking --*/
char BosoAIManagerClass::SessionBuildSteps[BOSO_MAX_BUILD_STEPS][BOSO_BUILD_STEP_LEN];
int  BosoAIManagerClass::SessionBuildStepCount                  = 0;
int  BosoAIManagerClass::FollowBuildOrderIdx                    = -1;
int  BosoAIManagerClass::FollowBuildStepIdx                     = 0;

char BosoAIManagerClass::ExtraHouseBuildSteps[7][BOSO_MAX_BUILD_STEPS][BOSO_BUILD_STEP_LEN];
int  BosoAIManagerClass::ExtraHouseBuildStepCount[7]            = {0};
int  BosoAIManagerClass::ExtraHouseFollowBuildOrderIdx[7]       = {-1,-1,-1,-1,-1,-1,-1};
int  BosoAIManagerClass::ExtraHouseFollowBuildStepIdx[7]        = {0};

/*-- Combat order persistence --*/
BosoCombatOrderRecord BosoAIManagerClass::CombatOrders[BOSO_MAX_COMBAT_ORDERS];
int                   BosoAIManagerClass::CombatOrderCount      = 0;

/*-- Combat order session tracking --*/
char BosoAIManagerClass::SessionCombatSteps[BOSO_MAX_COMBAT_STEPS][BOSO_COMBAT_STEP_LEN];
int  BosoAIManagerClass::SessionCombatStepCount                 = 0;
int  BosoAIManagerClass::FollowCombatOrderIdx                   = -1;
int  BosoAIManagerClass::FollowCombatStepIdx                    = 0;

char BosoAIManagerClass::ExtraHouseCombatSteps[7][BOSO_MAX_COMBAT_STEPS][BOSO_COMBAT_STEP_LEN];
int  BosoAIManagerClass::ExtraHouseCombatStepCount[7]           = {0};
int  BosoAIManagerClass::ExtraHouseFollowCombatOrderIdx[7]      = {-1,-1,-1,-1,-1,-1,-1};
int  BosoAIManagerClass::ExtraHouseFollowCombatStepIdx[7]       = {0};

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

/*-- Global spectator flag --*/
bool BosoSpectatorActive = false;

/*===========================================================================
** File-scope helpers
===========================================================================*/

/*
** Build a full path to a BosoAI data file in the same directory as the DLL.
*/
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

/*
** Returns true if the faction is an Allied faction.
*/
static bool Is_Allied_Faction(const char * faction)
{
    return stricmp(faction, "Greece")  == 0 ||
           stricmp(faction, "England") == 0 ||
           stricmp(faction, "France")  == 0 ||
           stricmp(faction, "Germany") == 0 ||
           stricmp(faction, "Spain")   == 0 ||
           stricmp(faction, "Turkey")  == 0;
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
** Building order evolution
===========================================================================*/

BosoBuildOrderRecord * BosoAIManagerClass::Find_Build_Order(void)
{
    for (int i = 0; i < BuildOrderCount; i++) {
        BosoBuildOrderRecord & ord = BuildOrders[i];
        if (!ord.Active) continue;
        if (stricmp(ord.Faction, CurrentFaction) != 0) continue;
        if (ord.StepCount != SessionBuildStepCount) continue;
        bool match = true;
        for (int s = 0; s < SessionBuildStepCount; s++) {
            if (stricmp(ord.Steps[s], SessionBuildSteps[s]) != 0) { match = false; break; }
        }
        if (match) return &ord;
    }
    return NULL;
}

BosoBuildOrderRecord * BosoAIManagerClass::Alloc_Build_Order(void)
{
    if (BuildOrderCount < BOSO_MAX_BUILD_ORDERS) {
        BuildOrders[BuildOrderCount].Active = true;
        return &BuildOrders[BuildOrderCount++];
    }
    /*-- Evict lowest normalized-score slot --*/
    int   worst_idx   = 0;
    float worst_score = (float)BuildOrders[0].WinScore / (float)(BuildOrders[0].GamesPlayed > 0 ? BuildOrders[0].GamesPlayed : 1);
    for (int i = 1; i < BOSO_MAX_BUILD_ORDERS; i++) {
        float s = (float)BuildOrders[i].WinScore / (float)(BuildOrders[i].GamesPlayed > 0 ? BuildOrders[i].GamesPlayed : 1);
        if (s < worst_score) { worst_score = s; worst_idx = i; }
    }
    memset(&BuildOrders[worst_idx], 0, sizeof(BosoBuildOrderRecord));
    BuildOrders[worst_idx].Active = true;
    return &BuildOrders[worst_idx];
}

int BosoAIManagerClass::Mutate_Build_Order(int parent_idx)
{
    if (parent_idx < 0 || parent_idx >= BuildOrderCount) return -1;
    BosoBuildOrderRecord & parent = BuildOrders[parent_idx];
    if (!parent.Active || parent.StepCount < 2) return parent_idx;

    static const char * allied_pool[] = {
        "POWR","APWR","PROC","TENT","WEAP","GUN","PBOX","DOME","ATEK","SYRD", NULL
    };
    static const char * soviet_pool[] = {
        "POWR","APWR","PROC","BARR","WEAP","FTUR","TSLA","DOME","STEK","SPEN", NULL
    };
    const char ** pool     = Is_Allied_Faction(CurrentFaction) ? allied_pool : soviet_pool;
    int           pool_size = 0;
    while (pool[pool_size]) pool_size++;

    char steps[BOSO_MAX_BUILD_STEPS][BOSO_BUILD_STEP_LEN];
    memcpy(steps, parent.Steps, sizeof(steps));
    int step_count = parent.StepCount;

    int num_ops = 1 + (rand() % 3);
    for (int op_i = 0; op_i < num_ops; op_i++) {
        switch (rand() % 4) {
            case 0: /* Swap */
                if (step_count >= 2) {
                    int a = rand() % step_count;
                    int b = rand() % step_count;
                    if (a != b) {
                        char tmp[BOSO_BUILD_STEP_LEN];
                        memcpy(tmp, steps[a], BOSO_BUILD_STEP_LEN);
                        memcpy(steps[a], steps[b], BOSO_BUILD_STEP_LEN);
                        memcpy(steps[b], tmp, BOSO_BUILD_STEP_LEN);
                    }
                }
                break;
            case 1: /* Replace */
                if (step_count > 0 && pool_size > 0) {
                    int pos = rand() % step_count;
                    strncpy(steps[pos], pool[rand() % pool_size], BOSO_BUILD_STEP_LEN - 1);
                    steps[pos][BOSO_BUILD_STEP_LEN - 1] = '\0';
                }
                break;
            case 2: /* Insert */
                if (step_count < BOSO_MAX_BUILD_STEPS && pool_size > 0) {
                    int pos = rand() % (step_count + 1);
                    for (int i = step_count; i > pos; i--)
                        memcpy(steps[i], steps[i - 1], BOSO_BUILD_STEP_LEN);
                    strncpy(steps[pos], pool[rand() % pool_size], BOSO_BUILD_STEP_LEN - 1);
                    steps[pos][BOSO_BUILD_STEP_LEN - 1] = '\0';
                    step_count++;
                }
                break;
            case 3: /* Remove */
                if (step_count > 3) {
                    int pos = rand() % step_count;
                    for (int i = pos; i < step_count - 1; i++)
                        memcpy(steps[i], steps[i + 1], BOSO_BUILD_STEP_LEN);
                    step_count--;
                }
                break;
        }
    }

    bool changed = (step_count != parent.StepCount);
    if (!changed) {
        for (int i = 0; i < step_count; i++) {
            if (stricmp(steps[i], parent.Steps[i]) != 0) { changed = true; break; }
        }
    }
    if (!changed) return parent_idx;

    for (int i = 0; i < BuildOrderCount; i++) {
        BosoBuildOrderRecord & ord = BuildOrders[i];
        if (!ord.Active || ord.StepCount != step_count) continue;
        if (stricmp(ord.Faction, CurrentFaction) != 0) continue;
        bool match = true;
        for (int s = 0; s < step_count; s++) {
            if (stricmp(ord.Steps[s], steps[s]) != 0) { match = false; break; }
        }
        if (match) return i;
    }

    BosoBuildOrderRecord * rec = Alloc_Build_Order();
    if (!rec) return parent_idx;
    memcpy(rec->Steps, steps, sizeof(steps));
    rec->StepCount = step_count;
    strncpy(rec->Faction, CurrentFaction, BOSO_FACTION_LEN - 1);
    rec->WinScore = 0;
    rec->Active   = true;
    GlyphX_Debug_Print("BosoAI: created mutated build order");
    return (int)(rec - BuildOrders);
}

int BosoAIManagerClass::Select_Build_Order(void)
{
    return Select_Build_Order_Excluding(NULL, 0);
}

/*
** Select_Build_Order_Excluding - picks the best build order for CurrentFaction
** that is NOT already in the `exclude` list (used to give different houses
** different orders when they share a faction).
** Falls back to a forced mutation of the best order if all records are excluded,
** guaranteeing a structurally distinct order.
*/
int BosoAIManagerClass::Select_Build_Order_Excluding(int * exclude, int exclude_count)
{
    int   best_idx   = -1;
    float best_score = -1e30f;

    /*-- In exploit mode (IsTraining=false) all houses use the global best — no
    **  exclusion and no mutation. This converges everyone onto the proven order. --*/
    bool do_exclude = IsTraining && (exclude_count > 0);

    for (int i = 0; i < BuildOrderCount; i++) {
        BosoBuildOrderRecord & ord = BuildOrders[i];
        if (!ord.Active) continue;
        if (stricmp(ord.Faction, CurrentFaction) != 0) continue;
        if (do_exclude) {
            bool excluded = false;
            for (int e = 0; e < exclude_count; e++) {
                if (exclude[e] == i) { excluded = true; break; }
            }
            if (excluded) continue;
        }
        float norm = (float)ord.WinScore / (float)(ord.GamesPlayed > 0 ? ord.GamesPlayed : 1);
        if (norm > best_score) { best_score = norm; best_idx = i; }
    }

    if (best_idx < 0) {
        if (!IsTraining) return -1;
        /*-- Training: all records excluded (same-faction houses exhausted the pool).
        **  Fall back to the global best and force a mutation so this house
        **  still plays a structurally different order. --*/
        for (int i = 0; i < BuildOrderCount; i++) {
            BosoBuildOrderRecord & ord = BuildOrders[i];
            if (!ord.Active) continue;
            if (stricmp(ord.Faction, CurrentFaction) != 0) continue;
            float norm = (float)ord.WinScore / (float)(ord.GamesPlayed > 0 ? ord.GamesPlayed : 1);
            if (norm > best_score) { best_score = norm; best_idx = i; }
        }
        if (best_idx < 0) return -1;
        int mutant = Mutate_Build_Order(best_idx);
        if (mutant >= 0) {
            GlyphX_Debug_Print("BosoAI: forced mutated build order (faction pool exhausted)");
            return mutant;
        }
        return best_idx;
    }

    if (IsTraining && (rand() % 100) >= BOSO_EXPLOIT_CHANCE) {
        int mutant = Mutate_Build_Order(best_idx);
        if (mutant >= 0) {
            GlyphX_Debug_Print("BosoAI: following mutated build order");
            return mutant;
        }
    }
    return best_idx;
}

void BosoAIManagerClass::Score_Session_Build_Order(bool won)
{
    /*-- Score the followed order for the primary house --*/
    if (FollowBuildOrderIdx >= 0 && FollowBuildOrderIdx < BuildOrderCount) {
        BosoBuildOrderRecord & ord = BuildOrders[FollowBuildOrderIdx];
        if (won) ord.WinScore += BOSO_BUILD_ORDER_WIN + GameTimeBonusScore;
        else     ord.WinScore -= BOSO_BUILD_ORDER_LOSS;
        ord.GamesPlayed++;
        strncpy(ord.LastGameUID, CurrentGameUID, BOSO_UID_LEN - 1);
        ord.LastGameSecs = CurrentGameSecs;
    }

    /*-- Score the followed order for each extra house --*/
    for (int hi = 0; hi < 7; hi++) {
        int boso_idx = hi + 1;
        if (boso_idx >= BosoHouseCount) break;
        int bidx = ExtraHouseFollowBuildOrderIdx[hi];
        if (bidx >= 0 && bidx < BuildOrderCount) {
            BosoBuildOrderRecord & ord = BuildOrders[bidx];
            HouseClass * hptr = BosoHouses[boso_idx];
            bool house_won = hptr && !hptr->IsDefeated;
            if (house_won) ord.WinScore += BOSO_BUILD_ORDER_WIN + GameTimeBonusScore;
            else           ord.WinScore -= BOSO_BUILD_ORDER_LOSS;
            ord.GamesPlayed++;
            strncpy(ord.LastGameUID, CurrentGameUID, BOSO_UID_LEN - 1);
            ord.LastGameSecs = CurrentGameSecs;
        }
    }

    /*-- Save/score the actual steps recorded this session.
    **   skip_follow_idx: if the recorded steps exactly match that followed order,
    **   skip creating a duplicate — it was already scored above. --*/
    auto Save_Steps = [&](const char * faction,
                          char (*steps)[BOSO_BUILD_STEP_LEN],
                          int step_count,
                          int skip_follow_idx)
    {
        if (step_count < 3) return;

        /*-- Check for existing identical sequence --*/
        for (int bi = 0; bi < BuildOrderCount; bi++) {
            BosoBuildOrderRecord & ord = BuildOrders[bi];
            if (!ord.Active || ord.StepCount != step_count) continue;
            if (stricmp(ord.Faction, faction) != 0) continue;
            bool match = true;
            for (int s = 0; s < step_count; s++) {
                if (stricmp(ord.Steps[s], steps[s]) != 0) { match = false; break; }
            }
            if (match) {
                if (won) ord.WinScore += BOSO_BUILD_ORDER_WIN + GameTimeBonusScore;
                else     ord.WinScore -= BOSO_BUILD_ORDER_LOSS;
                ord.GamesPlayed++;
                strncpy(ord.LastGameUID, CurrentGameUID, BOSO_UID_LEN - 1);
                ord.LastGameSecs = CurrentGameSecs;
                return;
            }
        }

        /*-- Skip if these steps are identical to the followed order (already scored above) --*/
        if (skip_follow_idx >= 0 && skip_follow_idx < BuildOrderCount) {
            BosoBuildOrderRecord & followed = BuildOrders[skip_follow_idx];
            if (followed.StepCount == step_count && stricmp(followed.Faction, faction) == 0) {
                bool match = true;
                for (int s = 0; s < step_count; s++) {
                    if (stricmp(followed.Steps[s], steps[s]) != 0) { match = false; break; }
                }
                if (match) return;
            }
        }

        BosoBuildOrderRecord * rec = Alloc_Build_Order();
        if (!rec) return;
        rec->StepCount   = step_count;
        for (int s = 0; s < step_count; s++) {
            strncpy(rec->Steps[s], steps[s], BOSO_BUILD_STEP_LEN - 1);
            rec->Steps[s][BOSO_BUILD_STEP_LEN - 1] = '\0';
        }
        strncpy(rec->Faction, faction, BOSO_FACTION_LEN - 1);
        rec->WinScore    = won ? (BOSO_BUILD_ORDER_WIN + GameTimeBonusScore) : -BOSO_BUILD_ORDER_LOSS;
        rec->GamesPlayed = 1;
        strncpy(rec->LastGameUID, CurrentGameUID, BOSO_UID_LEN - 1);
        rec->LastGameSecs = CurrentGameSecs;
        rec->Active      = true;
    };

    Save_Steps(CurrentFaction, SessionBuildSteps, SessionBuildStepCount, FollowBuildOrderIdx);

    for (int hi = 0; hi < 7; hi++) {
        int boso_idx = hi + 1;
        if (boso_idx >= BosoHouseCount) break;
        const char * hf = Get_Faction_Name(BosoHouses[boso_idx]);
        Save_Steps(hf, ExtraHouseBuildSteps[hi], ExtraHouseBuildStepCount[hi], ExtraHouseFollowBuildOrderIdx[hi]);
    }
}

/*===========================================================================
** Combat order evolution
===========================================================================*/

BosoCombatOrderRecord * BosoAIManagerClass::Find_Combat_Order(void)
{
    for (int i = 0; i < CombatOrderCount; i++) {
        BosoCombatOrderRecord & ord = CombatOrders[i];
        if (!ord.Active) continue;
        if (stricmp(ord.Faction, CurrentFaction) != 0) continue;
        if (ord.StepCount != SessionCombatStepCount) continue;
        bool match = true;
        for (int s = 0; s < SessionCombatStepCount; s++) {
            if (stricmp(ord.Steps[s], SessionCombatSteps[s]) != 0) { match = false; break; }
        }
        if (match) return &ord;
    }
    return NULL;
}

BosoCombatOrderRecord * BosoAIManagerClass::Alloc_Combat_Order(void)
{
    if (CombatOrderCount < BOSO_MAX_COMBAT_ORDERS) {
        CombatOrders[CombatOrderCount].Active = true;
        return &CombatOrders[CombatOrderCount++];
    }
    int   worst_idx   = 0;
    float worst_score = (float)CombatOrders[0].WinScore / (float)(CombatOrders[0].GamesPlayed > 0 ? CombatOrders[0].GamesPlayed : 1);
    for (int i = 1; i < BOSO_MAX_COMBAT_ORDERS; i++) {
        float s = (float)CombatOrders[i].WinScore / (float)(CombatOrders[i].GamesPlayed > 0 ? CombatOrders[i].GamesPlayed : 1);
        if (s < worst_score) { worst_score = s; worst_idx = i; }
    }
    memset(&CombatOrders[worst_idx], 0, sizeof(BosoCombatOrderRecord));
    CombatOrders[worst_idx].Active = true;
    return &CombatOrders[worst_idx];
}

int BosoAIManagerClass::Mutate_Combat_Order(int parent_idx)
{
    if (parent_idx < 0 || parent_idx >= CombatOrderCount) return -1;
    BosoCombatOrderRecord & parent = CombatOrders[parent_idx];
    if (!parent.Active || parent.StepCount < 2) return parent_idx;

    /*-- Build a donor pool: all steps from other orders of the same faction.
    **  This guarantees all IniNames are valid without hardcoded lookup tables. --*/
    const char * donor_pool[BOSO_MAX_COMBAT_ORDERS * BOSO_MAX_COMBAT_STEPS];
    int donor_count = 0;
    for (int i = 0; i < CombatOrderCount && donor_count < (int)(sizeof(donor_pool)/sizeof(donor_pool[0])); i++) {
        if (i == parent_idx) continue;
        BosoCombatOrderRecord & ord = CombatOrders[i];
        if (!ord.Active || stricmp(ord.Faction, CurrentFaction) != 0) continue;
        for (int s = 0; s < ord.StepCount; s++) {
            donor_pool[donor_count++] = ord.Steps[s];
        }
    }

    char steps[BOSO_MAX_COMBAT_STEPS][BOSO_COMBAT_STEP_LEN];
    memcpy(steps, parent.Steps, sizeof(steps));
    int step_count = parent.StepCount;

    int num_ops = 1 + (rand() % 3);
    for (int op_i = 0; op_i < num_ops; op_i++) {
        int op = (donor_count > 0) ? (rand() % 4) : (rand() % 2);  // only swap/remove if no donors
        switch (op) {
            case 0: /* Swap */
                if (step_count >= 2) {
                    int a = rand() % step_count;
                    int b = rand() % step_count;
                    if (a != b) {
                        char tmp[BOSO_COMBAT_STEP_LEN];
                        memcpy(tmp, steps[a], BOSO_COMBAT_STEP_LEN);
                        memcpy(steps[a], steps[b], BOSO_COMBAT_STEP_LEN);
                        memcpy(steps[b], tmp, BOSO_COMBAT_STEP_LEN);
                    }
                }
                break;
            case 1: /* Remove */
                if (step_count > 3) {
                    int pos = rand() % step_count;
                    for (int i = pos; i < step_count - 1; i++)
                        memcpy(steps[i], steps[i + 1], BOSO_COMBAT_STEP_LEN);
                    step_count--;
                }
                break;
            case 2: /* Replace with donor */
                if (step_count > 0 && donor_count > 0) {
                    int pos = rand() % step_count;
                    strncpy(steps[pos], donor_pool[rand() % donor_count], BOSO_COMBAT_STEP_LEN - 1);
                    steps[pos][BOSO_COMBAT_STEP_LEN - 1] = '\0';
                }
                break;
            case 3: /* Insert donor */
                if (step_count < BOSO_MAX_COMBAT_STEPS && donor_count > 0) {
                    int pos = rand() % (step_count + 1);
                    for (int i = step_count; i > pos; i--)
                        memcpy(steps[i], steps[i - 1], BOSO_COMBAT_STEP_LEN);
                    strncpy(steps[pos], donor_pool[rand() % donor_count], BOSO_COMBAT_STEP_LEN - 1);
                    steps[pos][BOSO_COMBAT_STEP_LEN - 1] = '\0';
                    step_count++;
                }
                break;
        }
    }

    bool changed = (step_count != parent.StepCount);
    if (!changed) {
        for (int i = 0; i < step_count; i++) {
            if (stricmp(steps[i], parent.Steps[i]) != 0) { changed = true; break; }
        }
    }
    if (!changed) return parent_idx;

    for (int i = 0; i < CombatOrderCount; i++) {
        BosoCombatOrderRecord & ord = CombatOrders[i];
        if (!ord.Active || ord.StepCount != step_count) continue;
        if (stricmp(ord.Faction, CurrentFaction) != 0) continue;
        bool match = true;
        for (int s = 0; s < step_count; s++) {
            if (stricmp(ord.Steps[s], steps[s]) != 0) { match = false; break; }
        }
        if (match) return i;
    }

    BosoCombatOrderRecord * rec = Alloc_Combat_Order();
    if (!rec) return parent_idx;
    memcpy(rec->Steps, steps, sizeof(steps));
    rec->StepCount = step_count;
    strncpy(rec->Faction, CurrentFaction, BOSO_FACTION_LEN - 1);
    rec->WinScore = 0;
    rec->Active   = true;
    GlyphX_Debug_Print("BosoAI: created mutated combat order");
    return (int)(rec - CombatOrders);
}

/*
** Select_Combat_Order_Excluding - picks the best combat order for CurrentFaction
** that is NOT already in the `exclude` list.
** In exploit mode (IsTraining=false) exclusion and mutation are skipped — every
** house converges on the globally best known order.
*/
int BosoAIManagerClass::Select_Combat_Order_Excluding(int * exclude, int exclude_count)
{
    int   best_idx   = -1;
    float best_score = -1e30f;

    bool do_exclude = IsTraining && (exclude_count > 0);

    for (int i = 0; i < CombatOrderCount; i++) {
        BosoCombatOrderRecord & ord = CombatOrders[i];
        if (!ord.Active) continue;
        if (stricmp(ord.Faction, CurrentFaction) != 0) continue;
        if (do_exclude) {
            bool excluded = false;
            for (int e = 0; e < exclude_count; e++) {
                if (exclude[e] == i) { excluded = true; break; }
            }
            if (excluded) continue;
        }
        float norm = (float)ord.WinScore / (float)(ord.GamesPlayed > 0 ? ord.GamesPlayed : 1);
        if (norm > best_score) { best_score = norm; best_idx = i; }
    }

    if (best_idx < 0) {
        if (!IsTraining) return -1;
        /*-- Training: all records excluded — force mutate from global best. --*/
        for (int i = 0; i < CombatOrderCount; i++) {
            BosoCombatOrderRecord & ord = CombatOrders[i];
            if (!ord.Active) continue;
            if (stricmp(ord.Faction, CurrentFaction) != 0) continue;
            float norm = (float)ord.WinScore / (float)(ord.GamesPlayed > 0 ? ord.GamesPlayed : 1);
            if (norm > best_score) { best_score = norm; best_idx = i; }
        }
        if (best_idx < 0) return -1;
        int mutant = Mutate_Combat_Order(best_idx);
        if (mutant >= 0) {
            GlyphX_Debug_Print("BosoAI: forced mutated combat order (faction pool exhausted)");
            return mutant;
        }
        return best_idx;
    }

    if (IsTraining && (rand() % 100) >= BOSO_EXPLOIT_CHANCE) {
        int mutant = Mutate_Combat_Order(best_idx);
        if (mutant >= 0) {
            GlyphX_Debug_Print("BosoAI: following mutated combat order");
            return mutant;
        }
    }
    return best_idx;
}

int BosoAIManagerClass::Select_Combat_Order(void)
{
    return Select_Combat_Order_Excluding(NULL, 0);
}

void BosoAIManagerClass::Score_Session_Combat_Order(bool won)
{
    /*-- Score the followed order for the primary house --*/
    if (FollowCombatOrderIdx >= 0 && FollowCombatOrderIdx < CombatOrderCount) {
        BosoCombatOrderRecord & ord = CombatOrders[FollowCombatOrderIdx];
        if (won) ord.WinScore += BOSO_COMBAT_ORDER_WIN + GameTimeBonusScore;
        else     ord.WinScore -= BOSO_COMBAT_ORDER_LOSS;
        ord.GamesPlayed++;
        strncpy(ord.LastGameUID, CurrentGameUID, BOSO_UID_LEN - 1);
        ord.LastGameSecs = CurrentGameSecs;
    }

    /*-- Score the followed order for each extra house --*/
    for (int hi = 0; hi < 7; hi++) {
        int boso_idx = hi + 1;
        if (boso_idx >= BosoHouseCount) break;
        int cidx = ExtraHouseFollowCombatOrderIdx[hi];
        if (cidx >= 0 && cidx < CombatOrderCount) {
            BosoCombatOrderRecord & ord = CombatOrders[cidx];
            HouseClass * hptr = BosoHouses[boso_idx];
            bool house_won = hptr && !hptr->IsDefeated;
            if (house_won) ord.WinScore += BOSO_COMBAT_ORDER_WIN + GameTimeBonusScore;
            else           ord.WinScore -= BOSO_COMBAT_ORDER_LOSS;
            ord.GamesPlayed++;
            strncpy(ord.LastGameUID, CurrentGameUID, BOSO_UID_LEN - 1);
            ord.LastGameSecs = CurrentGameSecs;
        }
    }

    auto Save_Combat_Steps = [&](const char * faction,
                                 char (*steps)[BOSO_COMBAT_STEP_LEN],
                                 int step_count,
                                 int skip_follow_idx)
    {
        if (step_count < 3) return;

        for (int bi = 0; bi < CombatOrderCount; bi++) {
            BosoCombatOrderRecord & ord = CombatOrders[bi];
            if (!ord.Active || ord.StepCount != step_count) continue;
            if (stricmp(ord.Faction, faction) != 0) continue;
            bool match = true;
            for (int s = 0; s < step_count; s++) {
                if (stricmp(ord.Steps[s], steps[s]) != 0) { match = false; break; }
            }
            if (match) {
                if (won) ord.WinScore += BOSO_COMBAT_ORDER_WIN + GameTimeBonusScore;
                else     ord.WinScore -= BOSO_COMBAT_ORDER_LOSS;
                ord.GamesPlayed++;
                strncpy(ord.LastGameUID, CurrentGameUID, BOSO_UID_LEN - 1);
                ord.LastGameSecs = CurrentGameSecs;
                return;
            }
        }

        /*-- Skip if these steps are identical to the followed order (already scored above) --*/
        if (skip_follow_idx >= 0 && skip_follow_idx < CombatOrderCount) {
            BosoCombatOrderRecord & followed = CombatOrders[skip_follow_idx];
            if (followed.StepCount == step_count && stricmp(followed.Faction, faction) == 0) {
                bool match = true;
                for (int s = 0; s < step_count; s++) {
                    if (stricmp(followed.Steps[s], steps[s]) != 0) { match = false; break; }
                }
                if (match) return;
            }
        }

        BosoCombatOrderRecord * rec = Alloc_Combat_Order();
        if (!rec) return;
        rec->StepCount   = step_count;
        for (int s = 0; s < step_count; s++) {
            strncpy(rec->Steps[s], steps[s], BOSO_COMBAT_STEP_LEN - 1);
            rec->Steps[s][BOSO_COMBAT_STEP_LEN - 1] = '\0';
        }
        strncpy(rec->Faction, faction, BOSO_FACTION_LEN - 1);
        rec->WinScore    = won ? (BOSO_COMBAT_ORDER_WIN + GameTimeBonusScore) : -BOSO_COMBAT_ORDER_LOSS;
        rec->GamesPlayed = 1;
        strncpy(rec->LastGameUID, CurrentGameUID, BOSO_UID_LEN - 1);
        rec->LastGameSecs = CurrentGameSecs;
        rec->Active      = true;
    };

    Save_Combat_Steps(CurrentFaction, SessionCombatSteps, SessionCombatStepCount, FollowCombatOrderIdx);

    for (int hi = 0; hi < 7; hi++) {
        int boso_idx = hi + 1;
        if (boso_idx >= BosoHouseCount) break;
        const char * hf = Get_Faction_Name(BosoHouses[boso_idx]);
        Save_Combat_Steps(hf, ExtraHouseCombatSteps[hi], ExtraHouseCombatStepCount[hi], ExtraHouseFollowCombatOrderIdx[hi]);
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
    r.MaxUnit      = BOSO_LIMITS_DEF_UNIT;
    r.MaxBuilding  = BOSO_LIMITS_DEF_BUILD;
    r.MaxInfantry  = BOSO_LIMITS_DEF_INF;
    r.MaxVessel    = BOSO_LIMITS_DEF_VESSEL;
    r.MaxAircraft  = BOSO_LIMITS_DEF_AIRCRAFT;
    r.MaxRefinery  = BOSO_LIMITS_DEF_REFINERY;
    r.MaxBarracks  = BOSO_LIMITS_DEF_BARRACKS;
    r.MaxWarFactory= BOSO_LIMITS_DEF_WAR;
    r.MaxDefense   = BOSO_LIMITS_DEF_DEFENSE;
    r.MaxAA        = BOSO_LIMITS_DEF_AA;
    r.MaxTesla     = BOSO_LIMITS_DEF_TESLA;
    r.MaxHelipad   = BOSO_LIMITS_DEF_HELIPAD;
    r.MaxAirstrip  = BOSO_LIMITS_DEF_AIRSTRIP;
    r.AttackSecs   = BOSO_LIMITS_DEF_ATTACK;
    r.BaseSizeAdd  = BOSO_LIMITS_DEF_BASESIZE;
    r.BaseSpacing  = BOSO_LIMITS_DEF_SPACING;
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

    rec.MaxUnit      = mutate(rec.MaxUnit,      BOSO_LIMITS_MUT_UNIT,    BOSO_LIMITS_MIN_UNIT,    BOSO_LIMITS_MAX_UNIT);
    rec.MaxBuilding  = mutate(rec.MaxBuilding,  BOSO_LIMITS_MUT_BUILD,   BOSO_LIMITS_MIN_BUILD,   BOSO_LIMITS_MAX_BUILD);
    rec.MaxInfantry  = mutate(rec.MaxInfantry,  BOSO_LIMITS_MUT_INF,     BOSO_LIMITS_MIN_INF,     BOSO_LIMITS_MAX_INF);
    rec.MaxVessel    = mutate(rec.MaxVessel,    BOSO_LIMITS_MUT_VESSEL,  BOSO_LIMITS_MIN_VESSEL,  BOSO_LIMITS_MAX_VESSEL);
    rec.MaxAircraft  = mutate(rec.MaxAircraft,  BOSO_LIMITS_MUT_AIRCRAFT,BOSO_LIMITS_MIN_AIRCRAFT,BOSO_LIMITS_MAX_AIRCRAFT);
    rec.MaxRefinery  = mutate(rec.MaxRefinery,  BOSO_LIMITS_MUT_PERTYPE, BOSO_LIMITS_MIN_PERTYPE, BOSO_LIMITS_MAX_REFINERY);
    rec.MaxBarracks  = mutate(rec.MaxBarracks,  BOSO_LIMITS_MUT_PERTYPE, BOSO_LIMITS_MIN_PERTYPE, BOSO_LIMITS_MAX_BARRACKS);
    rec.MaxWarFactory= mutate(rec.MaxWarFactory,BOSO_LIMITS_MUT_PERTYPE, BOSO_LIMITS_MIN_PERTYPE, BOSO_LIMITS_MAX_WAR);
    rec.MaxDefense   = mutate(rec.MaxDefense,   BOSO_LIMITS_MUT_PERTYPE, BOSO_LIMITS_MIN_PERTYPE, BOSO_LIMITS_MAX_DEFENSE);
    rec.MaxAA        = mutate(rec.MaxAA,        BOSO_LIMITS_MUT_PERTYPE, BOSO_LIMITS_MIN_PERTYPE, BOSO_LIMITS_MAX_AA);
    rec.MaxTesla     = mutate(rec.MaxTesla,     BOSO_LIMITS_MUT_PERTYPE, BOSO_LIMITS_MIN_PERTYPE, BOSO_LIMITS_MAX_TESLA);
    rec.MaxHelipad   = mutate(rec.MaxHelipad,   BOSO_LIMITS_MUT_PERTYPE, BOSO_LIMITS_MIN_PERTYPE, BOSO_LIMITS_MAX_HELIPAD);
    rec.MaxAirstrip  = mutate(rec.MaxAirstrip,  BOSO_LIMITS_MUT_PERTYPE, BOSO_LIMITS_MIN_PERTYPE, BOSO_LIMITS_MAX_AIRSTRIP);
    rec.AttackSecs   = mutate(rec.AttackSecs,   BOSO_LIMITS_MUT_ATTACK,    BOSO_LIMITS_MIN_ATTACK,    BOSO_LIMITS_MAX_ATTACK);
    rec.BaseSizeAdd  = mutate(rec.BaseSizeAdd,  BOSO_LIMITS_MUT_BASESIZE,  BOSO_LIMITS_MIN_BASESIZE,  BOSO_LIMITS_MAX_BASESIZE);
    rec.BaseSpacing  = mutate(rec.BaseSpacing,  BOSO_LIMITS_MUT_SPACING,   BOSO_LIMITS_MIN_SPACING,   BOSO_LIMITS_MAX_SPACING);
    rec.WinScore = 0;
    rec.GamesPlayed = 0;
    strncpy(rec.Faction, faction, BOSO_FACTION_LEN - 1);

    /*-- Dedup: reuse any existing identical record for this faction --*/
    for (int i = 0; i < LimitsCount; i++) {
        BosoLimitsRecord & e = LimitsRecords[i];
        if (!e.Active || stricmp(e.Faction, faction) != 0) continue;
        if (e.MaxUnit == rec.MaxUnit && e.MaxBuilding == rec.MaxBuilding &&
            e.MaxInfantry == rec.MaxInfantry && e.MaxVessel == rec.MaxVessel &&
            e.MaxAircraft == rec.MaxAircraft && e.MaxRefinery == rec.MaxRefinery &&
            e.MaxBarracks == rec.MaxBarracks && e.MaxWarFactory == rec.MaxWarFactory &&
            e.MaxDefense == rec.MaxDefense && e.MaxAA == rec.MaxAA &&
            e.MaxTesla == rec.MaxTesla && e.MaxHelipad == rec.MaxHelipad &&
            e.MaxAirstrip == rec.MaxAirstrip && e.AttackSecs == rec.AttackSecs &&
            e.BaseSizeAdd == rec.BaseSizeAdd && e.BaseSpacing == rec.BaseSpacing) {
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
        /*-- Normalise by games played so a 10-game record with score -3
             ranks above a 1-game record with score -3 (more reliable data) --*/
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
    house->Control.MaxUnit      = (unsigned short)lim.MaxUnit;
    house->Control.MaxBuilding  = (unsigned short)lim.MaxBuilding;
    house->Control.MaxInfantry  = (unsigned short)lim.MaxInfantry;
    house->Control.MaxVessel    = (unsigned short)lim.MaxVessel;
    house->Control.MaxAircraft  = (unsigned short)lim.MaxAircraft;
    house->Control.TechLevel    = 10;
    house->Attack               = lim.AttackSecs * TICKS_PER_SECOND;
    if (IsCostZero) {
        house->CostBias         = fixed(0);
        house->Credits          = 99999;
        house->Control.InitialCredits = 99999;
    }
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
    AircraftLossCount   = 0;
    memset(NavalViable, 0, sizeof(NavalViable));
    BosoHouseCount  = 0;
    CurrentMap[0]   = '\0';
    CurrentFaction[0] = '\0';
    CurrentEnemyFaction[0] = '\0';

    memset(BosoHouses,               0, sizeof(BosoHouses));
    memset(SessionBuildSteps,        0, sizeof(SessionBuildSteps));
    memset(ExtraHouseBuildSteps,     0, sizeof(ExtraHouseBuildSteps));
    memset(ExtraHouseBuildStepCount, 0, sizeof(ExtraHouseBuildStepCount));
    memset(SessionCombatSteps,       0, sizeof(SessionCombatSteps));
    memset(ExtraHouseCombatSteps,    0, sizeof(ExtraHouseCombatSteps));
    memset(ExtraHouseCombatStepCount,0, sizeof(ExtraHouseCombatStepCount));
    memset(LimitsRecords,            0, sizeof(LimitsRecords));
    memset(HouseLimits,              0, sizeof(HouseLimits));

    SessionBuildStepCount  = 0;
    FollowBuildOrderIdx    = -1;
    FollowBuildStepIdx     = 0;
    SessionCombatStepCount = 0;
    FollowCombatOrderIdx   = -1;
    FollowCombatStepIdx    = 0;
    LimitsCount            = 0;
    for (int i = 0; i < 8; i++) FollowLimitsIdx[i] = -1;
    for (int i = 0; i < 7; i++) {
        ExtraHouseFollowBuildOrderIdx[i]  = -1;
        ExtraHouseFollowBuildStepIdx[i]   = 0;
        ExtraHouseFollowCombatOrderIdx[i] = -1;
        ExtraHouseFollowCombatStepIdx[i]  = 0;
    }
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

    BosoHouse = PlayerPtr;
    BosoHouseCount = 0;
    BosoHouses[BosoHouseCount++] = PlayerPtr;

    {
        char _sl_dbg[128];
        sprintf(_sl_dbg, "BosoAI: On_Scenario_Load Active=%d AutoRestart=%d MultiHouse=%d",
            (int)IsBosoActive, (int)IsAutoRestart, (int)IsMultiHouseAI);
        GlyphX_Debug_Print(_sl_dbg);
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
    SessionBuildStepCount  = 0;
    FollowBuildOrderIdx    = -1;
    FollowBuildStepIdx     = 0;
    SessionCombatStepCount = 0;
    FollowCombatOrderIdx   = -1;
    FollowCombatStepIdx    = 0;
    for (int i = 0; i < 8; i++) FollowLimitsIdx[i] = -1;
    for (int i = 0; i < 7; i++) {
        ExtraHouseFollowBuildOrderIdx[i]  = -1;
        ExtraHouseFollowBuildStepIdx[i]   = 0;
        ExtraHouseFollowCombatOrderIdx[i] = -1;
        ExtraHouseFollowCombatStepIdx[i]  = 0;
    }
    memset(SessionBuildSteps,        0, sizeof(SessionBuildSteps));
    memset(ExtraHouseBuildSteps,     0, sizeof(ExtraHouseBuildSteps));
    memset(ExtraHouseBuildStepCount, 0, sizeof(ExtraHouseBuildStepCount));
    memset(SessionCombatSteps,       0, sizeof(SessionCombatSteps));
    memset(ExtraHouseCombatSteps,    0, sizeof(ExtraHouseCombatSteps));
    memset(ExtraHouseCombatStepCount,0, sizeof(ExtraHouseCombatStepCount));
    memset(HouseLimits,              0, sizeof(HouseLimits));

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
        char _dbg[160];
        sprintf(_dbg, "BosoAI Limits[0]: Unit=%d Bld=%d Inf=%d Vessel=%d Air=%d Attack=%ds",
            L.MaxUnit, L.MaxBuilding, L.MaxInfantry, L.MaxVessel, L.MaxAircraft, L.AttackSecs);
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

    /*-- Select primary house orders first so they can be excluded when picking
    **   for extra houses (prevents same-faction houses running identical orders). --*/
    FollowBuildOrderIdx  = Select_Build_Order();
    FollowCombatOrderIdx = Select_Combat_Order();
    if (FollowBuildOrderIdx  >= 0) GlyphX_Debug_Print("BosoAI: following saved build order");
    if (FollowCombatOrderIdx >= 0) GlyphX_Debug_Print("BosoAI: following saved combat order");

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
            {
                int hi = slot - 1; /* slot 0 = primary, hi maps slot 1-7 → 0-6 */

                /*-- Build exclude list: primary index + all already-assigned extra house indices. --*/
                int exclude[8];
                int exclude_count = 0;
                if (FollowBuildOrderIdx >= 0) exclude[exclude_count++] = FollowBuildOrderIdx;
                for (int ex = 0; ex < hi; ex++) {
                    if (ExtraHouseFollowBuildOrderIdx[ex] >= 0)
                        exclude[exclude_count++] = ExtraHouseFollowBuildOrderIdx[ex];
                }
                int cexclude[8];
                int cexclude_count = 0;
                if (FollowCombatOrderIdx >= 0) cexclude[cexclude_count++] = FollowCombatOrderIdx;
                for (int ex = 0; ex < hi; ex++) {
                    if (ExtraHouseFollowCombatOrderIdx[ex] >= 0)
                        cexclude[cexclude_count++] = ExtraHouseFollowCombatOrderIdx[ex];
                }

                char saved[BOSO_FACTION_LEN];
                strncpy(saved, CurrentFaction, sizeof(saved));
                strncpy(CurrentFaction, hfaction, BOSO_FACTION_LEN - 1);
                CurrentFaction[BOSO_FACTION_LEN - 1] = '\0';
                ExtraHouseFollowBuildOrderIdx[hi]  = Select_Build_Order_Excluding(exclude, exclude_count);
                ExtraHouseFollowBuildStepIdx[hi]   = 0;
                ExtraHouseFollowCombatOrderIdx[hi] = Select_Combat_Order_Excluding(cexclude, cexclude_count);
                ExtraHouseFollowCombatStepIdx[hi]  = 0;
                strncpy(CurrentFaction, saved, sizeof(CurrentFaction));
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
    if (!IsBosoActive || GameOverFired) return;
    GameOverFired = true;

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

    Record_Game_History(CurrentGameUID, CurrentGameSecs,
                        player_won ? "W" : "L",
                        CurrentFaction, CurrentMap);

    Score_Session_Build_Order(player_won);
    Score_Session_Combat_Order(player_won);

    /*-- Score each house's limits individually based on its own survival --*/
    for (int i = 0; i < BosoHouseCount; i++) {
        bool house_won = (BosoHouses[i] && !BosoHouses[i]->IsDefeated);
        Score_Session_Limits(i, house_won);
    }

    Save_Data();

    if (IsAutoRestart) {
        GlyphX_Debug_Print("BosoAI: auto-restart triggered");
        PlayerRestarts = true;
    }
}

/*===========================================================================
** Building order hooks
===========================================================================*/

void BosoAIManagerClass::On_Building_Queued(HouseClass const * house, StructType stype)
{
    if (!IsBosoActive) return;

    /*-- Filter out defense/AA/support structures — they are governed by BosoLimits
    **   per-type caps and should not pollute the build order sequence. The build
    **   order should only capture economy and production infrastructure decisions. --*/
    switch (stype) {
        case STRUCT_TURRET:       // Gun Turret
        case STRUCT_AAGUN:        // AA Gun
        case STRUCT_FLAME_TURRET: // Flame Tower
        case STRUCT_TESLA:        // Tesla Coil
        case STRUCT_SAM:          // SAM Site
        case STRUCT_GAP:          // Gap Generator
        case STRUCT_KENNEL:       // Kennel
        case STRUCT_PILLBOX:      // Pillbox
            return;
        default:
            break;
    }

    const BuildingTypeClass & btype = BuildingTypeClass::As_Reference(stype);

    if (house == BosoHouse) {
        if (SessionBuildStepCount < BOSO_MAX_BUILD_STEPS) {
            strncpy(SessionBuildSteps[SessionBuildStepCount], btype.IniName, BOSO_BUILD_STEP_LEN - 1);
            SessionBuildSteps[SessionBuildStepCount][BOSO_BUILD_STEP_LEN - 1] = '\0';
            SessionBuildStepCount++;
        }
        if (FollowBuildOrderIdx >= 0 && FollowBuildOrderIdx < BuildOrderCount) {
            BosoBuildOrderRecord & ord = BuildOrders[FollowBuildOrderIdx];
            if (FollowBuildStepIdx < ord.StepCount) {
                if (stricmp(ord.Steps[FollowBuildStepIdx], btype.IniName) == 0) {
                    FollowBuildStepIdx++;
                }
            }
        }
    } else {
        for (int i = 1; i < BosoHouseCount && i <= 7; i++) {
            if (BosoHouses[i] == house) {
                int hi = i - 1;
                if (ExtraHouseBuildStepCount[hi] < BOSO_MAX_BUILD_STEPS) {
                    strncpy(ExtraHouseBuildSteps[hi][ExtraHouseBuildStepCount[hi]],
                            btype.IniName, BOSO_BUILD_STEP_LEN - 1);
                    ExtraHouseBuildSteps[hi][ExtraHouseBuildStepCount[hi]][BOSO_BUILD_STEP_LEN - 1] = '\0';
                    ExtraHouseBuildStepCount[hi]++;
                }
                /*-- Advance follow step if this matches the expected next step --*/
                int bidx = ExtraHouseFollowBuildOrderIdx[hi];
                if (bidx >= 0 && bidx < BuildOrderCount) {
                    BosoBuildOrderRecord & ord = BuildOrders[bidx];
                    if (ExtraHouseFollowBuildStepIdx[hi] < ord.StepCount) {
                        if (stricmp(ord.Steps[ExtraHouseFollowBuildStepIdx[hi]], btype.IniName) == 0) {
                            ExtraHouseFollowBuildStepIdx[hi]++;
                        }
                    }
                }
                break;
            }
        }
    }
}

StructType BosoAIManagerClass::Get_Next_Build_Order_Step(HouseClass const * house)
{
    if (!IsBosoActive) return STRUCT_NONE;

    /*-- Determine which order/step indices apply to this house --*/
    int bidx;
    int * pstep;
    int naval_slot;

    if (house == BosoHouse) {
        bidx       = FollowBuildOrderIdx;
        pstep      = &FollowBuildStepIdx;
        naval_slot = 0;
    } else {
        bidx = -1;
        pstep = NULL;
        naval_slot = 0;
        for (int i = 1; i < BosoHouseCount && i <= 7; i++) {
            if (BosoHouses[i] == house) {
                int hi     = i - 1;
                bidx       = ExtraHouseFollowBuildOrderIdx[hi];
                pstep      = &ExtraHouseFollowBuildStepIdx[hi];
                naval_slot = i;
                break;
            }
        }
        if (!pstep) return STRUCT_NONE;
    }

    if (bidx < 0 || bidx >= BuildOrderCount) return STRUCT_NONE;

    BosoBuildOrderRecord & ord = BuildOrders[bidx];

    while (*pstep < ord.StepCount) {
        const char * next_name = ord.Steps[*pstep];
        if (next_name[0] == '\0') break;

        for (StructType s = STRUCT_FIRST; s < STRUCT_COUNT; s++) {
            const BuildingTypeClass & btype = BuildingTypeClass::As_Reference(s);
            if (stricmp(btype.IniName, next_name) == 0) {
                /*-- Skip naval buildings on maps with no water near this house. --*/
                if ((s == STRUCT_SHIP_YARD || s == STRUCT_SUB_PEN) && !NavalViable[naval_slot]) {
                    break; /* fall through to advance step */
                }
                return s;
            }
        }
        (*pstep)++;
    }
    return STRUCT_NONE;
}

/*===========================================================================
** Combat order hooks
===========================================================================*/

/*
** On_Unit_Queued - records the IniName of any combat unit/infantry/vessel
** just committed to production.  Called from HOUSE.CPP AI_Unit, AI_Infantry,
** AI_Vessel immediately after BuildUnit/BuildInfantry/BuildVessel is set.
*/
void BosoAIManagerClass::On_Unit_Queued(HouseClass const * house, const char * ini_name)
{
    if (!IsBosoActive || !ini_name || ini_name[0] == '\0') return;

    if (house == BosoHouse) {
        if (SessionCombatStepCount < BOSO_MAX_COMBAT_STEPS) {
            strncpy(SessionCombatSteps[SessionCombatStepCount], ini_name, BOSO_COMBAT_STEP_LEN - 1);
            SessionCombatSteps[SessionCombatStepCount][BOSO_COMBAT_STEP_LEN - 1] = '\0';
            SessionCombatStepCount++;
        }
        /*-- Advance the followed combat order step index --*/
        if (FollowCombatOrderIdx >= 0 && FollowCombatOrderIdx < CombatOrderCount) {
            BosoCombatOrderRecord & ord = CombatOrders[FollowCombatOrderIdx];
            if (FollowCombatStepIdx < ord.StepCount) {
                if (stricmp(ord.Steps[FollowCombatStepIdx], ini_name) == 0) {
                    FollowCombatStepIdx++;
                }
            }
        }
    } else {
        for (int i = 1; i < BosoHouseCount && i <= 7; i++) {
            if (BosoHouses[i] == house) {
                int hi = i - 1;
                if (ExtraHouseCombatStepCount[hi] < BOSO_MAX_COMBAT_STEPS) {
                    strncpy(ExtraHouseCombatSteps[hi][ExtraHouseCombatStepCount[hi]],
                            ini_name, BOSO_COMBAT_STEP_LEN - 1);
                    ExtraHouseCombatSteps[hi][ExtraHouseCombatStepCount[hi]][BOSO_COMBAT_STEP_LEN - 1] = '\0';
                    ExtraHouseCombatStepCount[hi]++;
                }
                /*-- Advance follow step if this matches the expected next step --*/
                int cidx = ExtraHouseFollowCombatOrderIdx[hi];
                if (cidx >= 0 && cidx < CombatOrderCount) {
                    BosoCombatOrderRecord & ord = CombatOrders[cidx];
                    if (ExtraHouseFollowCombatStepIdx[hi] < ord.StepCount) {
                        if (stricmp(ord.Steps[ExtraHouseFollowCombatStepIdx[hi]], ini_name) == 0) {
                            ExtraHouseFollowCombatStepIdx[hi]++;
                        }
                    }
                }
                break;
            }
        }
    }
}

/*
** Get_Next_Combat_Step - returns the IniName of the next unit to produce
** according to the current combat order, or NULL if no order is active.
** Supports both the primary BosoHouse and extra MultiHouseAI houses.
*/
const char * BosoAIManagerClass::Get_Next_Combat_Step(HouseClass const * house)
{
    if (!IsBosoActive) return NULL;

    int cidx;
    int * pstep;
    int naval_slot;

    if (house == BosoHouse) {
        cidx       = FollowCombatOrderIdx;
        pstep      = &FollowCombatStepIdx;
        naval_slot = 0;
    } else {
        cidx = -1;
        pstep = NULL;
        naval_slot = 0;
        for (int i = 1; i < BosoHouseCount && i <= 7; i++) {
            if (BosoHouses[i] == house) {
                int hi     = i - 1;
                cidx       = ExtraHouseFollowCombatOrderIdx[hi];
                pstep      = &ExtraHouseFollowCombatStepIdx[hi];
                naval_slot = i;
                break;
            }
        }
        if (!pstep) return NULL;
    }

    if (cidx < 0 || cidx >= CombatOrderCount) return NULL;

    BosoCombatOrderRecord & ord = CombatOrders[cidx];

    while (*pstep < ord.StepCount) {
        const char * name = ord.Steps[*pstep];
        if (name[0] == '\0') break;

        /*-- Skip vessel steps on maps with no water near this house's base. --*/
        if (!NavalViable[naval_slot]) {
            bool is_vessel = false;
            for (VesselType v = VESSEL_FIRST; v < VESSEL_COUNT; v++) {
                if (stricmp(VesselTypeClass::As_Reference(v).IniName, name) == 0) {
                    is_vessel = true;
                    break;
                }
            }
            if (is_vessel) { (*pstep)++; continue; }
        }

        return name;
    }
    return NULL;
}

/*===========================================================================
** Stalemate detection
===========================================================================*/

/*
** Check_Stalemate - called once per second from HOUSE.CPP AI_Unit, gated to
** the primary BosoHouse only so it runs exactly once per second.
**
** A stalemate is declared when every active BosoAI house simultaneously has:
**   - money at or below BOSO_STALEMATE_MONEY_CAP (broke), AND
**   - at least one end of the income loop missing: no harvesters OR no refinery.
** Income requires both ends; if either is absent and the house can't afford to
** replace it, no recovery is possible.
**
** If this persists for BOSO_STALEMATE_SECS consecutive seconds the game is
** scored as a loss for all orders (bad strategies should be penalised, not
** ignored) and then restarted if AutoRestart is enabled.
*/
void BosoAIManagerClass::Check_Stalemate(void)
{
    if (!IsBosoActive || GameOverFired) return;
    if (BosoHouseCount == 0) return;

    /*-- Check whether every active house is economically dead --*/
    bool all_deadlocked = true;
    for (int i = 0; i < BosoHouseCount; i++) {
        HouseClass * h = BosoHouses[i];
        if (!h || h->IsDefeated) continue;      // already out of the game; skip
        bool no_harvester = (h->UQuantity[UNIT_HARVESTER] == 0);
        bool no_refinery  = (h->BQuantity[STRUCT_REFINERY] == 0);
        bool broke        = (h->Available_Money() <= BOSO_STALEMATE_MONEY_CAP);
        if (!(broke && (no_harvester || no_refinery))) {
            all_deadlocked = false;
            break;
        }
    }

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

    /*-- Confirmed stalemate: score everything as a loss and restart --*/
    GlyphX_Debug_Print("BosoAI: stalemate confirmed - scoring as loss and restarting");
    GameOverFired      = true;
    GameTimeBonusScore = 0;     // no time bonus for a deadlock

    CurrentGameSecs = (int)((Frame - GameStartFrame) / TICKS_PER_SECOND);
    Record_Game_History(CurrentGameUID, CurrentGameSecs, "S", CurrentFaction, CurrentMap);

    Score_Session_Build_Order(false);
    Score_Session_Combat_Order(false);
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
** each non-primary house when IsMultiHouseAI is true. On_Scenario_Load runs
** before buildings/units are placed so secondary MULTI houses may not be
** IsActive yet; this deferred path handles that timing gap.
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

    {
        int hi = slot - 1;

        /*-- Build exclude list: primary + all already-assigned extra house indices. --*/
        int exclude[8];
        int exclude_count = 0;
        if (FollowBuildOrderIdx >= 0) exclude[exclude_count++] = FollowBuildOrderIdx;
        for (int ex = 0; ex < hi; ex++) {
            if (ExtraHouseFollowBuildOrderIdx[ex] >= 0)
                exclude[exclude_count++] = ExtraHouseFollowBuildOrderIdx[ex];
        }
        int cexclude[8];
        int cexclude_count = 0;
        if (FollowCombatOrderIdx >= 0) cexclude[cexclude_count++] = FollowCombatOrderIdx;
        for (int ex = 0; ex < hi; ex++) {
            if (ExtraHouseFollowCombatOrderIdx[ex] >= 0)
                cexclude[cexclude_count++] = ExtraHouseFollowCombatOrderIdx[ex];
        }

        char saved[BOSO_FACTION_LEN];
        strncpy(saved, CurrentFaction, sizeof(saved));
        strncpy(CurrentFaction, hfaction, BOSO_FACTION_LEN - 1);
        CurrentFaction[BOSO_FACTION_LEN - 1] = '\0';
        ExtraHouseFollowBuildOrderIdx[hi]  = Select_Build_Order_Excluding(exclude, exclude_count);
        ExtraHouseFollowBuildStepIdx[hi]   = 0;
        ExtraHouseFollowCombatOrderIdx[hi] = Select_Combat_Order_Excluding(cexclude, cexclude_count);
        ExtraHouseFollowCombatStepIdx[hi]  = 0;
        strncpy(CurrentFaction, saved, sizeof(CurrentFaction));
    }

    char _dbg[128];
    sprintf(_dbg, "BosoAI: late-registered secondary AI house (slot %d) IsHuman=%d",
            BosoHouseCount - 1, (int)house->IsHuman);
    GlyphX_Debug_Print(_dbg);
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

/*
** Water_Near_House_Base - returns true if any LAND_WATER cell exists within
** `radius` cells of the centroid of all buildings owned by `house`.
** Returns false if the house has no buildings yet.
*/
static bool Water_Near_House_Base(HouseClass * house, int radius)
{
    if (!house) return false;

    /*-- Find centroid of all owned buildings. --*/
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

    /*-- Scan for a coastal cell: a non-water cell with at least one water
         neighbour (N/S/E/W). Naval buildings must be placed on such cells,
         so this is the true test of whether a naval structure can ever be
         placed near this base. --*/
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
            if (Map[c].Land_Type() == LAND_WATER) continue; /* skip water cells */

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
    BuildOrderCount  = 0;
    CombatOrderCount = 0;
    memset(BuildOrders,  0, sizeof(BuildOrders));
    memset(CombatOrders, 0, sizeof(CombatOrders));

    char path[MAX_PATH];
    Get_Boso_INI_Path(path, sizeof(path));

    RawFileClass file(path);
    if (!file.Is_Available()) {
        GlyphX_Debug_Print("BosoAI: no INI file found, starting fresh");
        return;
    }

    CCINIClass ini;
    ini.Load(file, false);

    /*-- Building orders --*/
    int bocount = ini.Get_Int("BosoBuildOrders", "Count", 0);
    for (int i = 0; i < bocount && BuildOrderCount < BOSO_MAX_BUILD_ORDERS; i++) {
        char key[64];

        sprintf(key, "Order_%d_Faction", i);
        char faction[BOSO_FACTION_LEN] = {0};
        ini.Get_String("BosoBuildOrders", key, "", faction, sizeof(faction));
        if (faction[0] == '\0') continue;

        sprintf(key, "Order_%d_Score", i);
        int score = ini.Get_Int("BosoBuildOrders", key, 0);

        sprintf(key, "Order_%d_Games", i);
        int games = ini.Get_Int("BosoBuildOrders", key, 1);

        char last_uid[BOSO_UID_LEN] = {0};
        sprintf(key, "Order_%d_LastUID", i);
        ini.Get_String("BosoBuildOrders", key, "", last_uid, sizeof(last_uid));

        sprintf(key, "Order_%d_LastSecs", i);
        int last_secs = ini.Get_Int("BosoBuildOrders", key, 0);

        sprintf(key, "Order_%d_Steps", i);
        int steps = ini.Get_Int("BosoBuildOrders", key, 0);
        if (steps <= 0 || steps > BOSO_MAX_BUILD_STEPS) continue;

        BosoBuildOrderRecord & ord = BuildOrders[BuildOrderCount];
        memset(&ord, 0, sizeof(ord));
        strncpy(ord.Faction, faction, BOSO_FACTION_LEN - 1);
        ord.WinScore    = score;
        ord.GamesPlayed = games > 0 ? games : 1;
        strncpy(ord.LastGameUID, last_uid, BOSO_UID_LEN - 1);
        ord.LastGameSecs = last_secs;
        ord.StepCount   = steps;
        ord.Active      = true;

        bool valid = true;
        for (int s = 0; s < steps; s++) {
            sprintf(key, "Order_%d_Step_%d", i, s);
            char bname[BOSO_BUILD_STEP_LEN] = {0};
            ini.Get_String("BosoBuildOrders", key, "", bname, sizeof(bname));
            if (bname[0] == '\0') { valid = false; break; }
            strncpy(ord.Steps[s], bname, BOSO_BUILD_STEP_LEN - 1);
        }
        if (valid) BuildOrderCount++;
    }

    /*-- Combat orders --*/
    int cocount = ini.Get_Int("BosoCombatOrders", "Count", 0);
    for (int i = 0; i < cocount && CombatOrderCount < BOSO_MAX_COMBAT_ORDERS; i++) {
        char key[64];

        sprintf(key, "COrder_%d_Faction", i);
        char faction[BOSO_FACTION_LEN] = {0};
        ini.Get_String("BosoCombatOrders", key, "", faction, sizeof(faction));
        if (faction[0] == '\0') continue;

        sprintf(key, "COrder_%d_Score", i);
        int score = ini.Get_Int("BosoCombatOrders", key, 0);

        sprintf(key, "COrder_%d_Games", i);
        int games = ini.Get_Int("BosoCombatOrders", key, 1);

        char last_uid[BOSO_UID_LEN] = {0};
        sprintf(key, "COrder_%d_LastUID", i);
        ini.Get_String("BosoCombatOrders", key, "", last_uid, sizeof(last_uid));

        sprintf(key, "COrder_%d_LastSecs", i);
        int last_secs = ini.Get_Int("BosoCombatOrders", key, 0);

        sprintf(key, "COrder_%d_Steps", i);
        int steps = ini.Get_Int("BosoCombatOrders", key, 0);
        if (steps <= 0 || steps > BOSO_MAX_COMBAT_STEPS) continue;

        BosoCombatOrderRecord & ord = CombatOrders[CombatOrderCount];
        memset(&ord, 0, sizeof(ord));
        strncpy(ord.Faction, faction, BOSO_FACTION_LEN - 1);
        ord.WinScore    = score;
        ord.GamesPlayed = games > 0 ? games : 1;
        strncpy(ord.LastGameUID, last_uid, BOSO_UID_LEN - 1);
        ord.LastGameSecs = last_secs;
        ord.StepCount   = steps;
        ord.Active      = true;

        bool valid = true;
        for (int s = 0; s < steps; s++) {
            sprintf(key, "COrder_%d_Step_%d", i, s);
            char uname[BOSO_COMBAT_STEP_LEN] = {0};
            ini.Get_String("BosoCombatOrders", key, "", uname, sizeof(uname));
            if (uname[0] == '\0') { valid = false; break; }
            strncpy(ord.Steps[s], uname, BOSO_COMBAT_STEP_LEN - 1);
        }
        if (valid) CombatOrderCount++;
    }

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

        LGET(WinScore,    "Score",      0);
        LGET(MaxUnit,     "Unit",       BOSO_LIMITS_DEF_UNIT);
        LGET(MaxBuilding, "Building",   BOSO_LIMITS_DEF_BUILD);
        LGET(MaxInfantry, "Infantry",   BOSO_LIMITS_DEF_INF);
        LGET(MaxVessel,   "Vessel",     BOSO_LIMITS_DEF_VESSEL);
        LGET(MaxAircraft, "Aircraft",   BOSO_LIMITS_DEF_AIRCRAFT);
        LGET(MaxRefinery, "Refinery",   BOSO_LIMITS_DEF_REFINERY);
        LGET(MaxBarracks, "Barracks",   BOSO_LIMITS_DEF_BARRACKS);
        LGET(MaxWarFactory,"WarFactory",BOSO_LIMITS_DEF_WAR);
        LGET(MaxDefense,  "Defense",    BOSO_LIMITS_DEF_DEFENSE);
        LGET(MaxAA,       "AA",         BOSO_LIMITS_DEF_AA);
        LGET(MaxTesla,    "Tesla",      BOSO_LIMITS_DEF_TESLA);
        LGET(MaxHelipad,  "Helipad",    BOSO_LIMITS_DEF_HELIPAD);
        LGET(MaxAirstrip, "Airstrip",   BOSO_LIMITS_DEF_AIRSTRIP);
        LGET(AttackSecs,  "AttackSecs", BOSO_LIMITS_DEF_ATTACK);
        LGET(BaseSizeAdd, "BaseSizeAdd",BOSO_LIMITS_DEF_BASESIZE);
        LGET(BaseSpacing, "BaseSpacing",BOSO_LIMITS_DEF_SPACING);
        LGET(GamesPlayed, "Games",      0);
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

    /*-- Building orders --*/
    fprintf(fp, "[BosoBuildOrders]\n");
    int active = 0;
    for (int i = 0; i < BuildOrderCount; i++) if (BuildOrders[i].Active) active++;
    fprintf(fp, "Count=%d\n", active);
    int idx = 0;
    for (int i = 0; i < BuildOrderCount; i++) {
        BosoBuildOrderRecord & ord = BuildOrders[i];
        if (!ord.Active) continue;
        fprintf(fp, "Order_%d_Faction=%s\n",  idx, ord.Faction);
        fprintf(fp, "Order_%d_Score=%d\n",    idx, ord.WinScore);
        fprintf(fp, "Order_%d_Games=%d\n",    idx, ord.GamesPlayed);
        if (ord.LastGameUID[0]) {
            fprintf(fp, "Order_%d_LastUID=%s\n",  idx, ord.LastGameUID);
            fprintf(fp, "Order_%d_LastSecs=%d\n", idx, ord.LastGameSecs);
        }
        fprintf(fp, "Order_%d_Steps=%d\n",    idx, ord.StepCount);
        for (int s = 0; s < ord.StepCount; s++) {
            fprintf(fp, "Order_%d_Step_%d=%s\n", idx, s, ord.Steps[s]);
        }
        idx++;
    }

    /*-- Combat orders --*/
    fprintf(fp, "\n[BosoCombatOrders]\n");
    active = 0;
    for (int i = 0; i < CombatOrderCount; i++) if (CombatOrders[i].Active) active++;
    fprintf(fp, "Count=%d\n", active);
    idx = 0;
    for (int i = 0; i < CombatOrderCount; i++) {
        BosoCombatOrderRecord & ord = CombatOrders[i];
        if (!ord.Active) continue;
        fprintf(fp, "COrder_%d_Faction=%s\n",  idx, ord.Faction);
        fprintf(fp, "COrder_%d_Score=%d\n",    idx, ord.WinScore);
        fprintf(fp, "COrder_%d_Games=%d\n",    idx, ord.GamesPlayed);
        if (ord.LastGameUID[0]) {
            fprintf(fp, "COrder_%d_LastUID=%s\n",  idx, ord.LastGameUID);
            fprintf(fp, "COrder_%d_LastSecs=%d\n", idx, ord.LastGameSecs);
        }
        fprintf(fp, "COrder_%d_Steps=%d\n",    idx, ord.StepCount);
        for (int s = 0; s < ord.StepCount; s++) {
            fprintf(fp, "COrder_%d_Step_%d=%s\n", idx, s, ord.Steps[s]);
        }
        idx++;
    }

    /*-- Limits records --*/
    fprintf(fp, "\n[BosoLimits]\n");
    active = 0;
    for (int i = 0; i < LimitsCount; i++) if (LimitsRecords[i].Active) active++;
    fprintf(fp, "Count=%d\n", active);
    idx = 0;
    for (int i = 0; i < LimitsCount; i++) {
        BosoLimitsRecord & r = LimitsRecords[i];
        if (!r.Active) continue;
        fprintf(fp, "Limits_%d_Faction=%s\n",     idx, r.Faction);
        fprintf(fp, "Limits_%d_Score=%d\n",        idx, r.WinScore);
        fprintf(fp, "Limits_%d_Games=%d\n",        idx, r.GamesPlayed);
        if (r.LastGameUID[0]) {
            fprintf(fp, "Limits_%d_LastUID=%s\n",  idx, r.LastGameUID);
            fprintf(fp, "Limits_%d_LastSecs=%d\n", idx, r.LastGameSecs);
        }
        fprintf(fp, "Limits_%d_Unit=%d\n",         idx, r.MaxUnit);
        fprintf(fp, "Limits_%d_Building=%d\n",     idx, r.MaxBuilding);
        fprintf(fp, "Limits_%d_Infantry=%d\n",     idx, r.MaxInfantry);
        fprintf(fp, "Limits_%d_Vessel=%d\n",       idx, r.MaxVessel);
        fprintf(fp, "Limits_%d_Aircraft=%d\n",     idx, r.MaxAircraft);
        fprintf(fp, "Limits_%d_Refinery=%d\n",     idx, r.MaxRefinery);
        fprintf(fp, "Limits_%d_Barracks=%d\n",     idx, r.MaxBarracks);
        fprintf(fp, "Limits_%d_WarFactory=%d\n",   idx, r.MaxWarFactory);
        fprintf(fp, "Limits_%d_Defense=%d\n",      idx, r.MaxDefense);
        fprintf(fp, "Limits_%d_AA=%d\n",           idx, r.MaxAA);
        fprintf(fp, "Limits_%d_Tesla=%d\n",        idx, r.MaxTesla);
        fprintf(fp, "Limits_%d_Helipad=%d\n",      idx, r.MaxHelipad);
        fprintf(fp, "Limits_%d_Airstrip=%d\n",     idx, r.MaxAirstrip);
        fprintf(fp, "Limits_%d_AttackSecs=%d\n",   idx, r.AttackSecs);
        fprintf(fp, "Limits_%d_BaseSizeAdd=%d\n",  idx, r.BaseSizeAdd);
        fprintf(fp, "Limits_%d_BaseSpacing=%d\n",  idx, r.BaseSpacing);
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
