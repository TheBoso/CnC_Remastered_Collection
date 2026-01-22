// PROCMISSION.CPP
//
// Procedural Mission Runtime System for Red Alert
// Implementation of mission generation, TeamType pools, and AI Director
//

#include "function.h"
#include "procmission.h"
#include "procini.h"

extern void GlyphX_Debug_Print(const char *debug_text);


/*******************************************************************************
 * STATIC DATA
 ******************************************************************************/

static ProcMissionManager* s_ProcMissionInstance = NULL;
static ProcTeamPoolManager* s_ProcTeamPoolInstance = NULL;
static ProcMapPoolManager* s_ProcMapPoolInstance = NULL;
static ProcMissionMenuManager* s_ProcMissionMenuInstance = NULL;

ProcMissionManager& ProcMission = ProcMissionManager::Instance();
ProcTeamPoolManager& ProcTeamPools = ProcTeamPoolManager::Instance();
ProcMapPoolManager& ProcMapPool = ProcMapPoolManager::Instance();
ProcMissionMenuManager& ProcMissionMenu = ProcMissionMenuManager::Instance();

// Region name to type mapping
static const char* s_RegionNames[] = {
    "NONE", "Base_A", "Base_B", "Base_C", "Choke_1", "Choke_2",
    "Expansion_1", "Expansion_2", "Entry_North", "Entry_South",
    "Entry_East", "Entry_West", "Patrol_1", "Patrol_2",
    "Objective_1", "Objective_2", "Objective_3",
    "Outpost_1", "Outpost_2", "Neutral"
};

static const char* s_ThemeNames[] = {
    "NONE", "BASE_ASSAULT", "SURVIVAL", "SABOTAGE", "ESCORT", "CAPTURE"
};

static const char* s_PoolNames[] = {
    "SOVIET_ATTACK", "SOVIET_PATROL", "SOVIET_DEFENSE",
    "ALLIED_ATTACK", "ALLIED_PATROL", "ALLIED_DEFENSE", "SPECIAL"
};

static const char* s_ThreatNames[] = {
    "SCOUT", "LIGHT", "MEDIUM", "HEAVY", "OVERWHELMING"
};


/*******************************************************************************
 * CONSTRUCTOR IMPLEMENTATIONS
 ******************************************************************************/

ProcDifficultyModifiers::ProcDifficultyModifiers() :
    PlayerCreditMult(1), PlayerCostBias(1), PlayerArmorBias(1), PlayerFirepowerBias(1),
    EnemyCreditMult(1), EnemyTechLevelCap(10), EnemyArmorBias(1), EnemyFirepowerBias(1),
    EnemyIQLevel(2), TeamQuantityScale(1), SpawnRateScale(1),
    MaxConcurrentTeams(2), AntiRushPeriod(300)
{
}

ProcPlayerConfig::ProcPlayerConfig() :
    Region(PROC_REGION_BASE_A), TechLevel(5), Credits(10000),
    MaxUnits(100), MaxBuildings(50), MaxInfantry(100),
    DisabledBuildingCount(0), BuildingCount(0), UnitCount(0), InfantryCount(0)
{
    memset(DisabledBuildings, 0, sizeof(DisabledBuildings));
}

ProcEnemyBase::ProcEnemyBase() :
    House(HOUSE_USSR), Region(PROC_REGION_BASE_B), State(PROC_BASE_STATE_ACTIVE),
    TechLevel(5), Credits(10000), BuildingCount(0), DefenseCount(0),
    GuardUnitCount(0), GuardInfantryCount(0), BaseNodeCount(0), ActivationDelay(0)
{
    ID[0] = '\0';
}

ProcPoolTeam::ProcPoolTeam() :
    ThreatLevel(PROC_THREAT_LIGHT), House(HOUSE_USSR),
    MemberCount(0), MissionCount(0), RecruitPriority(7),
    IsRoundAbout(false), IsSuicide(false), UseGathering(true)
{
    Name[0] = '\0';
}

ProcDirectorRules::ProcDirectorRules() :
    AntiRushPeriod(300), InitialWaveDelay(50), MinSpawnInterval(120), MaxSpawnInterval(300),
    EscalationThreshold1(25), EscalationThreshold2(50), EscalationThreshold3(75),
    TurtleCheckTime(150), TurtleCreditThreshold(20000), TurtleResponseDelay(30),
    MaxConcurrentTeams(2), MaxTotalSpawns(0), SpawnEntryCount(0)
{
}

ProcMissionDef::ProcMissionDef() :
    Theme(PROC_THEME_NONE), PlayerFaction(HOUSE_GREECE), EnemyFaction(HOUSE_USSR),
    EnemyBaseCount(0), OutpostCount(0), SurvivalTime(0)
{
    MissionID[0] = '\0';
    TemplateMap[0] = '\0';

    // Default difficulty settings
    EasyDifficulty.PlayerCreditMult = fixed(125, 100);
    EasyDifficulty.PlayerCostBias = fixed(80, 100);
    EasyDifficulty.PlayerArmorBias = fixed(120, 100);
    EasyDifficulty.EnemyCreditMult = fixed(75, 100);
    EasyDifficulty.EnemyTechLevelCap = 6;
    EasyDifficulty.TeamQuantityScale = fixed(60, 100);
    EasyDifficulty.SpawnRateScale = fixed(150, 100);
    EasyDifficulty.MaxConcurrentTeams = 1;
    EasyDifficulty.AntiRushPeriod = 450;

    HardDifficulty.PlayerCreditMult = fixed(85, 100);
    HardDifficulty.PlayerCostBias = fixed(115, 100);
    HardDifficulty.PlayerArmorBias = fixed(90, 100);
    HardDifficulty.EnemyCreditMult = fixed(150, 100);
    HardDifficulty.EnemyTechLevelCap = 10;
    HardDifficulty.TeamQuantityScale = fixed(140, 100);
    HardDifficulty.SpawnRateScale = fixed(70, 100);
    HardDifficulty.MaxConcurrentTeams = 3;
    HardDifficulty.AntiRushPeriod = 180;
}


/*******************************************************************************
 * UTILITY FUNCTIONS
 ******************************************************************************/

int Proc_Region_To_Waypoint(ProcRegionType region)
{
    // All named regions (except NONE) map to waypoints
    // BASE_A=1 maps to waypoint 0 (A), BASE_B=2 maps to waypoint 1 (B), etc.
    // OUTPOST_1=17 maps to waypoint 16 (Q), OUTPOST_2=18 maps to 17 (R), NEUTRAL=19 maps to 18 (S)
    if (region >= PROC_REGION_BASE_A && region < PROC_REGION_COUNT) {
        return (int)region - 1;
    }
    return -1;
}

ProcRegionType Proc_Region_From_Name(const char* name)
{
    if (!name) return PROC_REGION_NONE;
    for (int i = 0; i < PROC_REGION_COUNT; i++) {
        if (stricmp(name, s_RegionNames[i]) == 0) {
            return (ProcRegionType)i;
        }
    }
    return PROC_REGION_NONE;
}

ProcMissionThemeType Proc_Theme_From_Name(const char* name)
{
    if (!name) return PROC_THEME_NONE;
    for (int i = 0; i < PROC_THEME_COUNT; i++) {
        if (stricmp(name, s_ThemeNames[i]) == 0) {
            return (ProcMissionThemeType)i;
        }
    }
    return PROC_THEME_NONE;
}

ProcTeamPoolType Proc_Pool_From_Name(const char* name)
{
    if (!name) return PROC_POOL_SOVIET_ATTACK;
    for (int i = 0; i < PROC_POOL_COUNT; i++) {
        if (stricmp(name, s_PoolNames[i]) == 0) {
            return (ProcTeamPoolType)i;
        }
    }
    return PROC_POOL_SOVIET_ATTACK;
}

ProcThreatLevel Proc_Threat_From_Name(const char* name)
{
    if (!name) return PROC_THREAT_LIGHT;
    for (int i = 0; i < PROC_THREAT_COUNT; i++) {
        if (stricmp(name, s_ThreatNames[i]) == 0) {
            return (ProcThreatLevel)i;
        }
    }
    return PROC_THREAT_LIGHT;
}


/*******************************************************************************
 * PROCTEAMPOOLMANAGER IMPLEMENTATION
 ******************************************************************************/

ProcTeamPoolManager& ProcTeamPoolManager::Instance()
{
    if (!s_ProcTeamPoolInstance) {
        s_ProcTeamPoolInstance = new ProcTeamPoolManager();
    }
    return *s_ProcTeamPoolInstance;
}

ProcTeamPoolManager::ProcTeamPoolManager() : IsInitialized(false)
{
}

ProcTeamPoolManager::~ProcTeamPoolManager()
{
}

void ProcTeamPoolManager::Initialize_Default_Pools()
{
    // Pools are now loaded from INI file - this just marks as ready
    IsInitialized = true;
}

// Team pool setup functions removed - pools are now loaded from INI files
// See PROCPOOLS.INI for team definitions

const ProcPoolTeam* ProcTeamPoolManager::Get_Random_Team(ProcTeamPoolType poolType,
    ProcThreatLevel minThreat, ProcThreatLevel maxThreat, HousesType house) const
{
    const ProcTeamPool& pool = Pools[poolType];

    // Build list of matching teams
    const ProcPoolTeam* candidates[PROC_MAX_POOL_TEAMS];
    int candidateCount = 0;

    for (int i = 0; i < pool.TeamCount; i++) {
        const ProcPoolTeam& team = pool.Teams[i];
        if (team.ThreatLevel >= minThreat && team.ThreatLevel <= maxThreat) {
            if (house == HOUSE_NONE || team.House == house || team.House == pool.DefaultHouse) {
                candidates[candidateCount++] = &team;
            }
        }
    }

    if (candidateCount == 0) return NULL;

    // Pick random candidate
    int index = Random_Pick(0, candidateCount - 1);
    return candidates[index];
}

const ProcPoolTeam* ProcTeamPoolManager::Get_Team_By_Name(const char* name) const
{
    if (!name) return NULL;

    for (int p = 0; p < PROC_POOL_COUNT; p++) {
        const ProcTeamPool& pool = Pools[p];
        for (int t = 0; t < pool.TeamCount; t++) {
            if (stricmp(pool.Teams[t].Name, name) == 0) {
                return &pool.Teams[t];
            }
        }
    }
    return NULL;
}

bool ProcTeamPoolManager::Add_Team_To_Pool(ProcTeamPoolType poolType, const ProcPoolTeam& team)
{
    ProcTeamPool& pool = Pools[poolType];
    if (pool.TeamCount >= PROC_MAX_POOL_TEAMS) return false;

    pool.Teams[pool.TeamCount++] = team;
    return true;
}

bool ProcTeamPoolManager::Load_Pools_From_INI(const char* filename)
{
    // Custom pool loading can be implemented here
    return true;
}


/*******************************************************************************
 * PROCMISSIONDIRECTOR IMPLEMENTATION
 ******************************************************************************/

ProcMissionDirector::ProcMissionDirector() :
    State(DIRECTOR_IDLE), Difficulty(DIFF_NORMAL), IsPaused(false),
    GameStartFrame(0), LastSpawnFrame(0), NextSpawnFrame(0), AntiRushEndFrame(0),
    ActiveTeamCount(0), TotalSpawns(0), CurrentThreatLevel(0), EscalationLevel(0),
    QuantityScale(1), SpawnRateScale(1), MaxConcurrentTeams(2)
{
}

ProcMissionDirector::~ProcMissionDirector()
{
}

void ProcMissionDirector::Reset()
{
    State = DIRECTOR_IDLE;
    IsPaused = false;
    GameStartFrame = 0;
    LastSpawnFrame = 0;
    NextSpawnFrame = 0;
    AntiRushEndFrame = 0;
    ActiveTeamCount = 0;
    TotalSpawns = 0;
    CurrentThreatLevel = 0;
    EscalationLevel = 0;
}

void ProcMissionDirector::Initialize(const ProcDirectorRules& rules, DiffType difficulty)
{
    Rules = rules;
    Difficulty = difficulty;

    // Get difficulty modifiers
    const ProcMissionDef* mission = ProcMission.Get_Current_Mission();
    if (mission) {
        const ProcDifficultyModifiers* mods;
        switch (difficulty) {
            case DIFF_EASY: mods = &mission->EasyDifficulty; break;
            case DIFF_HARD: mods = &mission->HardDifficulty; break;
            default: mods = &mission->NormalDifficulty; break;
        }
        QuantityScale = mods->TeamQuantityScale;
        SpawnRateScale = mods->SpawnRateScale;
        MaxConcurrentTeams = mods->MaxConcurrentTeams;
    }

    // Setup timing
    GameStartFrame = Frame;
    int antiRushTicks = (Rules.AntiRushPeriod * TICKS_PER_MINUTE) / 10;
    AntiRushEndFrame = GameStartFrame + antiRushTicks;

    // Calculate first spawn time
    int initialDelay = (Rules.InitialWaveDelay * TICKS_PER_MINUTE) / 10;
    initialDelay = (int)(initialDelay * SpawnRateScale);
    NextSpawnFrame = AntiRushEndFrame + initialDelay;

    State = DIRECTOR_ANTI_RUSH;
    CurrentThreatLevel = PROC_THREAT_LIGHT;
}

void ProcMissionDirector::AI()
{
    if (State == DIRECTOR_IDLE || IsPaused) return;

    // Debug output every 5 seconds (75 frames at 15fps)
    static int lastDebugFrame = 0;
    if (Frame - lastDebugFrame >= 75) {
        char debugBuf[128];
        sprintf(debugBuf, "ProcDirector: State=%d, Frame=%d, NextSpawn=%d, SpawnEntries=%d",
            (int)State, Frame, NextSpawnFrame, Rules.SpawnEntryCount);
        GlyphX_Debug_Print(debugBuf);
        lastDebugFrame = Frame;
    }

    // State machine
    switch (State) {
        case DIRECTOR_ANTI_RUSH:
            if (Frame >= AntiRushEndFrame) {
                State = DIRECTOR_ACTIVE;
                GlyphX_Debug_Print("ProcDirector: Anti-rush period ended, now ACTIVE");
            }
            break;

        case DIRECTOR_ACTIVE:
        case DIRECTOR_ESCALATED:
        case DIRECTOR_ENDGAME:
            Update_Escalation();
            Check_Turtle_Detection();
            Try_Spawn_Team();
            break;

        default:
            break;
    }
}

