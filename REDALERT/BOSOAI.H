//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

/*
** BOSOAI.H - Adaptive AI system for Red Alert.
**
** BosoAI takes over the human player's house and uses a runtime INI-based
** learning system to improve AI behaviour over multiple sessions via
** evolutionary mutation of [AI]-section rule parameters.
**
** Each AI house gets its own per-house copy of the tuned values; global
** Rule.* fields are never written.
**
** Activation: add BosoAI=yes (and optionally BosoAISpectator=yes) to the
** [Basic] section of any scenario or skirmish map, or set Enabled=yes in
** BOSOAI_CONFIG.INI.
*/

#ifndef BOSOAI_H
#define BOSOAI_H

/*
** Forward declarations.
*/
class HouseClass;

/*---------------------------------------------------------------------------
** Shared string lengths
---------------------------------------------------------------------------*/
#define BOSO_MAP_LEN            32
#define BOSO_FACTION_LEN        16
#define BOSO_UID_LEN            13   // 12 hex chars + null terminator

/*---------------------------------------------------------------------------
** Game history log
---------------------------------------------------------------------------*/
#define BOSO_MAX_GAME_HISTORY   128  // Max game entries stored in [BosoGameHistory]

/*---------------------------------------------------------------------------
** INI file names
---------------------------------------------------------------------------*/
#define BOSO_INI_FILE           "BOSOAI.INI"        // learning data (auto-generated)
#define BOSO_CONFIG_FILE        "BOSOAI_CONFIG.INI" // user settings (never overwritten)

/*---------------------------------------------------------------------------
** Unit / building limit evolution constants
---------------------------------------------------------------------------*/
#define BOSO_MAX_LIMITS_RECORDS 64

/*---------------------------------------------------------------------------
** Building ratio defaults (scaled by 256: 256 = 1.0)
** These mirror Rule.[Name]Ratio defaults from RULES.CPP.
---------------------------------------------------------------------------*/
#define BOSO_DEF_DEFENSE_RATIO    128   // 0.50
#define BOSO_DEF_AA_RATIO          36   // 0.14
#define BOSO_DEF_TESLA_RATIO       41   // 0.16 (Soviet)
#define BOSO_DEF_BARRACKS_RATIO    41   // 0.16
#define BOSO_DEF_WAR_RATIO         26   // 0.10
#define BOSO_DEF_HELIPAD_RATIO     31   // 0.12 (Allied)
#define BOSO_DEF_AIRSTRIP_RATIO    31   // 0.12 (Soviet)
#define BOSO_DEF_REFINERY_RATIO    41   // 0.16

/* Mutation range (±) for ratio values */
#define BOSO_MUT_RATIO             10
/* Hard min/max for all ratio values (0.0 to 2.0 range) */
#define BOSO_MIN_RATIO              0
#define BOSO_MAX_RATIO            512

/*---------------------------------------------------------------------------
** Building count limit defaults (mirror Rule.*Limit defaults)
---------------------------------------------------------------------------*/
#define BOSO_DEF_DEFENSE_LIMIT     40
#define BOSO_DEF_AA_LIMIT          10
#define BOSO_DEF_TESLA_LIMIT       10
#define BOSO_DEF_BARRACKS_LIMIT     2
#define BOSO_DEF_WAR_LIMIT          2
#define BOSO_DEF_HELIPAD_LIMIT      5
#define BOSO_DEF_AIRSTRIP_LIMIT     5
#define BOSO_DEF_REFINERY_LIMIT     4

/* Mutation deltas for limits */
#define BOSO_MUT_DEF_LIMIT          4
#define BOSO_MUT_AA_LIMIT           2
#define BOSO_MUT_TESLA_LIMIT        2
#define BOSO_MUT_PROD_LIMIT         1   // barracks, war, helipad, airstrip, refinery

/* Hard mins/maxes */
#define BOSO_MIN_DEF_LIMIT          2
#define BOSO_MAX_DEF_LIMIT         80
#define BOSO_MIN_AA_LIMIT           0
#define BOSO_MAX_AA_LIMIT          25
#define BOSO_MIN_TESLA_LIMIT        0
#define BOSO_MAX_TESLA_LIMIT       20
#define BOSO_MIN_BARRACKS_LIMIT     1
#define BOSO_MAX_BARRACKS_LIMIT     8
#define BOSO_MIN_WAR_LIMIT          1
#define BOSO_MAX_WAR_LIMIT          6
#define BOSO_MIN_HELIPAD_LIMIT      0
#define BOSO_MAX_HELIPAD_LIMIT     10
#define BOSO_MIN_AIRSTRIP_LIMIT     0
#define BOSO_MAX_AIRSTRIP_LIMIT    10
#define BOSO_MIN_REFINERY_LIMIT     1
#define BOSO_MAX_REFINERY_LIMIT     8