void ProcMissionDirector::Update_Escalation()
{
    int destruction = Get_Enemy_Destruction_Percentage();
    int newLevel = 0;

    if (destruction >= Rules.EscalationThreshold3) {
        newLevel = 3;
        State = DIRECTOR_ENDGAME;
        CurrentThreatLevel = PROC_THREAT_OVERWHELMING;
    } else if (destruction >= Rules.EscalationThreshold2) {
        newLevel = 2;
        State = DIRECTOR_ESCALATED;
        CurrentThreatLevel = PROC_THREAT_HEAVY;
    } else if (destruction >= Rules.EscalationThreshold1) {
        newLevel = 1;
        State = DIRECTOR_ESCALATED;
        CurrentThreatLevel = PROC_THREAT_MEDIUM;
    }

    EscalationLevel = newLevel;
}

void ProcMissionDirector::Check_Turtle_Detection()
{
    // Check if player is turtling (high credits, not attacking)
    int turtleCheckFrame = GameStartFrame + (Rules.TurtleCheckTime * TICKS_PER_MINUTE) / 10;
    if (Frame < turtleCheckFrame) return;

    HouseClass* player = PlayerPtr;
    if (player && player->Credits >= Rules.TurtleCreditThreshold) {
        // Player is turtling - increase threat
        if (CurrentThreatLevel < PROC_THREAT_HEAVY) {
            CurrentThreatLevel = PROC_THREAT_HEAVY;
            EscalationLevel = max(EscalationLevel, 2);
        }
    }
}

void ProcMissionDirector::Try_Spawn_Team()
{
    if (Frame < NextSpawnFrame) return;

    char debugBuf[128];

    if (!Can_Spawn_Team()) {
        sprintf(debugBuf, "ProcDirector: Can't spawn - ActiveTeams=%d, MaxAllowed=%d",
            ActiveTeamCount, MaxConcurrentTeams);
        GlyphX_Debug_Print(debugBuf);
        return;
    }

    if (Rules.SpawnEntryCount == 0) {
        GlyphX_Debug_Print("ProcDirector: No spawn entries configured!");
        return;
    }

    // Pick a random spawn entry
    int entryIndex = Random_Pick(0, Rules.SpawnEntryCount - 1);
    sprintf(debugBuf, "ProcDirector: Attempting spawn from entry %d, pool=%d",
        entryIndex, (int)Rules.SpawnEntries[entryIndex].PoolType);
    GlyphX_Debug_Print(debugBuf);

    Select_And_Spawn(Rules.SpawnEntries[entryIndex]);

    // Calculate next spawn time
    LastSpawnFrame = Frame;
    NextSpawnFrame = Frame + Calculate_Next_Spawn_Delay();
}

void ProcMissionDirector::Select_And_Spawn(const ProcSpawnEntry& entry)
{
    char debugBuf[128];

    // Adjust threat based on escalation
    ProcThreatLevel minThreat = entry.MinThreat;
    ProcThreatLevel maxThreat = entry.MaxThreat;

    // Escalation increases minimum threat
    if (EscalationLevel >= 1 && minThreat < PROC_THREAT_MEDIUM) {
        minThreat = PROC_THREAT_MEDIUM;
    }
    if (EscalationLevel >= 2 && minThreat < PROC_THREAT_HEAVY) {
        minThreat = PROC_THREAT_HEAVY;
    }

    // Clamp to current threat level
    if ((int)maxThreat > CurrentThreatLevel) {
        maxThreat = (ProcThreatLevel)CurrentThreatLevel;
    }
    if (minThreat > maxThreat) {
        minThreat = maxThreat;
    }

    // Get team from pool
    const ProcMissionDef* mission = ProcMission.Get_Current_Mission();
    HousesType house = mission ? mission->EnemyFaction : HOUSE_USSR;

    sprintf(debugBuf, "ProcDirector: Looking for team in pool %d, threat %d-%d, house %d",
        (int)entry.PoolType, (int)minThreat, (int)maxThreat, (int)house);
    GlyphX_Debug_Print(debugBuf);

    const ProcPoolTeam* poolTeam = ProcTeamPools.Get_Random_Team(
        entry.PoolType, minThreat, maxThreat, house);

    if (!poolTeam) {
        GlyphX_Debug_Print("ProcDirector: No team found in pool!");
        return;
    }

    sprintf(debugBuf, "ProcDirector: Found team '%s' with %d members",
        poolTeam->Name, poolTeam->MemberCount);
    GlyphX_Debug_Print(debugBuf);

    // Create the team
    TeamTypeClass* teamType = Create_Team_From_Pool(entry.PoolType, poolTeam->ThreatLevel, house, entry.SpawnRegion);
    if (!teamType) {
        GlyphX_Debug_Print("ProcDirector: Failed to create TeamTypeClass!");
        return;
    }

    // Create spawn trigger
    TriggerTypeClass* trigType = Create_Spawn_Trigger(teamType);
    if (trigType) {
        TriggerClass* trigger = Find_Or_Make(trigType);
        if (trigger) {
            TotalSpawns++;
            ActiveTeamCount++;
            sprintf(debugBuf, "ProcDirector: Spawned team! Total=%d, Active=%d",
                TotalSpawns, ActiveTeamCount);
            GlyphX_Debug_Print(debugBuf);
        }
    }
}

TeamTypeClass* ProcMissionDirector::Create_Team_From_Pool(ProcTeamPoolType pool,
    ProcThreatLevel threat, HousesType house, ProcRegionType spawnRegion)
{
    const ProcPoolTeam* poolTeam = ProcTeamPools.Get_Random_Team(pool, threat, threat, house);
    if (!poolTeam) return NULL;

    TeamTypeClass* teamType = new TeamTypeClass();
    if (!teamType) return NULL;

    // Generate unique name
    static int teamCounter = 0;
    char name[9];
    sprintf(name, "PT%04d", teamCounter++ % 10000);
    strncpy((char*)teamType->IniName, name, sizeof(teamType->IniName) - 1);

    teamType->House = house;
    teamType->IsActive = true;
    teamType->IsRoundAbout = poolTeam->IsRoundAbout;
    teamType->IsSuicide = poolTeam->IsSuicide;
    teamType->IsAutocreate = true;   // AI can auto-create this team
    teamType->IsPrebuilt = false;    // AI builds from factories
    teamType->IsReinforcable = false;
    teamType->RecruitPriority = poolTeam->RecruitPriority;
    teamType->InitNum = 1;
    teamType->MaxAllowed = 2;        // Allow a couple of these at once

    // Fill composition with quantity scaling
    teamType->ClassCount = min(poolTeam->MemberCount, (int)TeamTypeClass::MAX_TEAM_CLASSCOUNT);
    for (int i = 0; i < teamType->ClassCount; i++) {
        const ProcPoolMember& member = poolTeam->Members[i];
        TechnoTypeClass const* technoType = NULL;

        switch (member.RTTI) {
            case RTTI_UNITTYPE:
                technoType = &UnitTypeClass::As_Reference((UnitType)member.TypeIndex);
                break;
            case RTTI_INFANTRYTYPE:
                technoType = &InfantryTypeClass::As_Reference((InfantryType)member.TypeIndex);
                break;
            case RTTI_AIRCRAFTTYPE:
                technoType = &AircraftTypeClass::As_Reference((AircraftType)member.TypeIndex);
                break;
            case RTTI_VESSELTYPE:
                technoType = &VesselTypeClass::As_Reference((VesselType)member.TypeIndex);
                break;
            default:
                continue;
        }

        teamType->Members[i].Class = technoType;
        int scaledQty = (int)(member.Quantity * QuantityScale);
        teamType->Members[i].Quantity = max(1, scaledQty);
    }

    // Fill missions - optionally with gathering phases first if UseGathering is enabled
    // 1. Rally at safe cell near own base (if gathering)
    // 2. Move to staging cell near enemy base (if gathering)
    // 3. Original attack missions from pool

    int missionIndex = 0;
    int gatherMissions = 0;

    // Only add gathering missions if the team has UseGathering enabled
    if (poolTeam->UseGathering) {
        // Find enemy base location for staging point calculation
        CELL enemyBaseCell = ProcMission.Get_Region_Cell(PROC_REGION_BASE_A);  // Player base is enemy from AI perspective
        CELL ownBaseCell = ProcMission.Get_Region_Cell(PROC_REGION_BASE_B);    // AI base

        // Fallback if waypoints not set
        if (ownBaseCell == -1) {
            // Try to find own buildings
            HouseClass* housePtr = HouseClass::As_Pointer(house);
            if (housePtr) {
                for (int i = 0; i < Buildings.Count() && ownBaseCell == -1; i++) {
                    BuildingClass* bld = Buildings.Ptr(i);
                    if (bld && bld->House == housePtr) {
                        ownBaseCell = Coord_Cell(bld->Center_Coord());
                    }
                }
            }
        }

        if (enemyBaseCell == -1) {
            // Try to find player buildings
            if (PlayerPtr) {
                for (int i = 0; i < Buildings.Count() && enemyBaseCell == -1; i++) {
                    BuildingClass* bld = Buildings.Ptr(i);
                    if (bld && bld->House == PlayerPtr) {
                        enemyBaseCell = Coord_Cell(bld->Center_Coord());
                    }
                }
            }
        }

        // Mission 1: Rally at safe cell near own base (if we found our base)
        if (ownBaseCell != -1 && missionIndex < TeamTypeClass::MAX_TEAM_MISSIONS - poolTeam->MissionCount) {
            CELL rallyCell = Proc_Find_Safe_Cell_Near(ownBaseCell, house, 10);
            if (rallyCell != -1) {
                teamType->MissionList[missionIndex].Mission = TMISSION_MOVECELL;
                teamType->MissionList[missionIndex].Data.Value = rallyCell;
                missionIndex++;
                gatherMissions++;
            }
        }

        // Mission 2: Move to staging cell near enemy base (if we found enemy base)
        if (enemyBaseCell != -1 && missionIndex < TeamTypeClass::MAX_TEAM_MISSIONS - poolTeam->MissionCount) {
            CELL stagingCell = Proc_Find_Staging_Cell(enemyBaseCell, house, 15);
            if (stagingCell != -1) {
                teamType->MissionList[missionIndex].Mission = TMISSION_MOVECELL;
                teamType->MissionList[missionIndex].Data.Value = stagingCell;
                missionIndex++;
                gatherMissions++;
            }
        }
    }

    // Add original attack missions from pool
    int poolMissionsToAdd = min(poolTeam->MissionCount, (int)TeamTypeClass::MAX_TEAM_MISSIONS - missionIndex);
    for (int i = 0; i < poolMissionsToAdd; i++) {
        teamType->MissionList[missionIndex].Mission = poolTeam->Missions[i].Mission;
        teamType->MissionList[missionIndex].Data.Value = poolTeam->Missions[i].Data;
        missionIndex++;
    }

    teamType->MissionCount = missionIndex;

    char debugBuf[128];
    sprintf(debugBuf, "ProcDirector: Team %s has %d missions (%d gather + %d attack)",
        name, missionIndex, gatherMissions, poolMissionsToAdd);
    GlyphX_Debug_Print(debugBuf);

    return teamType;
}

TriggerTypeClass* ProcMissionDirector::Create_Spawn_Trigger(TeamTypeClass* team)
{
    if (!team) return NULL;

    TriggerTypeClass* trigType = new TriggerTypeClass();
    if (!trigType) return NULL;

    // Generate unique name
    static int trigCounter = 0;
    char name[5];
    sprintf(name, "D%03d", trigCounter++ % 1000);
    strncpy((char*)trigType->IniName, name, sizeof(trigType->IniName) - 1);

    trigType->House = team->House;
    trigType->IsActive = true;
    trigType->IsPersistant = TriggerTypeClass::VOLATILE;

    // Immediate execution trigger
    trigType->Event1.Event = TEVENT_TIME;
    trigType->Event1.Data.Value = 0;
    trigType->EventControl = MULTI_ONLY;

    // Use CREATE_TEAM - AI will build this team from its factories
    trigType->Action1.Action = TACTION_CREATE_TEAM;
    trigType->Action1.Team = team;
    trigType->ActionControl = MULTI_ONLY;

    return trigType;
}

int ProcMissionDirector::Calculate_Next_Spawn_Delay()
{
    int minDelay = (Rules.MinSpawnInterval * TICKS_PER_MINUTE) / 10;
    int maxDelay = (Rules.MaxSpawnInterval * TICKS_PER_MINUTE) / 10;

    // Scale by difficulty
    minDelay = (int)(minDelay * SpawnRateScale);
    maxDelay = (int)(maxDelay * SpawnRateScale);

    // Faster spawns at higher escalation
    if (EscalationLevel >= 2) {
        minDelay = minDelay * 7 / 10;
        maxDelay = maxDelay * 7 / 10;
    } else if (EscalationLevel >= 1) {
        minDelay = minDelay * 85 / 100;
        maxDelay = maxDelay * 85 / 100;
    }

    return Random_Pick(minDelay, maxDelay);
}

int ProcMissionDirector::Get_Enemy_Destruction_Percentage()
{
    const ProcMissionDef* mission = ProcMission.Get_Current_Mission();
    if (!mission) return 0;

    HouseClass* enemy = HouseClass::As_Pointer(mission->EnemyFaction);
    if (!enemy) return 0;

    // Calculate based on buildings destroyed vs initial
    int initial = enemy->Control.MaxBuilding;
    if (initial <= 0) initial = 1;

    int destroyed = initial - enemy->CurBuildings;
    if (destroyed < 0) destroyed = 0;

    return (destroyed * 100) / initial;
}

bool ProcMissionDirector::Can_Spawn_Team()
{
    if (ActiveTeamCount >= MaxConcurrentTeams) return false;
    if (Rules.MaxTotalSpawns > 0 && TotalSpawns >= Rules.MaxTotalSpawns) return false;
    return true;
}

void ProcMissionDirector::Force_Spawn(ProcTeamPoolType pool, ProcThreatLevel threat, ProcRegionType region)
{
    ProcSpawnEntry entry;
    entry.PoolType = pool;
    entry.SpawnRegion = region;
    entry.MinThreat = threat;
    entry.MaxThreat = threat;
    entry.Quarry = QUARRY_ANYTHING;

    Select_And_Spawn(entry);
}

void ProcMissionDirector::Set_Escalation_Level(int level)
{
    EscalationLevel = max(0, min(3, level));

    if (EscalationLevel >= 3) {
        State = DIRECTOR_ENDGAME;
        CurrentThreatLevel = PROC_THREAT_OVERWHELMING;
    } else if (EscalationLevel >= 1) {
        State = DIRECTOR_ESCALATED;
        CurrentThreatLevel = (ProcThreatLevel)(PROC_THREAT_MEDIUM + EscalationLevel - 1);
    }
}


/*******************************************************************************
 * PROCMISSIONMANAGER IMPLEMENTATION
 ******************************************************************************/

ProcMissionManager& ProcMissionManager::Instance()
{
    if (!s_ProcMissionInstance) {
        s_ProcMissionInstance = new ProcMissionManager();
    }
    return *s_ProcMissionInstance;
}

ProcMissionManager::ProcMissionManager() :
    IsMissionLoaded(false), CurrentDifficulty(DIFF_NORMAL), RegionCellsValid(false)
{
    memset(RegionCells, 0, sizeof(RegionCells));
}

ProcMissionManager::~ProcMissionManager()
{
}

void ProcMissionManager::Reset()
{
    IsMissionLoaded = false;
    RegionCellsValid = false;
    memset(RegionCells, 0, sizeof(RegionCells));
    CurrentMission = ProcMissionDef();
    Director.Reset();
}

bool ProcMissionManager::Load_Mission_Definition(const char* filename)
{
    GlyphX_Debug_Print("ProcMission: Load_Mission_Definition called");
    if (!filename || !*filename) {
        GlyphX_Debug_Print("ProcMission: filename is null or empty");
        return false;
    }
    GlyphX_Debug_Print(filename);

    ProcMissionDef mission;
    if (!ProcMissionINI.Load(filename, mission)) {
        GlyphX_Debug_Print("ProcMission: ProcMissionINI.Load() FAILED");
        return false;
    }

    GlyphX_Debug_Print("ProcMission: INI loaded successfully");
    return Load_Mission_From_Memory(mission);
}

bool ProcMissionManager::Load_Mission_From_Memory(const ProcMissionDef& mission)
{
    CurrentMission = mission;
    IsMissionLoaded = true;
    RegionCellsValid = false;
    return true;
}

bool ProcMissionManager::Auto_Generate_And_Inject(DiffType difficulty)
{
    GlyphX_Debug_Print("ProcMission: Auto_Generate_And_Inject called");
    char debugBuf[128];

    // Reset any previous state
    Reset();
    CurrentDifficulty = difficulty;

    // Step 1: Randomly select a theme
    int themeRoll = Random_Pick(0, 100);
    if (themeRoll < 50) {
        CurrentMission.Theme = PROC_THEME_BASE_ASSAULT;
        GlyphX_Debug_Print("ProcMission: Selected theme: BASE_ASSAULT");
    } else {
        CurrentMission.Theme = PROC_THEME_SURVIVAL;
        // Random survival time: 3-10 minutes
        CurrentMission.SurvivalTime = 20 * 60;  // 20 minutes
        sprintf(debugBuf, "ProcMission: Selected theme: SURVIVAL (%d seconds)", CurrentMission.SurvivalTime);
        GlyphX_Debug_Print(debugBuf);
    }

    // Step 2: Detect player faction from PlayerPtr
    if (PlayerPtr) {
        CurrentMission.PlayerFaction = PlayerPtr->Class->House;
        sprintf(debugBuf, "ProcMission: Player faction = %d", (int)CurrentMission.PlayerFaction);
        GlyphX_Debug_Print(debugBuf);
    } else {
        CurrentMission.PlayerFaction = HOUSE_GREECE;
        GlyphX_Debug_Print("ProcMission: No PlayerPtr, defaulting to Greece");
    }

    // Step 3: Set enemy faction (opposite side)
    // Allied houses: HOUSE_SPAIN, HOUSE_GREECE, HOUSE_ENGLAND, HOUSE_GERMANY, HOUSE_FRANCE, HOUSE_TURKEY
    // Soviet houses: HOUSE_USSR, HOUSE_UKRAINE
    bool playerIsAllied = (CurrentMission.PlayerFaction == HOUSE_GREECE ||
                           CurrentMission.PlayerFaction == HOUSE_SPAIN ||
                           CurrentMission.PlayerFaction == HOUSE_ENGLAND ||
                           CurrentMission.PlayerFaction == HOUSE_GERMANY ||
                           CurrentMission.PlayerFaction == HOUSE_FRANCE ||
                           CurrentMission.PlayerFaction == HOUSE_TURKEY);

    if (playerIsAllied) {
        CurrentMission.EnemyFaction = HOUSE_USSR;
    } else {
        CurrentMission.EnemyFaction = HOUSE_GREECE;
    }
    sprintf(debugBuf, "ProcMission: Enemy faction = %d", (int)CurrentMission.EnemyFaction);
    GlyphX_Debug_Print(debugBuf);

    // Step 4: Setup default difficulty modifiers
    Setup_Default_Difficulty_Modifiers();

    // Step 5: Setup default director rules
    Setup_Default_Director_Rules();

    // Mark as loaded
    IsMissionLoaded = true;

    // Step 6: Initialize team pools from master config if available
    ProcTeamPools.Initialize_Default_Pools();
    ProcMissionINI.Load_Master_Config(PROC_MASTER_INI);

    // Step 7: Cache region cells (for spawning)
    RegionCellsValid = true;
    for (int i = 0; i < PROC_REGION_COUNT; i++) {
        RegionCells[i] = Get_Region_Cell((ProcRegionType)i);
    }

    // Step 8: Spawn some starting units for the player (if they don't have much)
    Auto_Spawn_Player_Units();

    // Step 9: Setup alliances between enemy houses (before spawning)
    Setup_Enemy_Alliances();

    // Step 10: Spawn main enemy base
    Auto_Spawn_Enemy_Base();

    // Step 11: Spawn outpost bases (under different houses, allied with main enemy)
    Auto_Spawn_Outpost_Bases();

    // Step 12: Spawn neutral base (if enabled)
    Auto_Spawn_Neutral_Base();

    // Step 13: Setup victory/defeat triggers based on theme
    Setup_Victory_Defeat_Triggers();

    // Step 14: Initialize the director for enemy waves
    Director.Initialize(CurrentMission.DirectorRules, difficulty);

    GlyphX_Debug_Print("ProcMission: Auto-generation complete!");
    return true;
}

void ProcMissionManager::Setup_Default_Difficulty_Modifiers()
{
    // Easy
    CurrentMission.EasyDifficulty.PlayerCreditMult = fixed(125, 100);
    CurrentMission.EasyDifficulty.PlayerArmorBias = fixed(120, 100);
    CurrentMission.EasyDifficulty.PlayerFirepowerBias = fixed(110, 100);
    CurrentMission.EasyDifficulty.EnemyCreditMult = fixed(75, 100);
    CurrentMission.EasyDifficulty.EnemyTechLevelCap = 6;
    CurrentMission.EasyDifficulty.EnemyArmorBias = fixed(85, 100);
    CurrentMission.EasyDifficulty.EnemyFirepowerBias = fixed(90, 100);
    CurrentMission.EasyDifficulty.TeamQuantityScale = fixed(60, 100);
    CurrentMission.EasyDifficulty.SpawnRateScale = fixed(150, 100);
    CurrentMission.EasyDifficulty.MaxConcurrentTeams = 1;

    // Normal
    CurrentMission.NormalDifficulty.PlayerCreditMult = fixed(100, 100);
    CurrentMission.NormalDifficulty.PlayerArmorBias = fixed(100, 100);
    CurrentMission.NormalDifficulty.PlayerFirepowerBias = fixed(100, 100);
    CurrentMission.NormalDifficulty.EnemyCreditMult = fixed(100, 100);
    CurrentMission.NormalDifficulty.EnemyTechLevelCap = 8;
    CurrentMission.NormalDifficulty.EnemyArmorBias = fixed(100, 100);
    CurrentMission.NormalDifficulty.EnemyFirepowerBias = fixed(100, 100);
    CurrentMission.NormalDifficulty.TeamQuantityScale = fixed(100, 100);
    CurrentMission.NormalDifficulty.SpawnRateScale = fixed(100, 100);
    CurrentMission.NormalDifficulty.MaxConcurrentTeams = 2;

    // Hard
    CurrentMission.HardDifficulty.PlayerCreditMult = fixed(85, 100);
    CurrentMission.HardDifficulty.PlayerArmorBias = fixed(90, 100);
    CurrentMission.HardDifficulty.PlayerFirepowerBias = fixed(100, 100);
    CurrentMission.HardDifficulty.EnemyCreditMult = fixed(150, 100);
    CurrentMission.HardDifficulty.EnemyTechLevelCap = 10;
    CurrentMission.HardDifficulty.EnemyArmorBias = fixed(115, 100);
    CurrentMission.HardDifficulty.EnemyFirepowerBias = fixed(110, 100);
    CurrentMission.HardDifficulty.TeamQuantityScale = fixed(140, 100);
    CurrentMission.HardDifficulty.SpawnRateScale = fixed(70, 100);
    CurrentMission.HardDifficulty.MaxConcurrentTeams = 3;
}

void ProcMissionManager::Setup_Default_Director_Rules()
{
    ProcDirectorRules& rules = CurrentMission.DirectorRules;

    // Timing (in game ticks, 15 ticks = 1 second)
    rules.AntiRushPeriod = 60;           // 60 seconds before attacks start
    rules.InitialWaveDelay = 45;          // 45 seconds to first wave after anti-rush
    rules.MinSpawnInterval = 90;          // Minimum 90 seconds between spawns
    rules.MaxSpawnInterval = 180;         // Maximum 180 seconds between spawns

    // Escalation thresholds (game time in seconds when escalation occurs)
    rules.EscalationThreshold1 = 180;     // 3 minutes
    rules.EscalationThreshold2 = 360;     // 6 minutes
    rules.EscalationThreshold3 = 540;     // 9 minutes

    // Turtle detection (disabled for now)
    rules.TurtleCheckTime = 9999;
    rules.TurtleCreditThreshold = 99999;
    rules.TurtleResponseDelay = 60;

    // Spawn limits
    rules.MaxConcurrentTeams = 2;
    rules.MaxTotalSpawns = 0;  // 0 = unlimited

    // Determine the correct pool based on enemy faction
    bool enemyIsSoviet = (CurrentMission.EnemyFaction == HOUSE_USSR ||
                          CurrentMission.EnemyFaction == HOUSE_UKRAINE);
    ProcTeamPoolType attackPool = enemyIsSoviet ? PROC_POOL_SOVIET_ATTACK : PROC_POOL_ALLIED_ATTACK;

    // Setup default spawn entries
    rules.SpawnEntryCount = 4;

    // Entry 1: North attacks
    rules.SpawnEntries[0].SpawnRegion = PROC_REGION_ENTRY_NORTH;
    rules.SpawnEntries[0].PoolType = attackPool;
    rules.SpawnEntries[0].MinThreat = PROC_THREAT_SCOUT;
    rules.SpawnEntries[0].MaxThreat = PROC_THREAT_HEAVY;
    rules.SpawnEntries[0].Quarry = QUARRY_ANYTHING;

    // Entry 2: East attacks
    rules.SpawnEntries[1].SpawnRegion = PROC_REGION_ENTRY_EAST;
    rules.SpawnEntries[1].PoolType = attackPool;
    rules.SpawnEntries[1].MinThreat = PROC_THREAT_SCOUT;
    rules.SpawnEntries[1].MaxThreat = PROC_THREAT_MEDIUM;
    rules.SpawnEntries[1].Quarry = QUARRY_ANYTHING;

    // Entry 3: South attacks
    rules.SpawnEntries[2].SpawnRegion = PROC_REGION_ENTRY_SOUTH;
    rules.SpawnEntries[2].PoolType = attackPool;
    rules.SpawnEntries[2].MinThreat = PROC_THREAT_LIGHT;
    rules.SpawnEntries[2].MaxThreat = PROC_THREAT_HEAVY;
    rules.SpawnEntries[2].Quarry = QUARRY_BUILDINGS;

    // Entry 4: West attacks
    rules.SpawnEntries[3].SpawnRegion = PROC_REGION_ENTRY_WEST;
    rules.SpawnEntries[3].PoolType = attackPool;
    rules.SpawnEntries[3].MinThreat = PROC_THREAT_SCOUT;
    rules.SpawnEntries[3].MaxThreat = PROC_THREAT_MEDIUM;
    rules.SpawnEntries[3].Quarry = QUARRY_INFANTRY;
}