/*---------------------------------------------------------------------------
** Economy / base parameters
---------------------------------------------------------------------------*/
#define BOSO_DEF_POWER_SURPLUS     50
#define BOSO_MUT_POWER_SURPLUS     10
#define BOSO_MIN_POWER_SURPLUS      0
#define BOSO_MAX_POWER_SURPLUS    500

#define BOSO_DEF_INF_RESERVE     2000
#define BOSO_MUT_INF_RESERVE      150
#define BOSO_MIN_INF_RESERVE      200
#define BOSO_MAX_INF_RESERVE     8000

#define BOSO_DEF_INF_MULT           2
#define BOSO_MUT_INF_MULT           1
#define BOSO_MIN_INF_MULT           1
#define BOSO_MAX_INF_MULT           6

/* BaseSizeAdd: extra buildings beyond computed needed count.
** Vanilla default is 3 (very conservative); we use a larger default so
** BosoAI builds enough structures for a competitive base. */
#define BOSO_DEF_BASE_SIZE_ADD     20
#define BOSO_MUT_BASE_SIZE_ADD      5
#define BOSO_MIN_BASE_SIZE_ADD      3
#define BOSO_MAX_BASE_SIZE_ADD     80

/*---------------------------------------------------------------------------
** Attack timing (seconds)
---------------------------------------------------------------------------*/
#define BOSO_DEF_ATTACK_SECS      180   // 3 min (Rule.AttackInterval default = 3 min)
#define BOSO_MUT_ATTACK_SECS       15
#define BOSO_MIN_ATTACK_SECS       30
#define BOSO_MAX_ATTACK_SECS      600

#define BOSO_DEF_ATTACK_DELAY     300   // 5 min (Rule.AttackDelay default = 5 min)
#define BOSO_MUT_ATTACK_DELAY      30
#define BOSO_MIN_ATTACK_DELAY      30
#define BOSO_MAX_ATTACK_DELAY     600

#define BOSO_DEF_AUTOCREATE_SECS  300   // 5 min (Rule.AutocreateTime default = 5 min)
#define BOSO_MUT_AUTOCREATE_SECS   30
#define BOSO_MIN_AUTOCREATE_SECS   60
#define BOSO_MAX_AUTOCREATE_SECS  600

/* BaseSpacing: how many of 4 cardinal sides must have a clear corridor (2-4) */
#define BOSO_DEF_BASE_SPACING      3
#define BOSO_MUT_BASE_SPACING      1
#define BOSO_MIN_BASE_SPACING      0
#define BOSO_MAX_BASE_SPACING      4

/*---------------------------------------------------------------------------
** Per-house firepower bias (scaled by 256: 256 = 1.0 = no modification).
** Only FirepowerBias is evolved; the remaining bias fields (GroundspeedBias,
** ArmorBias, CostBias, BuildSpeedBias) are fixed at 1.0 and not mutated —
** they affect global game balance rather than AI behaviour.
---------------------------------------------------------------------------*/
#define BOSO_DEF_BIAS             256   // 1.0 — normal
#define BOSO_MUT_FIREPOWER         20
#define BOSO_MIN_FIREPOWER        128
#define BOSO_MAX_FIREPOWER        512

/*---------------------------------------------------------------------------
** Win/loss score deltas
---------------------------------------------------------------------------*/
#define BOSO_LIMITS_WIN           5
#define BOSO_LIMITS_LOSS          3

/*---------------------------------------------------------------------------
** Time bonus — extra score added to winning records based on game speed.
** Linear decay: full bonus at or below MIN_SECS, zero at or above CAP_SECS.
---------------------------------------------------------------------------*/
#define BOSO_TIME_BONUS_MAX       10   // Extra points for fastest possible win
#define BOSO_TIME_BONUS_MIN_SECS  300  // ≤ 5 min  → full bonus
#define BOSO_TIME_BONUS_CAP_SECS  1800 // ≥ 30 min → no bonus

/*---------------------------------------------------------------------------
** Shared evolution / exploration constants
---------------------------------------------------------------------------*/
#define BOSO_EXPLOIT_CHANCE     70      // % chance to follow best known record; rest → mutate