void ProcMissionManager::Auto_Spawn_Player_Units()
{
    GlyphX_Debug_Print("ProcMission: Auto-spawning player assets...");
    char debugBuf[128];

    // Must have a valid player
    if (!PlayerPtr) {
        GlyphX_Debug_Print("ProcMission: No PlayerPtr, cannot spawn units!");
        return;
    }

    // Use the actual player's house type for spawning
    HousesType playerHouse = PlayerPtr->Class->House;
    sprintf(debugBuf, "ProcMission: PlayerPtr house = %d, Theme = %d",
        (int)playerHouse, (int)CurrentMission.Theme);
    GlyphX_Debug_Print(debugBuf);

    // Find a valid spawn cell for the player
    CELL baseCell = Get_Region_Cell(PROC_REGION_BASE_A);
    if (baseCell == -1) {
        baseCell = Find_Fallback_Spawn_Cell(playerHouse);
    }
    if (baseCell == -1) {
        GlyphX_Debug_Print("ProcMission: Could not find spawn cell for player!");
        return;
    }

    sprintf(debugBuf, "ProcMission: Player base cell = %d", (int)baseCell);
    GlyphX_Debug_Print(debugBuf);

    // Determine what to spawn based on player faction
    bool playerIsAllied = (playerHouse == HOUSE_GREECE ||
                           playerHouse == HOUSE_SPAIN ||
                           playerHouse == HOUSE_ENGLAND ||
                           playerHouse == HOUSE_GERMANY ||
                           playerHouse == HOUSE_FRANCE ||
                           playerHouse == HOUSE_TURKEY);

    // Give player starting credits (TechLevel already set by scenario INI)
    PlayerPtr->Credits = 5000;
    PlayerPtr->Capacity = 50000;

    // Different setups based on mission theme
    if (CurrentMission.Theme == PROC_THEME_SURVIVAL) {
        // SURVIVAL: Player gets a pre-built base to defend with RANDOM placement
        GlyphX_Debug_Print("ProcMission: SURVIVAL mode - spawning player base with RANDOM placement");

        // Construction Yard (anchor at center)
        Spawn_Building(playerHouse, STRUCT_CONST, baseCell);

        // Power plants - random placement
        Spawn_Building_Random(playerHouse, STRUCT_POWER, baseCell, 3, 8);
        Spawn_Building_Random(playerHouse, STRUCT_POWER, baseCell, 3, 10);

        // Refinery - random placement
        Spawn_Building_Random(playerHouse, STRUCT_REFINERY, baseCell, 4, 10);

        // War Factory - random placement
        Spawn_Building_Random(playerHouse, STRUCT_WEAP, baseCell, 4, 12);

        // Barracks - random placement
        StructType barracksType = playerIsAllied ? STRUCT_BARRACKS : STRUCT_TENT;
        Spawn_Building_Random(playerHouse, barracksType, baseCell, 3, 10);

        // Defensive structures with random placement (perimeter)
        StructType defenseType1 = playerIsAllied ? STRUCT_PILLBOX : STRUCT_FLAME_TURRET;
        StructType defenseType2 = playerIsAllied ? STRUCT_TURRET : STRUCT_TESLA;

        Spawn_Building_Random(playerHouse, defenseType1, baseCell, 8, 16);
        Spawn_Building_Random(playerHouse, defenseType1, baseCell, 8, 16);
        Spawn_Building_Random(playerHouse, defenseType2, baseCell, 8, 16);
        Spawn_Building_Random(playerHouse, defenseType2, baseCell, 8, 16);

        // Harvester at random position near base
        CELL harvCell = Find_Nearby_Valid_Cell(baseCell, 6);
        if (harvCell == -1) harvCell = baseCell;
        Spawn_Unit(playerHouse, UNIT_HARVESTER, harvCell, MISSION_HARVEST);

        // Starting defense units at random positions
        for (int i = 0; i < 3; i++) {
            CELL tankCell = Find_Nearby_Valid_Cell(baseCell, 8);
            if (tankCell == -1) tankCell = baseCell;

            if (playerIsAllied) {
                UnitType tankType = (i == 0) ? UNIT_MTANK2 : UNIT_LTANK;
                Spawn_Unit(playerHouse, tankType, tankCell, MISSION_GUARD);
            } else {
                UnitType tankType = (i == 0) ? UNIT_HTANK : UNIT_MTANK;
                Spawn_Unit(playerHouse, tankType, tankCell, MISSION_GUARD);
            }
        }

        // Infantry garrison at random positions
        for (int i = 0; i < 8; i++) {
            CELL infCell = Find_Nearby_Valid_Cell(baseCell, 10);
            if (infCell == -1) infCell = baseCell;

            InfantryType infType = (i % 3 == 0) ? INFANTRY_E3 : INFANTRY_E1;
            Spawn_Infantry(playerHouse, infType, infCell, MISSION_GUARD);
        }

        GlyphX_Debug_Print("ProcMission: Player base spawned for SURVIVAL with RANDOM layout");

    } else {
        // BASE_ASSAULT or other: Player gets an MCV and starting force
        GlyphX_Debug_Print("ProcMission: ASSAULT mode - spawning MCV and units");

        // MCV for base building
        Spawn_Unit(playerHouse, UNIT_MCV, baseCell, MISSION_GUARD);

        // Starting tanks at random positions near MCV
        int numTanks = playerIsAllied ? 4 : 3;
        for (int i = 0; i < numTanks; i++) {
            CELL tankCell = Find_Nearby_Valid_Cell(baseCell, 5);
            if (tankCell == -1) tankCell = baseCell + 3 + (i * 2);

            if (playerIsAllied) {
                UnitType tankType = (i < 2) ? UNIT_MTANK2 : UNIT_LTANK;
                Spawn_Unit(playerHouse, tankType, tankCell, MISSION_GUARD);
            } else {
                UnitType tankType = (i < 2) ? UNIT_MTANK : UNIT_LTANK;
                Spawn_Unit(playerHouse, tankType, tankCell, MISSION_GUARD);
            }
        }

        // Starting infantry at random positions
        for (int i = 0; i < 6; i++) {
            CELL infCell = Find_Nearby_Valid_Cell(baseCell, 6);
            if (infCell == -1) infCell = baseCell + (MAP_CELL_W * 2) + i;

            InfantryType infType = (i % 2 == 0) ? INFANTRY_E1 : INFANTRY_E3;
            Spawn_Infantry(playerHouse, infType, infCell, MISSION_GUARD);
        }

        GlyphX_Debug_Print("ProcMission: MCV and units spawned for ASSAULT with RANDOM positions");
    }

    GlyphX_Debug_Print("ProcMission: Player auto-spawn complete");
}

void ProcMissionManager::Auto_Spawn_Enemy_Base()
{
    GlyphX_Debug_Print("ProcMission: Auto-spawning enemy base with RANDOM placement...");
    char debugBuf[128];

    HousesType enemyHouse = CurrentMission.EnemyFaction;
    sprintf(debugBuf, "ProcMission: Enemy house = %d, Difficulty = %d", (int)enemyHouse, (int)CurrentDifficulty);
    GlyphX_Debug_Print(debugBuf);

    // Get the enemy house pointer
    HouseClass* housePtr = HouseClass::As_Pointer(enemyHouse);
    if (!housePtr) {
        GlyphX_Debug_Print("ProcMission: Enemy house pointer is NULL!");
        return;
    }

    // Scale parameters based on difficulty
    int numDefenses = 0;
    int numGuardTanks = 2;
    int numGuardInfantry = 4;
    int numHarvesters = 1;
    int startingCredits = 8000;
    int aiIQ = 1;
    bool hasSecondFactory = false;
    bool hasAdvancedPower = false;
    bool hasTechCenter = false;
    int maxPowerPlants = 10;

    switch (CurrentDifficulty) {
        case DIFF_EASY:
            numDefenses = 3;
            numGuardTanks = 1;
            numGuardInfantry = 3;
            numHarvesters = 1;
            startingCredits = 5000;
            aiIQ = 1;
            maxPowerPlants = 6;
            break;

        case DIFF_HARD:
            numDefenses = 10;
            numGuardTanks = 5;
            numGuardInfantry = 10;
            numHarvesters = 6;
            startingCredits = 20000;
            aiIQ = 3;
            hasSecondFactory = true;
            maxPowerPlants = 15;
            break;

        default: // DIFF_NORMAL
            numDefenses = 6;
            numGuardTanks = 3;
            numGuardInfantry = 6;
            numHarvesters = 3;
            startingCredits = 10000;
            aiIQ = 2;
            maxPowerPlants = 10;
            break;
    }

    // Get tech level from house (already set by scenario INI loading)
    int techLevel = housePtr->Control.TechLevel;
    hasAdvancedPower = (techLevel >= 5);
    hasTechCenter = (techLevel >= 9);

    sprintf(debugBuf, "ProcMission: Using scenario TechLevel=%d", techLevel);
    GlyphX_Debug_Print(debugBuf);

    // Configure the enemy AI (TechLevel already set by scenario INI)
    housePtr->IsActive = true;
    housePtr->IsAlerted = true;
    housePtr->IsBaseBuilding = false;  // Don't allow AI to build new structures
    housePtr->IsStarted = true;
    housePtr->IQ = aiIQ;
    housePtr->Credits = startingCredits;
    housePtr->Capacity = 50000;

    sprintf(debugBuf, "ProcMission: Enemy AI - Credits=%d, TechLevel=%d, IQ=%d",
        housePtr->Credits, techLevel, housePtr->IQ);
    GlyphX_Debug_Print(debugBuf);

    // Find a spawn location for the enemy base center
    CELL baseCell = Get_Region_Cell(PROC_REGION_BASE_B);
    if (baseCell == -1) baseCell = Get_Region_Cell(PROC_REGION_ENTRY_NORTH);
    if (baseCell == -1) baseCell = Get_Region_Cell(PROC_REGION_ENTRY_EAST);
    if (baseCell == -1) baseCell = Find_Fallback_Spawn_Cell(enemyHouse);

    if (baseCell == -1) {
        GlyphX_Debug_Print("ProcMission: Could not find location for enemy base!");
        return;
    }

    sprintf(debugBuf, "ProcMission: Enemy base center cell = %d", (int)baseCell);
    GlyphX_Debug_Print(debugBuf);

    // Determine if enemy is Soviet or Allied based on actual house type
    bool enemyIsSoviet = (enemyHouse == HOUSE_USSR || enemyHouse == HOUSE_UKRAINE ||
                          enemyHouse == HOUSE_BAD);  // Include BADGUY as Soviet
    sprintf(debugBuf, "ProcMission: enemyIsSoviet = %s (house %d)",
        enemyIsSoviet ? "YES" : "NO", (int)enemyHouse);
    GlyphX_Debug_Print(debugBuf);

    // === CORE STRUCTURES with RANDOM placement ===

    // Construction Yard (placed at center first, others placed randomly around it)
    Spawn_Building(enemyHouse, STRUCT_CONST, baseCell);
    GlyphX_Debug_Print("ProcMission: Spawned Construction Yard");

    // Refinery - random placement 4-10 cells from center
    Spawn_Building_Random(enemyHouse, STRUCT_REFINERY, baseCell, 4, 10);
    GlyphX_Debug_Print("ProcMission: Spawned Refinery (random)");

    // War Factory - random placement
    Spawn_Building_Random(enemyHouse, STRUCT_WEAP, baseCell, 4, 12);
    GlyphX_Debug_Print("ProcMission: Spawned War Factory (random)");

    // Second War Factory on hard - random placement
    if (hasSecondFactory) {
        Spawn_Building_Random(enemyHouse, STRUCT_WEAP, baseCell, 6, 14);
        GlyphX_Debug_Print("ProcMission: Spawned second War Factory (random)");
    }

    // Barracks - faction-appropriate type, random placement
    StructType barracksType = enemyIsSoviet ? STRUCT_TENT : STRUCT_BARRACKS;
    Spawn_Building_Random(enemyHouse, barracksType, baseCell, 3, 10);
    sprintf(debugBuf, "ProcMission: Spawned %s (random)", enemyIsSoviet ? "Soviet Barracks (TENT)" : "Allied Barracks");
    GlyphX_Debug_Print(debugBuf);

    // Radar - random placement
    Spawn_Building_Random(enemyHouse, STRUCT_RADAR, baseCell, 4, 10);
    GlyphX_Debug_Print("ProcMission: Spawned Radar (random)");

    // Tech Center on hard - faction-appropriate type
    if (hasTechCenter) {
        StructType techType = enemyIsSoviet ? STRUCT_SOVIET_TECH : STRUCT_ADVANCED_TECH;
        Spawn_Building_Random(enemyHouse, techType, baseCell, 5, 12);
        sprintf(debugBuf, "ProcMission: Spawned %s (random)", enemyIsSoviet ? "Soviet Tech Center" : "Allied Tech Center");
        GlyphX_Debug_Print(debugBuf);
    }

    // === DEFENSES with RANDOM placement ===
    StructType defenseType1 = enemyIsSoviet ? STRUCT_FLAME_TURRET : STRUCT_PILLBOX;
    StructType defenseType2 = enemyIsSoviet ? STRUCT_TESLA : STRUCT_TURRET;
    StructType aaType = enemyIsSoviet ? STRUCT_SAM : STRUCT_AAGUN;

    sprintf(debugBuf, "ProcMission: Defense types - Light:%d Heavy:%d AA:%d",
        (int)defenseType1, (int)defenseType2, (int)aaType);
    GlyphX_Debug_Print(debugBuf);

    int defensesSpawned = 0;

    // Spawn defenses at random positions around the base perimeter
    for (int i = 0; i < numDefenses; i++) {
        // Alternate between light and heavy defenses
        StructType defType = (i % 2 == 0) ? defenseType1 : defenseType2;

        // Defenses placed further out (8-18 cells) to form a perimeter
        if (Spawn_Building_Random(enemyHouse, defType, baseCell, 8, 18)) {
            defensesSpawned++;
        }
    }

    // Add AA defenses proportional to total defenses
    int numAA = numDefenses / 3;
    for (int i = 0; i < numAA; i++) {
        Spawn_Building_Random(enemyHouse, aaType, baseCell, 6, 14);
    }

    sprintf(debugBuf, "ProcMission: Spawned %d defenses + %d AA (random)", defensesSpawned, numAA);
    GlyphX_Debug_Print(debugBuf);

    // === POWER PLANTS with RANDOM placement ===
    // Spawn power plants until we have enough power
    int powerPlantsSpawned = 0;
    int powerBuffer = 50;

    StructType powerType = hasAdvancedPower ? STRUCT_ADVANCED_POWER : STRUCT_POWER;

    while (powerPlantsSpawned < maxPowerPlants) {
        // Check current power status
        int powerDeficit = housePtr->Drain - housePtr->Power + powerBuffer;

        if (powerDeficit <= 0 && powerPlantsSpawned >= 2) {
            break;  // Enough power
        }

        if (Spawn_Building_Random(enemyHouse, powerType, baseCell, 3, 12)) {
            powerPlantsSpawned++;
        } else {
            // Failed to find placement, stop trying
            break;
        }
    }

    sprintf(debugBuf, "ProcMission: Spawned %d %s (random, Power=%d, Drain=%d)",
        powerPlantsSpawned, hasAdvancedPower ? "Advanced Power Plants" : "Power Plants",
        housePtr->Power, housePtr->Drain);
    GlyphX_Debug_Print(debugBuf);

    // === GUARD UNITS at random positions near base ===
    for (int i = 0; i < numGuardTanks; i++) {
        CELL tankCell = Find_Nearby_Valid_Cell(baseCell, 8);
        if (tankCell == -1) tankCell = baseCell;

        if (enemyIsSoviet) {
            UnitType tankType = (i == 0) ? UNIT_HTANK : UNIT_MTANK;
            Spawn_Unit(enemyHouse, tankType, tankCell, MISSION_GUARD);
        } else {
            UnitType tankType = (i < 2) ? UNIT_MTANK2 : UNIT_LTANK;
            Spawn_Unit(enemyHouse, tankType, tankCell, MISSION_GUARD);
        }
    }

    sprintf(debugBuf, "ProcMission: Spawned %d guard tanks (random)", numGuardTanks);
    GlyphX_Debug_Print(debugBuf);

    // Guard infantry at random positions
    for (int i = 0; i < numGuardInfantry; i++) {
        CELL infCell = Find_Nearby_Valid_Cell(baseCell, 10);
        if (infCell == -1) infCell = baseCell;

        InfantryType infType = (i % 3 == 0) ? INFANTRY_E3 : INFANTRY_E1;
        Spawn_Infantry(enemyHouse, infType, infCell, MISSION_GUARD);
    }

    sprintf(debugBuf, "ProcMission: Spawned %d guard infantry (random)", numGuardInfantry);
    GlyphX_Debug_Print(debugBuf);

    // === HARVESTERS ===
    for (int i = 0; i < numHarvesters; i++) {
        CELL harvCell = Find_Nearby_Valid_Cell(baseCell, 8);
        if (harvCell == -1) harvCell = baseCell;

        Spawn_Unit(enemyHouse, UNIT_HARVESTER, harvCell, MISSION_HARVEST);
    }

    sprintf(debugBuf, "ProcMission: Spawned %d harvesters (random)", numHarvesters);
    GlyphX_Debug_Print(debugBuf);

    // === NAVAL YARD (if water is nearby) ===
    CELL waterCell = Find_Water_Cell_Near(baseCell, 20);
    if (waterCell != -1) {
        GlyphX_Debug_Print("ProcMission: Water found near enemy base, spawning naval yard...");

        StructType navalYardType = enemyIsSoviet ? STRUCT_SUB_PEN : STRUCT_SHIP_YARD;

        // Find a shore cell for the naval yard
        CELL navalYardCell = -1;
        for (int radius = 1; radius <= 5; radius++) {
            for (int dy = -radius; dy <= radius; dy++) {
                for (int dx = -radius; dx <= radius; dx++) {
                    if (abs(dx) != radius && abs(dy) != radius) continue;

                    int x = Cell_X(waterCell) + dx;
                    int y = Cell_Y(waterCell) + dy;
                    if (x < 0 || y < 0 || x >= MAP_CELL_W || y >= MAP_CELL_H) continue;

                    CELL testCell = XY_Cell(x, y);
                    if (!Map.In_Radar(testCell)) continue;

                    LandType land = Map[testCell].Land_Type();
                    if (land != LAND_WATER && land != LAND_ROCK) {
                        navalYardCell = testCell;
                        break;
                    }
                }
                if (navalYardCell != -1) break;
            }
            if (navalYardCell != -1) break;
        }

        if (navalYardCell != -1) {
            if (Spawn_Building(enemyHouse, navalYardType, navalYardCell)) {
                sprintf(debugBuf, "ProcMission: %s spawned at cell %d",
                    enemyIsSoviet ? "Sub Pen" : "Naval Yard", (int)navalYardCell);
                GlyphX_Debug_Print(debugBuf);

                int numShips = 1;
                switch (CurrentDifficulty) {
                    case DIFF_EASY:   numShips = 1; break;
                    case DIFF_HARD:   numShips = 3; break;
                    default:          numShips = 2; break;
                }

                CELL shipCell = waterCell;
                for (int i = 0; i < numShips; i++) {
                    if (enemyIsSoviet) {
                        VesselType shipType = (i == 0 && CurrentDifficulty == DIFF_HARD) ? VESSEL_MISSILESUB : VESSEL_SS;
                        Spawn_Vessel(enemyHouse, shipType, shipCell, MISSION_GUARD);
                    } else {
                        VesselType shipType = (i == 0) ? VESSEL_DD : VESSEL_CA;
                        if (CurrentDifficulty == DIFF_EASY) shipType = VESSEL_PT;
                        Spawn_Vessel(enemyHouse, shipType, shipCell, MISSION_GUARD);
                    }
                    CELL nextWater = Find_Water_Cell_Near(shipCell, 3);
                    if (nextWater != -1 && nextWater != shipCell) {
                        shipCell = nextWater;
                    }
                }

                sprintf(debugBuf, "ProcMission: Spawned %d naval units", numShips);
                GlyphX_Debug_Print(debugBuf);
            }
        }
    } else {
        GlyphX_Debug_Print("ProcMission: No water near enemy base, skipping naval yard");
    }

    GlyphX_Debug_Print("ProcMission: Enemy base spawn complete with RANDOM layout!");
}