/*---------------------------------------------------------------------------
** Anti-air threat detection
---------------------------------------------------------------------------*/
#define BOSO_AA_LOSS_THRESHOLD  5       // Aircraft kills before forcing AA priority

/*---------------------------------------------------------------------------
** Stalemate detection — all-house economic deadlock.
---------------------------------------------------------------------------*/
#define BOSO_STALEMATE_SECS       60    // seconds of confirmed deadlock before restart
#define BOSO_STALEMATE_MONEY_CAP  500   // credits at/below which a house is "broke"

/*---------------------------------------------------------------------------
** BosoLimitsRecord - per-house evolved AI parameter set.
** All ratio values are stored as int scaled by 256 (256 = 1.0) for INI
** compatibility. Convert to fixed with fixed(val, 256) before use.
** Stored in [BosoLimits] section of BOSOAI.INI.
---------------------------------------------------------------------------*/
struct BosoLimitsRecord {
    /* Building ratio targets (int * 256, i.e. 256 = 1.0)
    ** Per-house equivalents of Rule.[Name]Ratio global values. */
    int  DefenseRatio;      // fraction of base that should be defenses
    int  AARatio;           // fraction of base that should be AA
    int  TeslaRatio;        // fraction of base that should be Tesla (Soviet)
    int  BarracksRatio;     // fraction of base for barracks/tents
    int  WarRatio;          // fraction of base for war factories
    int  HelipadRatio;      // fraction of base for helipads (Allied)
    int  AirstripRatio;     // fraction of base for airstrips (Soviet)
    int  RefineryRatio;     // fraction of base for refineries

    /* Building count limits — per-house equivalents of Rule.*Limit */
    int  DefenseLimit;
    int  AALimit;
    int  TeslaLimit;
    int  BarracksLimit;
    int  WarLimit;
    int  HelipadLimit;
    int  AirstripLimit;
    int  RefineryLimit;

    /* Economy / base parameters */
    int  PowerSurplus;      // target power surplus (Rule.PowerSurplus)
    int  InfantryReserve;   // credits reserved before training infantry (Rule.InfantryReserve)
    int  InfantryBaseMult;  // infantry count target relative to buildings (Rule.InfantryBaseMult)
    int  BaseSizeAdd;       // extra buildings beyond computed needed count (Rule.BaseSizeAdd)

    /* Attack timing (all in seconds) */
    int  AttackSecs;        // seconds between attack waves
    int  AttackDelaySecs;   // seconds before the first attack
    int  AutocreateSecs;    // seconds between autocreate team formation

    /* Base spacing: how many of the 4 cardinal sides (0-4) must have a 2-cell-wide clear corridor */
    int  BaseSpacing;

    /* Per-house combat bias (int * 256; 256 = 1.0 = no modification)
    ** Applied directly to HouseClass bias fields at Apply_Limits_To_House. */
    int  FirepowerBias;
    int  GroundspeedBias;
    int  ArmorBias;
    int  CostBias;
    int  BuildSpeedBias;

    /* Metadata */
    char Faction[BOSO_FACTION_LEN];
    int  WinScore;
    int  GamesPlayed;
    char LastGameUID[BOSO_UID_LEN]; // UID of the most recent game this record was scored in
    int  LastGameSecs;              // length of the most recent scored game (seconds)
    bool Active;
};

/*---------------------------------------------------------------------------
** BosoGameHistoryEntry - one entry per completed game.
** Stored in [BosoGameHistory] section of BOSOAI.INI.
---------------------------------------------------------------------------*/
struct BosoGameHistoryEntry {
    char UID[BOSO_UID_LEN];       // Unique game identifier (12 hex chars)
    int  GameSecs;                 // Game length in seconds
    char Outcome[4];               // "W" = win, "L" = loss, "S" = stalemate
    char Faction[BOSO_FACTION_LEN];
    char Map[BOSO_MAP_LEN];
};

/*---------------------------------------------------------------------------
** BosoAIManagerClass - static singleton manager.
---------------------------------------------------------------------------*/
class BosoAIManagerClass {
public:

    /*-- State flags (set at Load_Config / On_Scenario_Load) --*/
    static bool        IsBosoActive;       // BosoAI enabled for this session
    static bool        IsSpectatorMode;    // Reveal map and block human input
    static bool        IsMultiHouseAI;     // Extend BosoAI to all skirmish AI houses
    static bool        IsAutoRestart;      // Auto-restart match on game over (for automated training)
    static bool        IsCostZero;         // Set all house CostBias=0 and give large credits (no economy)
    static bool        IsTraining;         // Training=yes: diverse mutation; Training=no: exploit best known
    static HouseClass *BosoHouse;          // Primary managed house (= PlayerPtr at load time)