/*******************************************************************************
 * OUTPOST AND MULTI-BASE SUPPORT
 ******************************************************************************/

void ProcMissionManager::Setup_Enemy_Alliances()
{
    GlyphX_Debug_Print("ProcMission: Setting up enemy alliances...");
    char debugBuf[128];

    HousesType mainEnemy = CurrentMission.EnemyFaction;
    HouseClass* mainEnemyHouse = HouseClass::As_Pointer(mainEnemy);
    if (!mainEnemyHouse) return;

    // Determine allied enemy houses based on faction type
    // Soviet: USSR, Ukraine, Bad
    // Allied: Greece, Spain, England, Germany, France, Turkey
    bool enemyIsSoviet = (mainEnemy == HOUSE_USSR || mainEnemy == HOUSE_UKRAINE || mainEnemy == HOUSE_BAD);

    HousesType alliedEnemies[6];
    int alliedCount = 0;

    if (enemyIsSoviet) {
        // Soviet allies
        if (mainEnemy != HOUSE_USSR) alliedEnemies[alliedCount++] = HOUSE_USSR;
        if (mainEnemy != HOUSE_UKRAINE) alliedEnemies[alliedCount++] = HOUSE_UKRAINE;
        if (mainEnemy != HOUSE_BAD) alliedEnemies[alliedCount++] = HOUSE_BAD;
    } else {
        // Allied allies
        if (mainEnemy != HOUSE_GREECE) alliedEnemies[alliedCount++] = HOUSE_GREECE;
        if (mainEnemy != HOUSE_SPAIN) alliedEnemies[alliedCount++] = HOUSE_SPAIN;
        if (mainEnemy != HOUSE_ENGLAND) alliedEnemies[alliedCount++] = HOUSE_ENGLAND;
        if (mainEnemy != HOUSE_GERMANY) alliedEnemies[alliedCount++] = HOUSE_GERMANY;
        if (mainEnemy != HOUSE_FRANCE) alliedEnemies[alliedCount++] = HOUSE_FRANCE;
        if (mainEnemy != HOUSE_TURKEY) alliedEnemies[alliedCount++] = HOUSE_TURKEY;
    }

    // Make all enemy houses allies of each other
    for (int i = 0; i < alliedCount; i++) {
        HouseClass* allyHouse = HouseClass::As_Pointer(alliedEnemies[i]);
        if (allyHouse) {
            // Main enemy allies with this house
            mainEnemyHouse->Make_Ally(alliedEnemies[i]);
            allyHouse->Make_Ally(mainEnemy);

            // This house allies with all other enemy houses
            for (int j = 0; j < alliedCount; j++) {
                if (i != j) {
                    allyHouse->Make_Ally(alliedEnemies[j]);
                }
            }

            sprintf(debugBuf, "ProcMission: %s allied with %s",
                HouseClass::As_Pointer(mainEnemy)->Class->IniName,
                allyHouse->Class->IniName);
            GlyphX_Debug_Print(debugBuf);
        }
    }

    // Configure outposts based on difficulty
    CurrentMission.OutpostCount = 0;

    switch (CurrentDifficulty) {
        case DIFF_EASY:
            // No outposts on easy
            break;

        case DIFF_NORMAL:
            // One outpost on normal
            CurrentMission.Outposts[0].IsEnabled = true;
            CurrentMission.Outposts[0].House = enemyIsSoviet ? HOUSE_UKRAINE : HOUSE_SPAIN;
            CurrentMission.Outposts[0].Region = PROC_REGION_OUTPOST_1;
            CurrentMission.Outposts[0].DefenseCount = 2;
            CurrentMission.Outposts[0].GuardCount = 2;
            CurrentMission.OutpostCount = 1;
            break;

        case DIFF_HARD:
            // Two outposts on hard
            CurrentMission.Outposts[0].IsEnabled = true;
            CurrentMission.Outposts[0].House = enemyIsSoviet ? HOUSE_UKRAINE : HOUSE_SPAIN;
            CurrentMission.Outposts[0].Region = PROC_REGION_OUTPOST_1;
            CurrentMission.Outposts[0].DefenseCount = 4;
            CurrentMission.Outposts[0].GuardCount = 3;

            CurrentMission.Outposts[1].IsEnabled = true;
            CurrentMission.Outposts[1].House = enemyIsSoviet ? HOUSE_BAD : HOUSE_ENGLAND;
            CurrentMission.Outposts[1].Region = PROC_REGION_OUTPOST_2;
            CurrentMission.Outposts[1].DefenseCount = 3;
            CurrentMission.Outposts[1].GuardCount = 2;

            CurrentMission.OutpostCount = 2;
            break;
    }

    // Configure neutral base (only on hard difficulty)
    if (CurrentDifficulty == DIFF_HARD) {
        CurrentMission.NeutralBase.IsEnabled = true;
        CurrentMission.NeutralBase.House = HOUSE_NEUTRAL;
        CurrentMission.NeutralBase.Region = PROC_REGION_NEUTRAL;
        CurrentMission.NeutralBase.IsHostileToAll = true;
        CurrentMission.NeutralBase.DefenseCount = 4;
        CurrentMission.NeutralBase.GuardCount = 3;
    }

    sprintf(debugBuf, "ProcMission: Configured %d outposts, neutral base %s",
        CurrentMission.OutpostCount, CurrentMission.NeutralBase.IsEnabled ? "enabled" : "disabled");
    GlyphX_Debug_Print(debugBuf);
}


void ProcMissionManager::Auto_Spawn_Outpost_Bases()
{
    if (CurrentMission.OutpostCount == 0) {
        GlyphX_Debug_Print("ProcMission: No outposts to spawn");
        return;
    }

    char debugBuf[128];
    sprintf(debugBuf, "ProcMission: Spawning %d outpost bases...", CurrentMission.OutpostCount);
    GlyphX_Debug_Print(debugBuf);

    // Determine if enemy is Soviet-aligned
    bool enemyIsSoviet = (CurrentMission.EnemyFaction == HOUSE_USSR ||
                          CurrentMission.EnemyFaction == HOUSE_UKRAINE ||
                          CurrentMission.EnemyFaction == HOUSE_BAD);

    for (int outpostIdx = 0; outpostIdx < CurrentMission.OutpostCount; outpostIdx++) {
        ProcOutpostConfig& outpost = CurrentMission.Outposts[outpostIdx];
        if (!outpost.IsEnabled) continue;

        CELL outpostCell = Get_Region_Cell(outpost.Region);
        if (outpostCell == -1) {
            // No waypoint defined, find a random location far from existing bases
            sprintf(debugBuf, "ProcMission: Outpost %d has no waypoint, finding random location...", outpostIdx);
            GlyphX_Debug_Print(debugBuf);

            // Each successive outpost should be further from bases to spread them out
            int minDistance = 20 + (outpostIdx * 10);
            outpostCell = Find_Random_Base_Location(minDistance);

            if (outpostCell == -1) {
                sprintf(debugBuf, "ProcMission: Could not find location for outpost %d, skipping", outpostIdx);
                GlyphX_Debug_Print(debugBuf);
                continue;
            }
        }

        HouseClass* housePtr = HouseClass::As_Pointer(outpost.House);
        if (!housePtr) {
            sprintf(debugBuf, "ProcMission: Outpost %d house %d is NULL, skipping", outpostIdx, (int)outpost.House);
            GlyphX_Debug_Print(debugBuf);
            continue;
        }

        // Initialize the outpost house
        housePtr->IsActive = true;
        housePtr->IsAlerted = true;
        housePtr->IsBaseBuilding = false;
        housePtr->IsStarted = true;
        housePtr->IQ = 1;
        housePtr->Credits = 2000;
        housePtr->Capacity = 10000;

        sprintf(debugBuf, "ProcMission: Spawning outpost %d for house %s at cell %d",
            outpostIdx, housePtr->Class->IniName, (int)outpostCell);
        GlyphX_Debug_Print(debugBuf);

        // Spawn a small outpost structure (tent/barracks as center)
        StructType barracksType = enemyIsSoviet ? STRUCT_TENT : STRUCT_BARRACKS;
        Spawn_Building(outpost.House, barracksType, outpostCell);

        // Spawn defenses around the outpost
        StructType defenseType1 = enemyIsSoviet ? STRUCT_FLAME_TURRET : STRUCT_PILLBOX;
        StructType defenseType2 = enemyIsSoviet ? STRUCT_TESLA : STRUCT_TURRET;

        for (int i = 0; i < outpost.DefenseCount; i++) {
            StructType defType = (i % 2 == 0) ? defenseType1 : defenseType2;
            Spawn_Building_Random(outpost.House, defType, outpostCell, 4, 10);
        }

        // Power plant for defenses
        Spawn_Building_Random(outpost.House, STRUCT_POWER, outpostCell, 3, 8);

        // Spawn guard units
        for (int i = 0; i < outpost.GuardCount; i++) {
            CELL unitCell = Find_Nearby_Valid_Cell(outpostCell, 6);
            if (unitCell == -1) unitCell = outpostCell;

            if (enemyIsSoviet) {
                UnitType tankType = (i == 0) ? UNIT_MTANK : UNIT_LTANK;
                Spawn_Unit(outpost.House, tankType, unitCell, MISSION_GUARD);
            } else {
                UnitType tankType = (i == 0) ? UNIT_MTANK2 : UNIT_LTANK;
                Spawn_Unit(outpost.House, tankType, unitCell, MISSION_GUARD);
            }
        }

        // Spawn some guard infantry
        int numInfantry = outpost.GuardCount + 1;
        for (int i = 0; i < numInfantry; i++) {
            CELL infCell = Find_Nearby_Valid_Cell(outpostCell, 5);
            if (infCell == -1) infCell = outpostCell;

            InfantryType infType = (i % 2 == 0) ? INFANTRY_E1 : INFANTRY_E3;
            Spawn_Infantry(outpost.House, infType, infCell, MISSION_GUARD);
        }

        sprintf(debugBuf, "ProcMission: Outpost %d spawned with %d defenses, %d guards",
            outpostIdx, outpost.DefenseCount, outpost.GuardCount);
        GlyphX_Debug_Print(debugBuf);
    }

    GlyphX_Debug_Print("ProcMission: Outpost spawning complete");
}