    /*-- Lifecycle --*/
    static void Init(void);
    static void Load_Config(void);
    static void On_Scenario_Load(void);
    static void On_Game_Over(bool player_won);

    /*-- Stalemate detection (called from HOUSE.CPP AI_Unit) --*/
    static void Check_Stalemate(void);

    /*-- Late registration for MultiHouseAI (called from HOUSE.CPP AI_Building) --*/
    static void Try_Register_House(HouseClass * house);

    /*-- PlayerPtr redirect: returns next alive BosoAI house when primary is defeated,
    **   or NULL if none. Called from DLLInterface.cpp after Set_Player_Context. --*/
    static HouseClass * Get_Next_Active_House(void);

    /*-- Returns true if at least one BosoAI house is still alive (not defeated). --*/
    static bool Any_Boso_House_Alive(void);

    /*-- File-based logger: appends msg to BOSOAI_debug.log in the DLL directory. --*/
    static void Log(const char * msg);

    /*-- Anti-air threat tracking (called from TECHNO.CPP Record_The_Kill) --*/
    static void On_Unit_Killed_By_Aircraft(void);
    static int  AircraftLossCount;

    /*-- Naval viability cache (set once per scenario load, per house slot) --*/
    static bool NavalViable[8];

    /*-- INI persistence --*/
    static void Load_Data(void);
    static void Save_Data(void);

    /*-- Helpers --*/
    static bool Is_Naval_Viable(HouseClass * house);
    static bool Is_Spectator_Input_Blocked(void);
    static bool Is_Boso_House(HouseClass const * h);

    /*-- Limits accessor (used by HOUSE.CPP AI_Building for per-house AI params) --*/
    static const BosoLimitsRecord & Get_House_Limits(HouseClass const * h);

private:

    /*-- Multi-house tracking (BosoHouse is always index 0) --*/
    static HouseClass * BosoHouses[8];
    static int          BosoHouseCount;

    /*-- Limit record persistence --*/
    static BosoLimitsRecord LimitsRecords[BOSO_MAX_LIMITS_RECORDS];
    static int              LimitsCount;
    /* Per-house limit selection (index into LimitsRecords[], -1 = none) */
    static int              FollowLimitsIdx[8];
    static BosoLimitsRecord HouseLimits[8];

    /*-- Game context --*/
    static char CurrentMap[BOSO_MAP_LEN];
    static char CurrentFaction[BOSO_FACTION_LEN];
    static char CurrentEnemyFaction[BOSO_FACTION_LEN];
    static bool GameOverFired;
    static long GameStartFrame;     // Frame at scenario load (for elapsed time calc)
    static int  GameTimeBonusScore; // Computed at game-over; added to winner scores
    static int  StalemateTicks;          // Consecutive seconds all houses have been deadlocked
    static int  HouseDeadlockTicks[8];   // Per-house consecutive seconds individually deadlocked
    static long StaleCheckFrame;         // Frame of last stalemate check (throttle: once per second)

    /*-- Current game UID and length (set at game start / game over) --*/
    static char CurrentGameUID[BOSO_UID_LEN];
    static int  CurrentGameSecs;

    /*-- Game history log --*/
    static BosoGameHistoryEntry GameHistory[BOSO_MAX_GAME_HISTORY];
    static int                  GameHistoryCount;

    /*-- Internal: context detection --*/
    static void Detect_Context(HouseClass * house);

    /*-- Internal: game history --*/
    static void Record_Game_History(const char * uid, int secs, const char * outcome,
                                     const char * faction, const char * map);

    /*-- Internal: limits evolution --*/
    static void Score_Session_Limits(int slot, bool won);
    static int  Select_Limits(const char * faction, BosoLimitsRecord & out);
    static BosoLimitsRecord * Alloc_Limits(void);
    static int  Mutate_Limits(int parent_idx, const char * faction);
    static void Apply_Limits_To_House(HouseClass * house, int slot);
    static int  House_Slot(HouseClass const * h);
};

/*---------------------------------------------------------------------------
** Global flag checked in CNC_Handle_Input to suppress orders in spectator mode.
** Lives in BOSOAI.CPP.
---------------------------------------------------------------------------*/
extern bool BosoSpectatorActive;

#endif // BOSOAI_H