void ProcMissionManager::Auto_Spawn_Neutral_Base()
{
    if (!CurrentMission.NeutralBase.IsEnabled) {
        GlyphX_Debug_Print("ProcMission: Neutral base disabled");
        return;
    }

    char debugBuf[128];
    GlyphX_Debug_Print("ProcMission: Spawning neutral base...");

    CELL neutralCell = Get_Region_Cell(CurrentMission.NeutralBase.Region);
    if (neutralCell == -1) {
        // No waypoint defined, find a random location far from all bases
        GlyphX_Debug_Print("ProcMission: Neutral base has no waypoint, finding random location...");
        neutralCell = Find_Random_Base_Location(25);  // Neutral should be fairly isolated

        if (neutralCell == -1) {
            GlyphX_Debug_Print("ProcMission: Could not find location for neutral base, skipping");
            return;
        }
    }

    HousesType neutralHouse = CurrentMission.NeutralBase.House;
    HouseClass* housePtr = HouseClass::As_Pointer(neutralHouse);
    if (!housePtr) {
        GlyphX_Debug_Print("ProcMission: Neutral house pointer is NULL!");
        return;
    }

    // Initialize neutral house
    housePtr->IsActive = true;
    housePtr->IsAlerted = true;
    housePtr->IsBaseBuilding = false;
    housePtr->IsStarted = true;
    housePtr->IQ = 2;
    housePtr->Credits = 5000;
    housePtr->Capacity = 20000;

    // Make neutral hostile to everyone if configured
    if (CurrentMission.NeutralBase.IsHostileToAll) {
        // Neutral is enemy of everyone by default, no need to make enemies
        // Just ensure they're not allied with anyone
        GlyphX_Debug_Print("ProcMission: Neutral base configured as hostile to all");
    }

    sprintf(debugBuf, "ProcMission: Spawning neutral base for house %s at cell %d",
        housePtr->Class->IniName, (int)neutralCell);
    GlyphX_Debug_Print(debugBuf);

    // Neutral bases use a mix of Soviet and Allied tech (older/salvaged equipment)
    // Center building - use civilian structure or tech center if available
    Spawn_Building(neutralHouse, STRUCT_TENT, neutralCell);

    // Power
    Spawn_Building_Random(neutralHouse, STRUCT_POWER, neutralCell, 3, 8);
    Spawn_Building_Random(neutralHouse, STRUCT_POWER, neutralCell, 3, 8);

    // Defenses - mix of types
    for (int i = 0; i < CurrentMission.NeutralBase.DefenseCount; i++) {
        StructType defType;
        switch (i % 4) {
            case 0: defType = STRUCT_PILLBOX; break;
            case 1: defType = STRUCT_FLAME_TURRET; break;
            case 2: defType = STRUCT_TURRET; break;
            default: defType = STRUCT_TESLA; break;
        }
        Spawn_Building_Random(neutralHouse, defType, neutralCell, 5, 12);
    }

    // Guard units - mix of Soviet and Allied
    for (int i = 0; i < CurrentMission.NeutralBase.GuardCount; i++) {
        CELL unitCell = Find_Nearby_Valid_Cell(neutralCell, 6);
        if (unitCell == -1) unitCell = neutralCell;

        UnitType tankType;
        switch (i % 3) {
            case 0: tankType = UNIT_HTANK; break;   // Mammoth
            case 1: tankType = UNIT_MTANK2; break;  // Medium
            default: tankType = UNIT_MTANK; break;  // Heavy
        }
        Spawn_Unit(neutralHouse, tankType, unitCell, MISSION_GUARD);
    }

    // Guard infantry
    int numInfantry = CurrentMission.NeutralBase.GuardCount + 2;
    for (int i = 0; i < numInfantry; i++) {
        CELL infCell = Find_Nearby_Valid_Cell(neutralCell, 5);
        if (infCell == -1) infCell = neutralCell;

        InfantryType infType;
        switch (i % 3) {
            case 0: infType = INFANTRY_E1; break;
            case 1: infType = INFANTRY_E3; break;
            default: infType = INFANTRY_E4; break;  // Flamethrower
        }
        Spawn_Infantry(neutralHouse, infType, infCell, MISSION_GUARD);
    }

    sprintf(debugBuf, "ProcMission: Neutral base spawned with %d defenses, %d guards",
        CurrentMission.NeutralBase.DefenseCount, CurrentMission.NeutralBase.GuardCount);
    GlyphX_Debug_Print(debugBuf);
}


CELL ProcMissionManager::Get_Region_Cell(ProcRegionType region) const
{
    if (region <= PROC_REGION_NONE || region >= PROC_REGION_COUNT) return -1;

    int waypoint = Proc_Region_To_Waypoint(region);
    if (waypoint < 0 || waypoint >= WAYPT_HOME) return -1;

    return Scen.Waypoint[waypoint];
}

int ProcMissionManager::Get_Region_Waypoint(ProcRegionType region) const
{
    return Proc_Region_To_Waypoint(region);
}

void ProcMissionManager::AI()
{
    if (!IsMissionLoaded) return;
    Director.AI();
}

bool ProcMissionManager::Inject_Mission(DiffType difficulty)
{
    GlyphX_Debug_Print("ProcMission: Inject_Mission called");
    if (!IsMissionLoaded) {
        GlyphX_Debug_Print("ProcMission: Mission not loaded!");
        return false;
    }

    CurrentDifficulty = difficulty;

    // Initialize team pools
    ProcTeamPools.Initialize_Default_Pools();

    // Cache region cells
    GlyphX_Debug_Print("ProcMission: Caching region cells...");
    RegionCellsValid = true;
    for (int i = 0; i < PROC_REGION_COUNT; i++) {
        RegionCells[i] = Get_Region_Cell((ProcRegionType)i);
    }

    // Debug: print waypoint for Base_A
    char debugBuf[128];
    sprintf(debugBuf, "ProcMission: Base_A waypoint cell = %d", RegionCells[PROC_REGION_BASE_A]);
    GlyphX_Debug_Print(debugBuf);

    // Step 1: Initialize houses
    GlyphX_Debug_Print("ProcMission: Initializing houses...");
    if (!Initialize_Houses(difficulty)) {
        GlyphX_Debug_Print("ProcMission: Initialize_Houses FAILED");
        return false;
    }

    // Step 2: Spawn player assets
    GlyphX_Debug_Print("ProcMission: Spawning player assets...");
    if (!Spawn_Player_Assets()) {
        GlyphX_Debug_Print("ProcMission: Spawn_Player_Assets FAILED");
        return false;
    }

    // Step 3: Spawn enemy bases
    GlyphX_Debug_Print("ProcMission: Spawning enemy bases...");
    if (!Spawn_Enemy_Bases()) {
        GlyphX_Debug_Print("ProcMission: Spawn_Enemy_Bases FAILED");
        return false;
    }

    // Step 4: Setup victory/defeat triggers
    GlyphX_Debug_Print("ProcMission: Setting up triggers...");
    if (!Setup_Victory_Defeat_Triggers()) {
        GlyphX_Debug_Print("ProcMission: Setup_Victory_Defeat_Triggers FAILED");
        return false;
    }

    // Step 5: Initialize the director
    GlyphX_Debug_Print("ProcMission: Initializing director...");
    Director.Initialize(CurrentMission.DirectorRules, difficulty);

    return true;
}

bool ProcMissionManager::Initialize_Houses(DiffType difficulty)
{
    const ProcDifficultyModifiers* mods;
    switch (difficulty) {
        case DIFF_EASY: mods = &CurrentMission.EasyDifficulty; break;
        case DIFF_HARD: mods = &CurrentMission.HardDifficulty; break;
        default: mods = &CurrentMission.NormalDifficulty; break;
    }

	/*
    // Setup player house
    HouseClass* playerHouse = HouseClass::As_Pointer(CurrentMission.PlayerFaction);
    if (playerHouse) {
        const ProcPlayerConfig& config = CurrentMission.PlayerConfig;

        playerHouse->Control.TechLevel = config.TechLevel;
        playerHouse->Control.InitialCredits = (int)config.Credits * mods->PlayerCreditMult;
        playerHouse->Credits = playerHouse->Control.InitialCredits;
        playerHouse->Control.MaxUnit = config.MaxUnits;
        playerHouse->Control.MaxBuilding = config.MaxBuildings;
        playerHouse->Control.MaxInfantry = config.MaxInfantry;

        playerHouse->FirepowerBias = config.Handicaps.FirepowerBias * mods->PlayerFirepowerBias;
        playerHouse->ArmorBias = config.Handicaps.ArmorBias * mods->PlayerArmorBias;
        playerHouse->CostBias = config.Handicaps.CostBias * mods->PlayerCostBias;
        playerHouse->BuildSpeedBias = config.Handicaps.BuildSpeedBias;
        playerHouse->GroundspeedBias = config.Handicaps.GroundspeedBias;
        playerHouse->ROFBias = config.Handicaps.ROFBias;

        playerHouse->IsHuman = true;
        playerHouse->IsPlayerControl = true;
        playerHouse->IsActive = true;
    }
	*/

    // Setup enemy houses
    for (int i = 0; i < CurrentMission.EnemyBaseCount; i++) {
        const ProcEnemyBase& base = CurrentMission.EnemyBases[i];
        HouseClass* enemyHouse = HouseClass::As_Pointer(base.House);

        if (enemyHouse) {
            int techLevel = min(base.TechLevel, mods->EnemyTechLevelCap);
            enemyHouse->Control.TechLevel = techLevel;
            enemyHouse->Control.InitialCredits = (int)base.Credits * mods->EnemyCreditMult;
            enemyHouse->Credits = enemyHouse->Control.InitialCredits;

            // Set AI IQ
            enemyHouse->IQ = 3;

            // Apply handicaps
            enemyHouse->FirepowerBias = mods->EnemyFirepowerBias;
            enemyHouse->ArmorBias = mods->EnemyArmorBias;

            // Enable AI features based on flags
            enemyHouse->IsAlerted = base.AIFlags.IsAlerted;
            enemyHouse->IsBaseBuilding = base.AIFlags.IsBaseBuilding;

            // IMPORTANT: Enable autocreate so AI builds teams from pool
            if (base.AIFlags.IsAutocreate) {
                enemyHouse->IsStarted = true;
            }

            // Mark as active
            enemyHouse->IsHuman = false;
            enemyHouse->IsActive = true;

            // Start the AI if base is active
            if (base.State == PROC_BASE_STATE_ACTIVE) {
                enemyHouse->IsStarted = true;
            }
        }
    }

    return true;
}

bool ProcMissionManager::Is_Valid_Spawn_Cell(CELL cell) const
{
    if (cell == -1) return false;

    // Check if cell is within map bounds
    if (!Map.In_Radar(cell)) return false;

    // Check land type - reject water, river, and rock
    LandType land = Map[cell].Land_Type();
    if (land == LAND_WATER || land == LAND_RIVER || land == LAND_ROCK) {
        return false;
    }

    // Check if cell is clear for movement (infantry)
    if (!Map[cell].Is_Clear_To_Move(SPEED_FOOT, false, false)) {
        return false;
    }

    return true;
}

CELL ProcMissionManager::Find_Nearby_Valid_Cell(CELL startCell, int maxRadius) const
{
    if (startCell == -1) return -1;

    // If start cell is valid, use it
    if (Is_Valid_Spawn_Cell(startCell)) {
        return startCell;
    }

    // Search in expanding rings around the start cell
    for (int radius = 1; radius <= maxRadius; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                // Only check cells on the edge of this ring
                if (abs(dx) != radius && abs(dy) != radius) continue;

                int x = Cell_X(startCell) + dx;
                int y = Cell_Y(startCell) + dy;

                if (x < 0 || y < 0 || x >= MAP_CELL_W || y >= MAP_CELL_H) continue;

                CELL testCell = XY_Cell(x, y);
                if (Is_Valid_Spawn_Cell(testCell)) {
                    return testCell;
                }
            }
        }
    }

    return -1;
}

CELL ProcMissionManager::Find_House_Building_Cell(HousesType house) const
{
    // Find a cell near an existing building owned by this house
    for (int index = 0; index < Buildings.Count(); index++) {
        BuildingClass* building = Buildings.Ptr(index);
        if (building && building->IsActive && building->House->Class->House == house) {
            CELL cell = Coord_Cell(building->Center_Coord());
            // Find a valid cell nearby (buildings may be on non-passable terrain)
            return Find_Nearby_Valid_Cell(cell, 10);
        }
    }
    return -1;
}

CELL ProcMissionManager::Find_House_Unit_Cell(HousesType house) const
{
    // Find a cell near an existing unit owned by this house
    for (int index = 0; index < Units.Count(); index++) {
        UnitClass* unit = Units.Ptr(index);
        if (unit && unit->IsActive && unit->House->Class->House == house) {
            CELL cell = Coord_Cell(unit->Center_Coord());
            return Find_Nearby_Valid_Cell(cell, 10);
        }
    }
    return -1;
}

CELL ProcMissionManager::Find_Fallback_Spawn_Cell(HousesType house) const
{
    char debugBuf[128];

    // Try 1: Find existing building for this house
    CELL cell = Find_House_Building_Cell(house);
    if (cell != -1) {
        sprintf(debugBuf, "ProcMission: Fallback - found building, using cell %d", (int)cell);
        GlyphX_Debug_Print(debugBuf);
        return cell;
    }

    // Try 2: Find existing unit for this house
    cell = Find_House_Unit_Cell(house);
    if (cell != -1) {
        sprintf(debugBuf, "ProcMission: Fallback - found unit, using cell %d", (int)cell);
        GlyphX_Debug_Print(debugBuf);
        return cell;
    }

    // Try 3: Use map's random location picker (try multiple times to find valid cell)
    for (int attempt = 0; attempt < 50; attempt++) {
        cell = Map.Pick_Random_Location();
        if (Is_Valid_Spawn_Cell(cell)) {
            sprintf(debugBuf, "ProcMission: Fallback - random valid cell %d", (int)cell);
            GlyphX_Debug_Print(debugBuf);
            return cell;
        }
        // Try to find a valid cell nearby
        CELL validCell = Find_Nearby_Valid_Cell(cell, 5);
        if (validCell != -1) {
            sprintf(debugBuf, "ProcMission: Fallback - random nearby valid cell %d", (int)validCell);
            GlyphX_Debug_Print(debugBuf);
            return validCell;
        }
    }

    // Try 4: Scan from map center outward for a valid cell
    cell = XY_Cell(Map.MapCellX + Map.MapCellWidth / 2, Map.MapCellY + Map.MapCellHeight / 2);
    CELL validCell = Find_Nearby_Valid_Cell(cell, 30);
    if (validCell != -1) {
        sprintf(debugBuf, "ProcMission: Fallback - map center area cell %d", (int)validCell);
        GlyphX_Debug_Print(debugBuf);
        return validCell;
    }

    GlyphX_Debug_Print("ProcMission: WARNING - Could not find any valid spawn cell!");
    return cell; // Return center anyway as last resort
}


CELL ProcMissionManager::Find_Random_Base_Location(int minDistanceFromBases) const
{
    char debugBuf[128];
    GlyphX_Debug_Print("ProcMission: Finding random base location...");

    // Collect all existing base locations (player and enemy)
    CELL existingBases[10];
    int baseCount = 0;

    // Player base
    CELL playerBase = Get_Region_Cell(PROC_REGION_BASE_A);
    if (playerBase != -1 && baseCount < 10) {
        existingBases[baseCount++] = playerBase;
    }

    // Main enemy base
    CELL enemyBase = Get_Region_Cell(PROC_REGION_BASE_B);
    if (enemyBase != -1 && baseCount < 10) {
        existingBases[baseCount++] = enemyBase;
    }

    // Also check for any existing construction yards
    for (int index = 0; index < Buildings.Count() && baseCount < 10; index++) {
        BuildingClass* building = Buildings.Ptr(index);
        if (building && building->IsActive && *building == STRUCT_CONST) {
            CELL cell = Coord_Cell(building->Center_Coord());
            // Avoid duplicates
            bool isDupe = false;
            for (int i = 0; i < baseCount; i++) {
                int dx = abs(Cell_X(cell) - Cell_X(existingBases[i]));
                int dy = abs(Cell_Y(cell) - Cell_Y(existingBases[i]));
                if (dx < 5 && dy < 5) {
                    isDupe = true;
                    break;
                }
            }
            if (!isDupe) {
                existingBases[baseCount++] = cell;
            }
        }
    }

    sprintf(debugBuf, "ProcMission: Found %d existing bases to avoid", baseCount);
    GlyphX_Debug_Print(debugBuf);

    // Try random locations on the map
    for (int attempt = 0; attempt < 100; attempt++) {
        CELL cell = Map.Pick_Random_Location();

        if (!Is_Valid_Spawn_Cell(cell)) continue;

        // Check if this cell is on ore/gems
        LandType land = Map[cell].Land_Type();
        if (land == LAND_TIBERIUM) continue;

        // Check distance from all existing bases
        bool tooClose = false;
        for (int i = 0; i < baseCount; i++) {
            int dx = abs(Cell_X(cell) - Cell_X(existingBases[i]));
            int dy = abs(Cell_Y(cell) - Cell_Y(existingBases[i]));
            int distance = (dx > dy) ? dx : dy;  // Chebyshev distance

            if (distance < minDistanceFromBases) {
                tooClose = true;
                break;
            }
        }

        if (tooClose) continue;

        // Make sure there's room for buildings (check a small area)
        bool hasSpace = true;
        for (int dy = -2; dy <= 2 && hasSpace; dy++) {
            for (int dx = -2; dx <= 2 && hasSpace; dx++) {
                int x = Cell_X(cell) + dx;
                int y = Cell_Y(cell) + dy;
                if (x < 0 || y < 0 || x >= MAP_CELL_W || y >= MAP_CELL_H) {
                    hasSpace = false;
                    continue;
                }
                CELL testCell = XY_Cell(x, y);
                if (!Map.In_Radar(testCell)) {
                    hasSpace = false;
                    continue;
                }
                LandType testLand = Map[testCell].Land_Type();
                if (testLand == LAND_WATER || testLand == LAND_ROCK || testLand == LAND_TIBERIUM) {
                    hasSpace = false;
                }
            }
        }

        if (!hasSpace) continue;

        sprintf(debugBuf, "ProcMission: Found random base location at cell %d (attempt %d)", (int)cell, attempt);
        GlyphX_Debug_Print(debugBuf);
        return cell;
    }

    // Fallback: just find any valid cell far from player
    GlyphX_Debug_Print("ProcMission: Couldn't find ideal location, using fallback");
    for (int attempt = 0; attempt < 50; attempt++) {
        CELL cell = Map.Pick_Random_Location();
        if (Is_Valid_Spawn_Cell(cell)) {
            // At least check it's not right on top of player
            if (playerBase != -1) {
                int dx = abs(Cell_X(cell) - Cell_X(playerBase));
                int dy = abs(Cell_Y(cell) - Cell_Y(playerBase));
                if (dx > 15 || dy > 15) {
                    return cell;
                }
            } else {
                return cell;
            }
        }
    }

    return -1;
}


bool ProcMissionManager::Spawn_Building(HousesType house, StructType type, CELL cell)
{
    if (type == STRUCT_NONE || cell == -1) return false;

    HouseClass* housePtr = HouseClass::As_Pointer(house);
    if (!housePtr) return false;

    BuildingTypeClass const* buildingType = &BuildingTypeClass::As_Reference(type);
    if (!buildingType) return false;

    BuildingClass* building = (BuildingClass*)buildingType->Create_One_Of(housePtr);
    if (!building) return false;

    COORDINATE coord = Cell_Coord(cell);
    if (!building->Unlimbo(coord, DIR_N)) {
        delete building;
        return false;
    }

    return true;
}

bool ProcMissionManager::Spawn_Unit(HousesType house, UnitType type, CELL cell, MissionType mission)
{
    if (type == UNIT_NONE || cell == -1) return false;

    HouseClass* housePtr = HouseClass::As_Pointer(house);
    if (!housePtr) return false;

    UnitTypeClass const* unitType = &UnitTypeClass::As_Reference(type);
    if (!unitType) return false;

    UnitClass* unit = (UnitClass*)unitType->Create_One_Of(housePtr);
    if (!unit) return false;

    if (!Scan_Place_Object(unit, cell)) {
        delete unit;
        return false;
    }

    unit->Assign_Mission(mission);
    return true;
}

bool ProcMissionManager::Spawn_Infantry(HousesType house, InfantryType type, CELL cell, MissionType mission)
{
    if (type == INFANTRY_NONE || cell == -1) return false;

    HouseClass* housePtr = HouseClass::As_Pointer(house);
    if (!housePtr) return false;

    InfantryTypeClass const* infantryType = &InfantryTypeClass::As_Reference(type);
    if (!infantryType) return false;

    InfantryClass* infantry = (InfantryClass*)infantryType->Create_One_Of(housePtr);
    if (!infantry) return false;

    if (!Scan_Place_Object(infantry, cell)) {
        delete infantry;
        return false;
    }

    infantry->Assign_Mission(mission);
    return true;
}

bool ProcMissionManager::Spawn_Vessel(HousesType house, VesselType type, CELL cell, MissionType mission)
{
    if (type == VESSEL_NONE || cell == -1) return false;

    HouseClass* housePtr = HouseClass::As_Pointer(house);
    if (!housePtr) return false;

    VesselTypeClass const* vesselType = &VesselTypeClass::As_Reference(type);
    if (!vesselType) return false;

    VesselClass* vessel = (VesselClass*)vesselType->Create_One_Of(housePtr);
    if (!vessel) return false;

    COORDINATE coord = Cell_Coord(cell);
    if (!vessel->Unlimbo(coord, DIR_N)) {
        delete vessel;
        return false;
    }

    vessel->Assign_Mission(mission);
    return true;
}

CELL ProcMissionManager::Find_Random_Building_Cell(CELL centerCell, StructType type, int minRadius, int maxRadius) const
{
    if (centerCell == -1 || type == STRUCT_NONE) return -1;

    BuildingTypeClass const* buildingType = &BuildingTypeClass::As_Reference(type);
    if (!buildingType) return -1;

    // Get building dimensions
    int bWidth = buildingType->Width();
    int bHeight = buildingType->Height();

    // Try multiple random positions
    const int maxAttempts = 50;
    for (int attempt = 0; attempt < maxAttempts; attempt++) {
        // Pick random offsets within the radius range
        // Use a simple box-based approach with distance check
        int dx = Random_Pick(-maxRadius, maxRadius);
        int dy = Random_Pick(-maxRadius, maxRadius);

        // Check if within desired radius range
        int dist = abs(dx) + abs(dy);  // Manhattan distance approximation
        if (dist < minRadius || dist > maxRadius * 2) {
            continue;
        }

        int x = Cell_X(centerCell) + dx;
        int y = Cell_Y(centerCell) + dy;

        // Bounds check
        if (x < 2 || y < 2 || x >= MAP_CELL_W - bWidth - 2 || y >= MAP_CELL_H - bHeight - 2) {
            continue;
        }

        CELL testCell = XY_Cell(x, y);
        if (!Map.In_Radar(testCell)) continue;

        // Check if we can place the building here
        bool canPlace = true;
        for (int by = 0; by < bHeight && canPlace; by++) {
            for (int bx = 0; bx < bWidth && canPlace; bx++) {
                CELL checkCell = testCell + (by * MAP_CELL_W) + bx;
                if (!Map.In_Radar(checkCell)) {
                    canPlace = false;
                    break;
                }

                CellClass& cell = Map[checkCell];

                // Check land type
                LandType land = cell.Land_Type();
                if (land == LAND_WATER || land == LAND_RIVER || land == LAND_ROCK) {
                    canPlace = false;
                    break;
                }

                // Check if cell is occupied
                if (cell.Cell_Building() != NULL || cell.Cell_Unit() != NULL) {
                    canPlace = false;
                    break;
                }

                // Check terrain objects
                if (cell.Cell_Terrain() != NULL) {
                    canPlace = false;
                    break;
                }
            }
        }

        if (canPlace) {
            return testCell;
        }
    }

    return -1;  // Could not find valid placement
}

bool ProcMissionManager::Spawn_Building_Random(HousesType house, StructType type, CELL centerCell, int minRadius, int maxRadius)
{
    CELL buildCell = Find_Random_Building_Cell(centerCell, type, minRadius, maxRadius);
    if (buildCell == -1) {
        // Fallback: try at the center with small offset
        buildCell = Find_Random_Building_Cell(centerCell, type, 0, 5);
    }

    if (buildCell != -1) {
        return Spawn_Building(house, type, buildCell);
    }

    return false;
}

CELL ProcMissionManager::Find_Water_Cell_Near(CELL startCell, int maxRadius) const
{
    if (startCell == -1) return -1;

    // Search in expanding rings around the start cell for water
    for (int radius = 1; radius <= maxRadius; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                // Only check cells on the edge of this ring
                if (abs(dx) != radius && abs(dy) != radius) continue;

                int x = Cell_X(startCell) + dx;
                int y = Cell_Y(startCell) + dy;

                if (x < 0 || y < 0 || x >= MAP_CELL_W || y >= MAP_CELL_H) continue;

                CELL testCell = XY_Cell(x, y);

                // Check if this cell is water
                if (!Map.In_Radar(testCell)) continue;

                LandType land = Map[testCell].Land_Type();
                if (land == LAND_WATER) {
                    return testCell;
                }
            }
        }
    }

    return -1;  // No water found
}

bool ProcMissionManager::Spawn_Player_Assets()
{
    const ProcPlayerConfig& config = CurrentMission.PlayerConfig;

    char debugBuf[128];
    sprintf(debugBuf, "ProcMission: Player region = %d", (int)config.Region);
    GlyphX_Debug_Print(debugBuf);

    CELL baseCell = Get_Region_Cell(config.Region);
    sprintf(debugBuf, "ProcMission: Player baseCell from waypoint = %d", (int)baseCell);
    GlyphX_Debug_Print(debugBuf);

    // Fallback: if no waypoint, find existing assets or use random location
    if (baseCell == -1) {
        GlyphX_Debug_Print("ProcMission: No waypoint set, using fallback...");
        baseCell = Find_Fallback_Spawn_Cell(CurrentMission.PlayerFaction);
    }

    if (baseCell == -1) {
        GlyphX_Debug_Print("ProcMission: All fallbacks failed!");
        return false;
    }

    sprintf(debugBuf, "ProcMission: Final baseCell = %d", (int)baseCell);
    GlyphX_Debug_Print(debugBuf);

    sprintf(debugBuf, "ProcMission: Spawning %d buildings, %d units, %d infantry",
        config.BuildingCount, config.UnitCount, config.InfantryCount);
    GlyphX_Debug_Print(debugBuf);

    // Spawn buildings
    for (int i = 0; i < config.BuildingCount; i++) {
        const ProcBuildingDef& bdef = config.Buildings[i];
        CELL targetCell = baseCell + (bdef.OffsetY * MAP_CELL_W) + bdef.OffsetX;

        for (int j = 0; j < bdef.Count; j++) {
            Spawn_Building(CurrentMission.PlayerFaction, bdef.Type, targetCell);
        }
    }

    // Spawn units
    for (int i = 0; i < config.UnitCount; i++) {
        const ProcUnitDef& udef = config.Units[i];
        for (int j = 0; j < udef.Count; j++) {
            Spawn_Unit(CurrentMission.PlayerFaction, udef.Type, baseCell, udef.Mission);
        }
    }

    // Spawn infantry
    for (int i = 0; i < config.InfantryCount; i++) {
        const ProcInfantryDef& idef = config.Infantry[i];
        for (int j = 0; j < idef.Count; j++) {
            Spawn_Infantry(CurrentMission.PlayerFaction, idef.Type, baseCell, idef.Mission);
        }
    }

    return true;
}

bool ProcMissionManager::Spawn_Enemy_Bases()
{
    char debugBuf[128];

    for (int b = 0; b < CurrentMission.EnemyBaseCount; b++) {
        const ProcEnemyBase& base = CurrentMission.EnemyBases[b];

        sprintf(debugBuf, "ProcMission: Enemy base %d, region = %d", b, (int)base.Region);
        GlyphX_Debug_Print(debugBuf);

        CELL baseCell = Get_Region_Cell(base.Region);

        // Fallback: if no waypoint, find existing assets or use random location
        if (baseCell == -1) {
            GlyphX_Debug_Print("ProcMission: No waypoint for enemy base, using fallback...");
            baseCell = Find_Fallback_Spawn_Cell(base.House);
        }

        if (baseCell == -1) {
            GlyphX_Debug_Print("ProcMission: Skipping enemy base - no valid cell found");
            continue;
        }

        sprintf(debugBuf, "ProcMission: Enemy base cell = %d", (int)baseCell);
        GlyphX_Debug_Print(debugBuf);

        // Spawn buildings
        for (int i = 0; i < base.BuildingCount; i++) {
            const ProcBuildingDef& bdef = base.Buildings[i];
            CELL targetCell = baseCell + (bdef.OffsetY * MAP_CELL_W) + bdef.OffsetX;
            for (int j = 0; j < bdef.Count; j++) {
                Spawn_Building(base.House, bdef.Type, targetCell);
            }
        }

        // Spawn defenses
        for (int i = 0; i < base.DefenseCount; i++) {
            const ProcBuildingDef& ddef = base.Defenses[i];
            CELL targetCell = baseCell + (ddef.OffsetY * MAP_CELL_W) + ddef.OffsetX;
            for (int j = 0; j < ddef.Count; j++) {
                Spawn_Building(base.House, ddef.Type, targetCell);
            }
        }

        // Spawn guard units
        MissionType guardMission = (base.State == PROC_BASE_STATE_ACTIVE) ? MISSION_GUARD : MISSION_SLEEP;
        for (int i = 0; i < base.GuardUnitCount; i++) {
            const ProcUnitDef& udef = base.GuardUnits[i];
            MissionType mission = (udef.Mission != MISSION_NONE) ? udef.Mission : guardMission;
            for (int j = 0; j < udef.Count; j++) {
                Spawn_Unit(base.House, udef.Type, baseCell, mission);
            }
        }

        // Spawn guard infantry
        for (int i = 0; i < base.GuardInfantryCount; i++) {
            const ProcInfantryDef& idef = base.GuardInfantry[i];
            MissionType mission = (idef.Mission != MISSION_NONE) ? idef.Mission : guardMission;
            for (int j = 0; j < idef.Count; j++) {
                Spawn_Infantry(base.House, idef.Type, baseCell, mission);
            }
        }
    }

    return true;
}

bool ProcMissionManager::Setup_Victory_Defeat_Triggers()
{
    char debugBuf[128];
    sprintf(debugBuf, "ProcMission: Setting up triggers for theme %d", (int)CurrentMission.Theme);
    GlyphX_Debug_Print(debugBuf);

    // Defeat trigger is common to all modes: All player assets destroyed
	/*
    TriggerTypeClass* loseTrig = new TriggerTypeClass();
    if (loseTrig) {
        strcpy((char*)loseTrig->IniName, "PLOSE");
        loseTrig->House = CurrentMission.PlayerFaction;
        loseTrig->IsActive = true;
        loseTrig->IsPersistant = TriggerTypeClass::VOLATILE;
        loseTrig->Event1.Event = TEVENT_ALL_DESTROYED;
        loseTrig->Event1.Data.House = CurrentMission.PlayerFaction;
        loseTrig->EventControl = MULTI_ONLY;
        loseTrig->Action1.Action = TACTION_LOSE;
        loseTrig->ActionControl = MULTI_ONLY;

        Find_Or_Make(loseTrig);
    }
	*/
    // Victory trigger depends on mission theme
    switch (CurrentMission.Theme) {
        case PROC_THEME_SURVIVAL:
            Setup_Survival_Triggers();
            break;

        case PROC_THEME_BASE_ASSAULT:
        default:
            Setup_Base_Assault_Triggers();
            break;
    }

    return true;
}

void ProcMissionManager::Setup_Base_Assault_Triggers()
{
    GlyphX_Debug_Print("ProcMission: Setting up BASE_ASSAULT victory trigger");

    // Victory: All enemy buildings destroyed
    TriggerTypeClass* winTrig = new TriggerTypeClass();
    if (winTrig) {
        strcpy((char*)winTrig->IniName, "PWIN");
        winTrig->House = CurrentMission.PlayerFaction;
        winTrig->IsActive = true;
        winTrig->IsPersistant = TriggerTypeClass::VOLATILE;
        winTrig->Event1.Event = TEVENT_BUILDINGS_DESTROYED;
        winTrig->Event1.Data.House = CurrentMission.EnemyFaction;
        winTrig->EventControl = MULTI_ONLY;
        winTrig->Action1.Action = TACTION_WIN;
        winTrig->ActionControl = MULTI_ONLY;

        Find_Or_Make(winTrig);
    }
}

void ProcMissionManager::Setup_Survival_Triggers()
{
    // Get survival time - default to 10 minutes if not specified
    int survivalSeconds = CurrentMission.SurvivalTime;
    if (survivalSeconds <= 0) {
        survivalSeconds = 20 * 60; // 20 minutes default
    }

    char debugBuf[128];
    sprintf(debugBuf, "ProcMission: Setting up SURVIVAL triggers - %d seconds", survivalSeconds);
    GlyphX_Debug_Print(debugBuf);

    // Start the mission timer immediately
    // Timer is in ticks (15 ticks per second)
    int timerTicks = survivalSeconds * TICKS_PER_SECOND;
    Scen.MissionTimer = timerTicks;
    Scen.MissionTimer.Start();

    // Victory: Timer expires (player survived!)
    TriggerTypeClass* winTrig = new TriggerTypeClass();
    if (winTrig) {
        strcpy((char*)winTrig->IniName, "PWIN");
        winTrig->House = CurrentMission.PlayerFaction;
        winTrig->IsActive = true;
        winTrig->IsPersistant = TriggerTypeClass::VOLATILE;
        winTrig->Event1.Event = TEVENT_MISSION_TIMER_EXPIRED;
        winTrig->EventControl = MULTI_ONLY;
        winTrig->Action1.Action = TACTION_WIN;
        winTrig->ActionControl = MULTI_ONLY;

        Find_Or_Make(winTrig);
    }

    GlyphX_Debug_Print("ProcMission: SURVIVAL triggers created, timer started");
}

bool ProcMissionManager::Initialize_Team_Pools()
{
    ProcTeamPools.Initialize_Default_Pools();
    return true;
}

void ProcMissionManager::Apply_Difficulty_Modifiers(HouseClass* house,
    const ProcDifficultyModifiers& mods, bool isPlayer)
{
    if (!house) return;

    if (isPlayer) {
        house->FirepowerBias = house->FirepowerBias * mods.PlayerFirepowerBias;
        house->ArmorBias = house->ArmorBias * mods.PlayerArmorBias;
        house->CostBias = house->CostBias * mods.PlayerCostBias;
    } else {
        house->FirepowerBias = mods.EnemyFirepowerBias;
        house->ArmorBias = mods.EnemyArmorBias;
    }
}


/*******************************************************************************
 * PROCMAPPOOLMANAGER IMPLEMENTATION
 ******************************************************************************/

ProcMapPoolManager& ProcMapPoolManager::Instance()
{
    if (!s_ProcMapPoolInstance) {
        s_ProcMapPoolInstance = new ProcMapPoolManager();
    }
    return *s_ProcMapPoolInstance;
}

ProcMapPoolManager::ProcMapPoolManager() : MapCount(0)
{
}

ProcMapPoolManager::~ProcMapPoolManager()
{
}

bool ProcMapPoolManager::Load_From_INI(const char* filename)
{
    // Loading is handled by ProcMissionINI
    return true;
}

const ProcMapEntry* ProcMapPoolManager::Get_Random_Map() const
{
    if (MapCount == 0) return NULL;
    int index = Random_Pick(0, MapCount - 1);
    return &Maps[index];
}

const ProcMapEntry* ProcMapPoolManager::Get_Map(int index) const
{
    if (index < 0 || index >= MapCount) return NULL;
    return &Maps[index];
}

const ProcMapEntry* ProcMapPoolManager::Get_Map_By_Name(const char* displayName) const
{
    if (!displayName) return NULL;

    for (int i = 0; i < MapCount; i++) {
        if (stricmp(Maps[i].DisplayName, displayName) == 0) {
            return &Maps[i];
        }
    }
    return NULL;
}

bool ProcMapPoolManager::Add_Map(const ProcMapEntry& map)
{
    if (MapCount >= PROC_MAX_MAPS) return false;
    Maps[MapCount++] = map;
    return true;
}


/*******************************************************************************
 * PROCMISSIONMENUMANAGER IMPLEMENTATION
 ******************************************************************************/

ProcMissionMenuManager& ProcMissionMenuManager::Instance()
{
    if (!s_ProcMissionMenuInstance) {
        s_ProcMissionMenuInstance = new ProcMissionMenuManager();
    }
    return *s_ProcMissionMenuInstance;
}

ProcMissionMenuManager::ProcMissionMenuManager() : MissionCount(0)
{
}

ProcMissionMenuManager::~ProcMissionMenuManager()
{
}

bool ProcMissionMenuManager::Load_From_INI(const char* filename)
{
    // Loading is handled by ProcMissionINI
    return true;
}

int ProcMissionMenuManager::Get_Mission_Count(HousesType faction) const
{
    int count = 0;
    for (int i = 0; i < MissionCount; i++) {
        if (Missions[i].IsEnabled && Missions[i].PlayerFaction == faction) {
            count++;
        }
    }
    return count;
}

const ProcMenuMission* ProcMissionMenuManager::Get_Mission(HousesType faction, int index) const
{
    int count = 0;
    for (int i = 0; i < MissionCount; i++) {
        if (Missions[i].IsEnabled && Missions[i].PlayerFaction == faction) {
            if (count == index) {
                return &Missions[i];
            }
            count++;
        }
    }
    return NULL;
}

const ProcMenuMission* ProcMissionMenuManager::Get_Mission_By_ID(const char* id) const
{
    if (!id) return NULL;

    for (int i = 0; i < MissionCount; i++) {
        if (stricmp(Missions[i].ID, id) == 0) {
            return &Missions[i];
        }
    }
    return NULL;
}

bool ProcMissionMenuManager::Add_Mission(const ProcMenuMission& mission)
{
    if (MissionCount >= PROC_MAX_MENU_MISSIONS) return false;
    Missions[MissionCount++] = mission;
    return true;
}


/*******************************************************************************
 * SAFE CELL FINDING UTILITY FUNCTIONS
 ******************************************************************************/

CELL Proc_Find_Safe_Cell_Near(CELL centerCell, HousesType forHouse, int searchRadius)
{
    if (centerCell == -1) return -1;

    HouseClass* house = HouseClass::As_Pointer(forHouse);
    if (!house) return centerCell;

    // Score cells based on safety (distance from enemy units/buildings)
    CELL bestCell = -1;
    int bestScore = -9999;

    // Search in expanding rings
    for (int radius = 1; radius <= searchRadius; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                // Only check cells on this ring's perimeter
                if (abs(dx) != radius && abs(dy) != radius) continue;

                int x = Cell_X(centerCell) + dx;
                int y = Cell_Y(centerCell) + dy;

                if (x < 0 || y < 0 || x >= MAP_CELL_W || y >= MAP_CELL_H) continue;

                CELL testCell = XY_Cell(x, y);
                if (!Map.In_Radar(testCell)) continue;

                CellClass& cell = Map[testCell];

                // Must be passable ground
                LandType land = cell.Land_Type();
                if (land == LAND_WATER || land == LAND_RIVER || land == LAND_ROCK) {
                    continue;
                }

                // Must be clear of buildings and units
                if (cell.Cell_Building() != NULL || cell.Cell_Terrain() != NULL) {
                    continue;
                }

                // Calculate safety score
                int score = 100;

                // Check for nearby enemy threats
                for (int checkY = -5; checkY <= 5; checkY++) {
                    for (int checkX = -5; checkX <= 5; checkX++) {
                        int cx = x + checkX;
                        int cy = y + checkY;
                        if (cx < 0 || cy < 0 || cx >= MAP_CELL_W || cy >= MAP_CELL_H) continue;

                        CELL checkCell = XY_Cell(cx, cy);
                        if (!Map.In_Radar(checkCell)) continue;

                        CellClass& check = Map[checkCell];

                        // Penalize cells near enemy buildings
                        BuildingClass* building = check.Cell_Building();
                        if (building && !house->Is_Ally(building)) {
                            int dist = abs(checkX) + abs(checkY);
                            score -= (20 - dist * 3);  // Closer = worse
                        }

                        // Penalize cells near enemy units
                        UnitClass* unit = check.Cell_Unit();
                        if (unit && !house->Is_Ally(unit)) {
                            int dist = abs(checkX) + abs(checkY);
                            score -= (15 - dist * 2);
                        }
                    }
                }

                // Bonus for being near friendly units (safety in numbers)
                for (int checkY = -3; checkY <= 3; checkY++) {
                    for (int checkX = -3; checkX <= 3; checkX++) {
                        int cx = x + checkX;
                        int cy = y + checkY;
                        if (cx < 0 || cy < 0 || cx >= MAP_CELL_W || cy >= MAP_CELL_H) continue;

                        CELL checkCell = XY_Cell(cx, cy);
                        if (!Map.In_Radar(checkCell)) continue;

                        CellClass& check = Map[checkCell];

                        BuildingClass* building = check.Cell_Building();
                        if (building && house->Is_Ally(building)) {
                            score += 5;
                        }

                        UnitClass* unit = check.Cell_Unit();
                        if (unit && house->Is_Ally(unit)) {
                            score += 3;
                        }
                    }
                }

                if (score > bestScore) {
                    bestScore = score;
                    bestCell = testCell;
                }
            }
        }
    }

    return (bestCell != -1) ? bestCell : centerCell;
}

CELL Proc_Find_Staging_Cell(CELL targetCell, HousesType forHouse, int distance)
{
    if (targetCell == -1) return -1;

    HouseClass* house = HouseClass::As_Pointer(forHouse);
    if (!house) return -1;

    // Find the house's base center (average position of their buildings)
    int homeX = 0, homeY = 0, buildingCount = 0;

    for (int index = 0; index < Buildings.Count(); index++) {
        BuildingClass* building = Buildings.Ptr(index);
        if (building && building->IsActive && building->House == house) {
            CELL bCell = Coord_Cell(building->Center_Coord());
            homeX += Cell_X(bCell);
            homeY += Cell_Y(bCell);
            buildingCount++;
        }
    }

    if (buildingCount == 0) {
        // No buildings, can't find staging point
        return Proc_Find_Safe_Cell_Near(targetCell, forHouse, distance);
    }

    homeX /= buildingCount;
    homeY /= buildingCount;

    // Calculate direction from home to target
    int targetX = Cell_X(targetCell);
    int targetY = Cell_Y(targetCell);

    int dirX = targetX - homeX;
    int dirY = targetY - homeY;

    // Normalize direction
    int totalDist = abs(dirX) + abs(dirY);
    if (totalDist == 0) {
        return Proc_Find_Safe_Cell_Near(targetCell, forHouse, distance);
    }

    // Find a staging point that's 'distance' cells away from target, towards home
    int stagingX = targetX - (dirX * distance / totalDist);
    int stagingY = targetY - (dirY * distance / totalDist);

    // Clamp to map bounds
    if (stagingX < 2) stagingX = 2;
    if (stagingY < 2) stagingY = 2;
    if (stagingX >= MAP_CELL_W - 2) stagingX = MAP_CELL_W - 3;
    if (stagingY >= MAP_CELL_H - 2) stagingY = MAP_CELL_H - 3;

    CELL stagingCell = XY_Cell(stagingX, stagingY);

    // Find a safe cell near the staging point
    return Proc_Find_Safe_Cell_Near(stagingCell, forHouse, 8);
}
