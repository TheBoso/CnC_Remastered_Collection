//
// Copyright 2020 Electronic Arts Inc.
//
// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : Command & Conquer                                            *
 *                                                                                             *
 *                    File Name : AUTOPLAY.CPP                                                 *
 *                                                                                             *
 *                   Programmer : Autonomous Player Mod                                        *
 *                                                                                             *
 *                   Start Date : January 2026                                                 *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Autonomous_Player_AI -- Process autonomous player units each frame                        *
 *   Process_Patrol_Unit -- Handle patrol behavior for a single unit                           *
 *   Process_Attack_Units -- Handle coordinated attack behavior                                *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "function.h"
#include "autoplay.h"


/*
** Forward declarations
*/
static void Process_Factory_Teams(void);


/***********************************************************************************************
 * Create_Attack_Team -- Create a temporary team from selected units for coordinated attack    *
 *                                                                                             *
 *    This routine creates a temporary TeamTypeClass and TeamClass for selected units to       *
 *    perform a coordinated attack using the team mission system.                              *
 *                                                                                             *
 * INPUT:   house -- Pointer to the house that owns the selected units                         *
 *                                                                                             *
 * OUTPUT:  Returns the number of units added to the team                                      *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/2026 - Created for Autonomous Player Mod                                               *
 *=============================================================================================*/
int Create_Attack_Team(HouseClass * house, CELL gather_cell)
{
    if (!house) {
        GlyphX_Debug_Print("Create_Attack_Team: house is NULL");
        return 0;
    }

    char dbg[256];
    sprintf(dbg, "Create_Attack_Team: Starting for house %d, gather cell %d...", house->Class->House, gather_cell);
    GlyphX_Debug_Print(dbg);

    /*
    ** First pass: count unit types and quantities for selected units
    ** We need to know what member types to put in the TeamTypeClass
    ** IMPORTANT: Use &Class_Of() to match what Can_Add() compares against
    */
    struct UnitTypeCount {
        TechnoTypeClass const * Class;
        int Count;
    };
    UnitTypeCount type_counts[20];  // Support up to 20 different unit types
    int type_count = 0;
    int total_units = 0;
    int scanned_units = 0;
    int scanned_inf = 0;
    int scanned_vessels = 0;
    int scanned_aircraft = 0;

    // Process ground units
    for (int index = 0; index < Units.Count(); index++) {
        UnitClass * unit = Units.Ptr(index);
        scanned_units++;
        if (unit != NULL && unit->House == house && unit->Is_Selected_By_Player(house)) {
            // Use &Class_Of() cast to TechnoTypeClass - this matches what Can_Add() compares
            TechnoTypeClass const * tclass = (TechnoTypeClass const *)&unit->Class_Of();
            bool found = false;
            for (int t = 0; t < type_count; t++) {
                if (type_counts[t].Class == tclass) {
                    type_counts[t].Count++;
                    found = true;
                    break;
                }
            }
            if (!found && type_count < 20) {
                type_counts[type_count].Class = tclass;
                type_counts[type_count].Count = 1;
                type_count++;
            }
            total_units++;
        }
    }

    // Process infantry
    for (int index = 0; index < Infantry.Count(); index++) {
        InfantryClass * inf = Infantry.Ptr(index);
        scanned_inf++;
        if (inf != NULL && inf->House == house && inf->Is_Selected_By_Player(house)) {
            TechnoTypeClass const * tclass = (TechnoTypeClass const *)&inf->Class_Of();
            bool found = false;
            for (int t = 0; t < type_count; t++) {
                if (type_counts[t].Class == tclass) {
                    type_counts[t].Count++;
                    found = true;
                    break;
                }
            }
            if (!found && type_count < 20) {
                type_counts[type_count].Class = tclass;
                type_counts[type_count].Count = 1;
                type_count++;
            }
            total_units++;
        }
    }

    // Process vessels
    for (int index = 0; index < Vessels.Count(); index++) {
        VesselClass * vessel = Vessels.Ptr(index);
        scanned_vessels++;
        if (vessel != NULL && vessel->House == house && vessel->Is_Selected_By_Player(house)) {
            TechnoTypeClass const * tclass = (TechnoTypeClass const *)&vessel->Class_Of();
            bool found = false;
            for (int t = 0; t < type_count; t++) {
                if (type_counts[t].Class == tclass) {
                    type_counts[t].Count++;
                    found = true;
                    break;
                }
            }
            if (!found && type_count < 20) {
                type_counts[type_count].Class = tclass;
                type_counts[type_count].Count = 1;
                type_count++;
            }
            total_units++;
        }
    }

    // Process aircraft
    for (int index = 0; index < Aircraft.Count(); index++) {
        AircraftClass * aircraft = Aircraft.Ptr(index);
        scanned_aircraft++;
        if (aircraft != NULL && aircraft->House == house && aircraft->Is_Selected_By_Player(house)) {
            TechnoTypeClass const * tclass = (TechnoTypeClass const *)&aircraft->Class_Of();
            bool found = false;
            for (int t = 0; t < type_count; t++) {
                if (type_counts[t].Class == tclass) {
                    type_counts[t].Count++;
                    found = true;
                    break;
                }
            }
            if (!found && type_count < 20) {
                type_counts[type_count].Class = tclass;
                type_counts[type_count].Count = 1;
                type_count++;
            }
            total_units++;
        }
    }

    sprintf(dbg, "Create_Attack_Team: Scanned %d units, %d inf, %d vessels, %d aircraft",
            scanned_units, scanned_inf, scanned_vessels, scanned_aircraft);
    GlyphX_Debug_Print(dbg);

    if (total_units == 0 || type_count == 0) {
        GlyphX_Debug_Print("Create_Attack_Team: No selected units found (total_units=0)");
        return 0;
    }

    sprintf(dbg, "Create_Attack_Team: Found %d units of %d types", total_units, type_count);
    GlyphX_Debug_Print(dbg);

    /*
    ** Limit to MAX_TEAM_CLASSCOUNT (5) types - take the first ones
    */
    if (type_count > TeamTypeClass::MAX_TEAM_CLASSCOUNT) {
        type_count = TeamTypeClass::MAX_TEAM_CLASSCOUNT;
    }

    /*
    ** Create a new TeamTypeClass
    */
    TeamTypeClass * team_type = new TeamTypeClass();
    if (!team_type) {
        GlyphX_Debug_Print("Create_Attack_Team: Failed to create TeamTypeClass");
        return 0;
    }

    /*
    ** Configure the team type
    */
    strcpy((char*)team_type->IniName, "AUTOATK");
    team_type->House = house->Class->House;
    team_type->IsRoundAbout = false;
    team_type->IsSuicide = false;
    team_type->IsAutocreate = false;
    team_type->IsPrebuilt = false;
    team_type->IsReinforcable = false;
    team_type->IsTransient = true;  // Auto-delete when done
    team_type->RecruitPriority = 15;  // Highest priority
    team_type->InitNum = 0;
    team_type->MaxAllowed = 1;
    team_type->Fear = 0;

    /*
    ** Set up member types
    */
    team_type->ClassCount = type_count;
    for (int i = 0; i < type_count; i++) {
        team_type->Members[i].Class = type_counts[i].Class;
        team_type->Members[i].Quantity = type_counts[i].Count;
    }

    /*
    ** Check if we have a transport and infantry - if so, use load/unload missions
    */
    bool has_transport = false;
    bool has_infantry = false;
    for (int i = 0; i < type_count; i++) {
        if (type_counts[i].Class->Max_Passengers() > 0) {
            has_transport = true;
        }
        if (type_counts[i].Class->What_Am_I() == RTTI_INFANTRYTYPE) {
            has_infantry = true;
        }
    }

    /*
    ** Set up mission list based on whether we have transport + infantry
    */
    if (has_transport && has_infantry) {
        /*
        ** LOAD -> GATHER -> MOVE_NEAR_ENEMY -> UNLOAD -> ATTACK
        ** Infantry load onto transport, travel together, unload near enemy, then attack
        */
        GlyphX_Debug_Print("Create_Attack_Team: Transport + Infantry detected, using LOAD/UNLOAD missions");
        team_type->MissionCount = 5;
        team_type->MissionList[0].Mission = TMISSION_LOAD;
        team_type->MissionList[0].Data.Value = 0;
        team_type->MissionList[1].Mission = TMISSION_GATHER;
        team_type->MissionList[1].Data.Value = gather_cell;  // Player-specified gather point
        team_type->MissionList[2].Mission = TMISSION_MOVE_NEAR_ENEMY;
        team_type->MissionList[2].Data.Value = 0;
        team_type->MissionList[3].Mission = TMISSION_UNLOAD;
        team_type->MissionList[3].Data.Value = 0;
        team_type->MissionList[4].Mission = TMISSION_ATTACK;
        team_type->MissionList[4].Data.Quarry = QUARRY_ANYTHING;
    } else {
        /*
        ** Standard mission list: GATHER -> MOVE_NEAR_ENEMY -> ATTACK
        */
        team_type->MissionCount = 3;
        team_type->MissionList[0].Mission = TMISSION_GATHER;
        team_type->MissionList[0].Data.Value = gather_cell;  // Player-specified gather point
        team_type->MissionList[1].Mission = TMISSION_MOVE_NEAR_ENEMY;
        team_type->MissionList[1].Data.Value = 0;
        team_type->MissionList[2].Mission = TMISSION_ATTACK;
        team_type->MissionList[2].Data.Quarry = QUARRY_ANYTHING;
    }

    /*
    ** Create the team from this type
    */
    sprintf(dbg, "Create_Attack_Team: TeamType created, ClassCount=%d, MaxAllowed=%d, Number=%d",
            team_type->ClassCount, team_type->MaxAllowed, team_type->Number);
    GlyphX_Debug_Print(dbg);

    TeamClass * team = team_type->Create_One_Of();
    if (!team) {
        GlyphX_Debug_Print("Create_Attack_Team: Failed to create TeamClass (Create_One_Of returned NULL)");
        delete team_type;
        return 0;
    }

    GlyphX_Debug_Print("Create_Attack_Team: TeamClass created successfully");

    /*
    ** Force the team into active state
    */
    team->Force_Active();
    GlyphX_Debug_Print("Create_Attack_Team: Force_Active called");

    /*
    ** Add selected units to the team
    ** First remove from any existing team to allow re-assignment
    */
    int units_added = 0;
    int units_failed = 0;

    for (int index = 0; index < Units.Count(); index++) {
        UnitClass * unit = Units.Ptr(index);
        if (unit != NULL && unit->House == house && unit->Is_Selected_By_Player(house)) {
            // Remove from existing team first
            if (unit->Team.Is_Valid()) {
                unit->Team->Remove(unit);
            }
            // Clear autonomous mode but keep IsAutonomous flag so they can return to autonomous after team disbands
            // The team check (!Team.Is_Valid()) in Autonomous_Player_AI will skip units in teams
            unit->AutonomousMode = AUTOMODE_NONE;
            // Change mission to GUARD (recruitable) before adding - GUARD_AREA is not recruitable
            unit->Assign_Mission(MISSION_GUARD);
            if (team->Add(unit)) {
                units_added++;
            } else {
                units_failed++;
                char dbg2[128];
                sprintf(dbg2, "  Failed to add unit, Mission=%d, InRadio=%d", unit->Mission, unit->In_Radio_Contact());
                GlyphX_Debug_Print(dbg2);
            }
        }
    }

    for (int index = 0; index < Infantry.Count(); index++) {
        InfantryClass * inf = Infantry.Ptr(index);
        if (inf != NULL && inf->House == house && inf->Is_Selected_By_Player(house)) {
            // Remove from existing team first
            if (inf->Team.Is_Valid()) {
                inf->Team->Remove(inf);
            }
            // Clear autonomous mode but keep IsAutonomous flag so they can return to autonomous after team disbands
            inf->AutonomousMode = AUTOMODE_NONE;
            // Change mission to GUARD (recruitable) before adding - GUARD_AREA is not recruitable
            inf->Assign_Mission(MISSION_GUARD);
            if (team->Add(inf)) {
                units_added++;
            } else {
                units_failed++;
                char dbg2[128];
                sprintf(dbg2, "  Failed to add infantry, Mission=%d, InRadio=%d", inf->Mission, inf->In_Radio_Contact());
                GlyphX_Debug_Print(dbg2);
            }
        }
    }

    for (int index = 0; index < Vessels.Count(); index++) {
        VesselClass * vessel = Vessels.Ptr(index);
        if (vessel != NULL && vessel->House == house && vessel->Is_Selected_By_Player(house)) {
            // Remove from existing team first
            if (vessel->Team.Is_Valid()) {
                vessel->Team->Remove(vessel);
            }
            // Clear autonomous mode but keep IsAutonomous flag so they can return to autonomous after team disbands
            vessel->AutonomousMode = AUTOMODE_NONE;
            // Change mission to GUARD (recruitable) before adding - GUARD_AREA is not recruitable
            vessel->Assign_Mission(MISSION_GUARD);
            if (team->Add(vessel)) {
                units_added++;
            } else {
                units_failed++;
                char dbg2[128];
                sprintf(dbg2, "  Failed to add vessel, Mission=%d, InRadio=%d", vessel->Mission, vessel->In_Radio_Contact());
                GlyphX_Debug_Print(dbg2);
            }
        }
    }

    for (int index = 0; index < Aircraft.Count(); index++) {
        AircraftClass * aircraft = Aircraft.Ptr(index);
        if (aircraft != NULL && aircraft->House == house && aircraft->Is_Selected_By_Player(house)) {
            // Remove from existing team first
            if (aircraft->Team.Is_Valid()) {
                aircraft->Team->Remove(aircraft);
            }
            // Clear autonomous mode but keep IsAutonomous flag so they can return to autonomous after team disbands
            aircraft->AutonomousMode = AUTOMODE_NONE;
            // Change mission to GUARD (recruitable) before adding - GUARD_AREA is not recruitable
            aircraft->Assign_Mission(MISSION_GUARD);
            if (team->Add(aircraft)) {
                units_added++;
            } else {
                units_failed++;
                char dbg2[128];
                sprintf(dbg2, "  Failed to add aircraft, Mission=%d, InRadio=%d, Ammo=%d", aircraft->Mission, aircraft->In_Radio_Contact(), aircraft->Ammo);
                GlyphX_Debug_Print(dbg2);
            }
        }
    }

    sprintf(dbg, "Create_Attack_Team: Created team with %d units, %d failed (%d types)", units_added, units_failed, type_count);
    GlyphX_Debug_Print(dbg);

    return units_added;
}


/***********************************************************************************************
 * Process_Patrol_Unit -- Handle patrol behavior for a single unit                             *
 *                                                                                             *
 *    Processes patrol logic for one unit. Units alternate between guarding a position and    *
 *    moving to a new random position within their patrol radius.                              *
 *                                                                                             *
 * INPUT:   unit -- Pointer to the unit to process                                             *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/2026 - Created for Autonomous Player Mod                                               *
 *=============================================================================================*/
static void Process_Patrol_Unit(FootClass* unit)
{
    if (!unit || !unit->IsActive) return;

    /*
    ** First check for threats - patrol units should attack enemies they see
    */
    if (unit->Is_Weapon_Equipped() && !Target_Legal(unit->TarCom)) {
        TARGET threat = unit->Greatest_Threat(THREAT_AREA | THREAT_BUILDINGS | THREAT_VEHICLES | THREAT_INFANTRY);
        if (Target_Legal(threat)) {
            unit->Assign_Target(threat);
            unit->Assign_Mission(MISSION_ATTACK);
            return;  // Skip patrol logic while attacking
        }
    }

    /*
    ** If currently attacking, let it finish
    */
    if (unit->Mission == MISSION_ATTACK && Target_Legal(unit->TarCom)) {
        return;
    }

    switch (unit->PatrolPhase) {
        case PATROL_PHASE_GUARDING:
            /*
            ** Decrement the guard timer
            */
            if (unit->PatrolTimer > 0) {
                unit->PatrolTimer--;
            } else {
                /*
                ** Timer expired - pick a new random patrol point and move there
                */
                int home_x = Coord_X(unit->PatrolHome);
                int home_y = Coord_Y(unit->PatrolHome);

                /*
                ** Generate random offset within patrol radius
                */
                int offset_x = (Random_Pick(0, PATROL_RADIUS * 2) - PATROL_RADIUS) * CELL_LEPTON_W;
                int offset_y = (Random_Pick(0, PATROL_RADIUS * 2) - PATROL_RADIUS) * CELL_LEPTON_H;

                int new_x = home_x + offset_x;
                int new_y = home_y + offset_y;

                /*
                ** Clamp to valid map coordinates
                */
                if (new_x < 0) new_x = 0;
                if (new_y < 0) new_y = 0;

                COORDINATE new_coord = XY_Coord(new_x, new_y);
                CELL new_cell = Coord_Cell(new_coord);

                /*
                ** Check if the cell is valid and passable for this unit
                */
                if (Map.In_Radar(new_cell)) {
                    /*
                    ** Start moving to new patrol point
                    */
                    unit->PatrolPhase = PATROL_PHASE_MOVING;
                    unit->Assign_Destination(::As_Target(new_coord));
                    unit->Assign_Mission(MISSION_MOVE);
                } else {
                    /*
                    ** Invalid cell, try again next frame with a shorter timer
                    */
                    unit->PatrolTimer = TICKS_PER_SECOND;
                }
            }
            break;

        case PATROL_PHASE_MOVING:
            /*
            ** Check if we've reached our destination or are no longer moving
            ** Note: Aircraft should never reach this code path as they don't patrol
            */
            if (unit->Mission != MISSION_MOVE || !Target_Legal(unit->NavCom)) {
                /*
                ** Arrived at patrol point - start guarding
                */
                unit->PatrolPhase = PATROL_PHASE_GUARDING;
                unit->PatrolTimer = PATROL_GUARD_TIME + Random_Pick(0, TICKS_PER_SECOND * 4);
                unit->ArchiveTarget = ::As_Target(unit->Coord);
                unit->Assign_Mission(MISSION_GUARD);
            }
            break;
    }
}


/*
** Static rally point for attack grouping - persists between frames
*/
static COORDINATE AttackRallyPoint = 0;
static bool RallyPointSet = false;


/***********************************************************************************************
 * Process_Attack_Units -- Handle coordinated attack behavior for all attack-mode units        *
 *                                                                                             *
 *    Coordinates units in AUTOMODE_ATTACK through a three-phase state machine:               *
 *      1. GROUPING - Units move to a fixed rally point and wait for everyone                 *
 *      2. APPROACHING - Group moves toward enemy base together                               *
 *      3. ATTACKING - Group engages enemies                                                  *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   none                                                                            *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/2026 - Created for Autonomous Player Mod                                               *
 *=============================================================================================*/
static void Process_Attack_Units(void)
{
    /*
    ** Collect all units in attack mode
    */
    DynamicVectorClass<FootClass*> attackers;
    int count = 0;

    // Process ground units - skip units in teams (team system handles them)
    for (int i = 0; i < Units.Count(); i++) {
        UnitClass* unit = Units.Ptr(i);
        if (unit && unit->IsActive && unit->IsAutonomous && !unit->Team.Is_Valid() &&
            unit->AutonomousMode == AUTOMODE_ATTACK) {
            attackers.Add(unit);
            count++;
        }
    }

    // Process infantry - skip units in teams
    for (int i = 0; i < Infantry.Count(); i++) {
        InfantryClass* inf = Infantry.Ptr(i);
        if (inf && inf->IsActive && inf->IsAutonomous && !inf->Team.Is_Valid() &&
            inf->AutonomousMode == AUTOMODE_ATTACK) {
            attackers.Add(inf);
            count++;
        }
    }

    // Process aircraft - skip units in teams
    for (int i = 0; i < Aircraft.Count(); i++) {
        AircraftClass* air = Aircraft.Ptr(i);
        if (air && air->IsActive && air->IsAutonomous && !air->Team.Is_Valid() &&
            air->AutonomousMode == AUTOMODE_ATTACK) {
            attackers.Add(air);
            count++;
        }
    }

    // Process vessels - skip units in teams
    for (int i = 0; i < Vessels.Count(); i++) {
        VesselClass* ship = Vessels.Ptr(i);
        if (ship && ship->IsActive && ship->IsAutonomous && !ship->Team.Is_Valid() &&
            ship->AutonomousMode == AUTOMODE_ATTACK) {
            attackers.Add(ship);
            count++;
        }
    }

    /*
    ** If no attackers, reset rally point and return
    */
    if (count == 0) {
        RallyPointSet = false;
        AttackRallyPoint = 0;
        return;
    }

    /*
    ** Determine current group phase - use the first unit's phase as reference
    */
    AttackPhaseType current_phase = attackers[0]->AttackPhase;

    /*
    ** PHASE 1: GROUPING - Move to rally point and wait for everyone
    */
    if (current_phase == ATTACK_PHASE_GROUPING) {
        /*
        ** Set rally point if not already set (use first unit's position)
        */
        if (!RallyPointSet) {
            AttackRallyPoint = attackers[0]->Coord;
            RallyPointSet = true;
        }

        /*
        ** Check if all units have arrived at rally point
        */
        bool all_arrived = true;
        for (int i = 0; i < attackers.Count(); i++) {
            FootClass* unit = attackers[i];

            /*
            ** Unit is "arrived" if within grouping range
            */
            bool at_rally = (::Distance(unit->Coord, AttackRallyPoint) <= GROUPING_RANGE);

            if (!at_rally) {
                all_arrived = false;
                /*
                ** Send unit to rally point if not already moving there
                */
                if (unit->Mission != MISSION_MOVE || !Target_Legal(unit->NavCom)) {
                    unit->Assign_Destination(::As_Target(AttackRallyPoint));
                    unit->Assign_Mission(MISSION_MOVE);
                }
            } else {
                /*
                ** Unit at rally - make sure it stays put (stop if moving/fighting)
                */
                if (unit->Mission != MISSION_GUARD) {
                    unit->Assign_Mission(MISSION_GUARD);
                }
            }
        }

        /*
        ** All units arrived - transition to approaching phase
        */
        if (all_arrived) {
            for (int i = 0; i < attackers.Count(); i++) {
                attackers[i]->AttackPhase = ATTACK_PHASE_APPROACHING;
            }
            RallyPointSet = false;  // Reset for next grouping
        }
    }
    /*
    ** PHASE 2: APPROACHING - Move to rally point near enemy base
    */
    else if (current_phase == ATTACK_PHASE_APPROACHING) {
        COORDINATE enemy_base = PlayerPtr->Find_Enemy_Base_Center();

        if (!enemy_base) {
            /*
            ** No enemy base found - switch to hunt mode
            */
            GlyphX_Debug_Print("APPROACHING: No enemy base found, switching to HUNT");
            for (int i = 0; i < attackers.Count(); i++) {
                attackers[i]->AttackPhase = ATTACK_PHASE_ATTACKING;
                attackers[i]->Assign_Mission(MISSION_HUNT);
            }
            return;
        }

        /*
        ** Calculate group center
        */
        long total_x = 0, total_y = 0;
        for (int i = 0; i < attackers.Count(); i++) {
            total_x += Coord_X(attackers[i]->Coord);
            total_y += Coord_Y(attackers[i]->Coord);
        }
        COORDINATE group_center = XY_Coord(total_x / count, total_y / count);

        /*
        ** Calculate rally point near enemy base (APPROACH_RANGE away from base center)
        ** in the direction of our group
        */
        if (!RallyPointSet) {
            int enemy_x = Coord_X(enemy_base);
            int enemy_y = Coord_Y(enemy_base);
            int group_x = Coord_X(group_center);
            int group_y = Coord_Y(group_center);

            /*
            ** Get direction from enemy base toward our group
            */
            int dx = group_x - enemy_x;
            int dy = group_y - enemy_y;
            int dist = ::Distance(group_center, enemy_base);

            if (dist > 0) {
                /*
                ** Place rally point APPROACH_RANGE away from enemy base toward our group
                */
                int rally_x = enemy_x + (dx * APPROACH_RANGE) / dist;
                int rally_y = enemy_y + (dy * APPROACH_RANGE) / dist;
                AttackRallyPoint = XY_Coord(rally_x, rally_y);
            } else {
                AttackRallyPoint = enemy_base;
            }
            RallyPointSet = true;

            char buf[128];
            sprintf(buf, "APPROACHING: Rally point set, dist to enemy=%d", dist / CELL_LEPTON_W);
            GlyphX_Debug_Print(buf);
        }

        /*
        ** Check if group has arrived at rally point near enemy base
        */
        int dist_to_rally = ::Distance(group_center, AttackRallyPoint);
        if (dist_to_rally <= GROUPING_RANGE) {
            /*
            ** Arrived - transition to regrouping phase
            */
            GlyphX_Debug_Print("APPROACHING: Arrived at rally, switching to REGROUPING");
            for (int i = 0; i < attackers.Count(); i++) {
                attackers[i]->AttackPhase = ATTACK_PHASE_REGROUPING;
            }
        } else {
            /*
            ** Check if all units have arrived at current waypoint or are stuck
            */
            bool all_stopped = true;
            for (int i = 0; i < attackers.Count(); i++) {
                if (attackers[i]->Mission == MISSION_MOVE && Target_Legal(attackers[i]->NavCom)) {
                    all_stopped = false;
                    break;
                }
            }

            /*
            ** If all stopped (arrived or stuck), issue move order to rally point
            */
            if (all_stopped) {
                for (int i = 0; i < attackers.Count(); i++) {
                    attackers[i]->Assign_Destination(::As_Target(AttackRallyPoint));
                    attackers[i]->Assign_Mission(MISSION_MOVE);
                }
            }
        }
    }
    /*
    ** PHASE 3: REGROUPING - Gather near enemy base before attacking
    */
    else if (current_phase == ATTACK_PHASE_REGROUPING) {
        /*
        ** Check if all units have arrived at rally point near enemy base
        */
        bool all_arrived = true;
        for (int i = 0; i < attackers.Count(); i++) {
            FootClass* unit = attackers[i];

            bool at_rally = (::Distance(unit->Coord, AttackRallyPoint) <= GROUPING_RANGE);

            if (!at_rally) {
                all_arrived = false;
                /*
                ** Send unit to rally point if not already moving there
                */
                if (unit->Mission != MISSION_MOVE || !Target_Legal(unit->NavCom)) {
                    unit->Assign_Destination(::As_Target(AttackRallyPoint));
                    unit->Assign_Mission(MISSION_MOVE);
                }
            } else {
                /*
                ** Unit at rally - make sure it stays put (stop if moving/fighting)
                */
                if (unit->Mission != MISSION_GUARD) {
                    unit->Assign_Mission(MISSION_GUARD);
                }
            }
        }

        /*
        ** All units arrived near enemy base - transition to attack phase
        */
        if (all_arrived) {
            for (int i = 0; i < attackers.Count(); i++) {
                attackers[i]->AttackPhase = ATTACK_PHASE_ATTACKING;
            }
            RallyPointSet = false;
        }
    }
    /*
    ** PHASE 4: ATTACKING - Coordinated attack with shared targets
    */
    else if (current_phase == ATTACK_PHASE_ATTACKING) {
        /*
        ** Find or validate shared attack target
        ** Use static to persist between frames
        */
        static TARGET SharedAttackTarget = TARGET_NONE;

        /*
        ** Check if current target is still valid
        */
        if (!Target_Legal(SharedAttackTarget)) {
            SharedAttackTarget = TARGET_NONE;

            /*
            ** Find a leader unit with a weapon to pick a target
            */
            FootClass* leader = NULL;
            for (int i = 0; i < attackers.Count(); i++) {
                if (attackers[i]->Is_Weapon_Equipped()) {
                    leader = attackers[i];
                    break;
                }
            }

            /*
            ** Have the leader pick a target - include buildings, vehicles, infantry
            */
            if (leader != NULL) {
                SharedAttackTarget = leader->Greatest_Threat(THREAT_NORMAL | THREAT_BUILDINGS | THREAT_VEHICLES | THREAT_INFANTRY);
            }
        }

        /*
        ** Assign all units to attack the shared target
        */
        for (int i = 0; i < attackers.Count(); i++) {
            FootClass* unit = attackers[i];

            if (Target_Legal(SharedAttackTarget)) {
                /*
                ** Attack shared target like teams do
                */
                if (unit->Mission != MISSION_ATTACK || unit->TarCom != SharedAttackTarget) {
                    unit->Assign_Mission(MISSION_ATTACK);
                    unit->Assign_Target(SharedAttackTarget);
                }
            } else {
                /*
                ** No valid target - hunt for enemies
                */
                if (unit->Mission != MISSION_ATTACK && unit->Mission != MISSION_HUNT) {
                    unit->Assign_Mission(MISSION_HUNT);
                }
            }
        }
    }
}


/***********************************************************************************************
 * Autonomous_Player_AI -- Process autonomous player units each frame                          *
 *                                                                                             *
 *    Main entry point for autonomous player AI. Called each game frame to process all        *
 *    autonomous player units based on their current mode.                                     *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   Called every frame, must be efficient                                           *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   01/2026 - Created for Autonomous Player Mod                                               *
 *=============================================================================================*/
void Autonomous_Player_AI(void)
{
    /*
    ** Process patrol units - iterate through all unit types
    ** Check for any unit in autonomous mode (don't filter by house here)
    */
    static int debug_counter = 0;
    static int call_count = 0;
    int patrol_count = 0;
    int attack_count = 0;

    call_count++;

    // Debug: log every 5 seconds to confirm AI is running
    if ((call_count % (TICKS_PER_SECOND * 5)) == 0) {
        char buf[128];
        sprintf(buf, "Autonomous_Player_AI called %d times, Units=%d Infantry=%d",
                call_count, Units.Count(), Infantry.Count());
        GlyphX_Debug_Print(buf);
    }

    // Process ground units
    // Skip units in teams - let the team system handle them
    for (int i = 0; i < Units.Count(); i++) {
        UnitClass* unit = Units.Ptr(i);
        if (unit && unit->IsActive && unit->IsAutonomous && !unit->Team.Is_Valid()) {
            if (unit->AutonomousMode == AUTOMODE_PATROL) {
                patrol_count++;
                Process_Patrol_Unit(unit);
            } else if (unit->AutonomousMode == AUTOMODE_ATTACK) {
                attack_count++;
            }
        }
    }

    // Process infantry
    // Skip units in teams - let the team system handle them
    for (int i = 0; i < Infantry.Count(); i++) {
        InfantryClass* inf = Infantry.Ptr(i);
        if (inf && inf->IsActive && inf->IsAutonomous && !inf->Team.Is_Valid()) {
            if (inf->AutonomousMode == AUTOMODE_PATROL) {
                patrol_count++;
                Process_Patrol_Unit(inf);
            } else if (inf->AutonomousMode == AUTOMODE_ATTACK) {
                attack_count++;
            }
        }
    }

    // Process aircraft - special handling (aircraft can't patrol like ground units)
    // Skip units in teams - let the team system handle them
    for (int i = 0; i < Aircraft.Count(); i++) {
        AircraftClass* air = Aircraft.Ptr(i);
        if (air && air->IsActive && air->IsAutonomous && !air->Team.Is_Valid()) {
            if (air->AutonomousMode == AUTOMODE_PATROL) {
                patrol_count++;

                /*
                ** Aircraft "patrol" means: stay on helipad, scan for threats, attack when found
                ** Aircraft cannot move around on the ground like infantry/vehicles
                ** They stay defensive until a threat appears, then scramble to attack
                */

                /*
                ** PRIORITY 1: If out of ammo, stop attacking and return to helipad to rearm
                ** This must be checked first to interrupt any ongoing attack
                */
                if (air->Is_Weapon_Equipped() && air->Ammo == 0) {
                    /*
                    ** Clear any attack target - can't attack without ammo
                    */
                    if (Target_Legal(air->TarCom)) {
                        air->Assign_Target(TARGET_NONE);
                    }

                    /*
                    ** Return to helipad if not already doing so
                    */
                    if (!air->In_Radio_Contact() && air->Mission != MISSION_ENTER) {
                        BuildingClass * helipad = air->Find_Docking_Bay(STRUCT_HELIPAD, false);
                        if (helipad != NULL) {
                            air->Assign_Destination(helipad->As_Target());
                            air->Assign_Mission(MISSION_ENTER);
                        }
                    }
                    continue;  // Skip other logic until rearmed
                }

                /*
                ** PRIORITY 2: If aircraft is damaged, look for repair bay
                */
                if (air->Health_Ratio() <= Rule.ConditionYellow && air->House->Available_Money() >= 100) {
                    if (!air->In_Radio_Contact() && air->Mission != MISSION_ENTER) {
                        BuildingClass * repair = air->Find_Docking_Bay(STRUCT_REPAIR, true);
                        if (repair != NULL) {
                            air->Assign_Destination(repair->As_Target());
                            air->Assign_Mission(MISSION_ENTER);
                            continue;
                        }
                    }
                }

                /*
                ** PRIORITY 3: If aircraft has weapon and ammo, scan for threats and attack
                */
                if (air->Is_Weapon_Equipped() && air->Ammo > 0) {
                    if (!Target_Legal(air->TarCom)) {
                        /*
                        ** Scan for nearby threats - include buildings, vehicles, infantry
                        */
                        TARGET threat = air->Greatest_Threat(THREAT_AREA | THREAT_BUILDINGS | THREAT_VEHICLES | THREAT_INFANTRY);
                        if (Target_Legal(threat)) {
                            air->Assign_Target(threat);
                            air->Assign_Mission(MISSION_ATTACK);
                        }
                    } else if (air->Mission != MISSION_ATTACK) {
                        /*
                        ** Has target but not attacking - start attack
                        */
                        air->Assign_Mission(MISSION_ATTACK);
                    }
                }

            } else if (air->AutonomousMode == AUTOMODE_ATTACK) {
                attack_count++;
            }
        }
    }

    // Process vessels
    // Skip units in teams - let the team system handle them
    for (int i = 0; i < Vessels.Count(); i++) {
        VesselClass* ship = Vessels.Ptr(i);
        if (ship && ship->IsActive && ship->IsAutonomous && !ship->Team.Is_Valid()) {
            if (ship->AutonomousMode == AUTOMODE_PATROL) {
                patrol_count++;
                Process_Patrol_Unit(ship);
            } else if (ship->AutonomousMode == AUTOMODE_ATTACK) {
                attack_count++;
            }
        }
    }

    // Debug output every 2 seconds
    debug_counter++;
    if (debug_counter >= TICKS_PER_SECOND * 2) {
        debug_counter = 0;
        if (patrol_count > 0 || attack_count > 0) {
            char buf[128];
            sprintf(buf, "AutoAI: %d patrol, %d attack units", patrol_count, attack_count);
            GlyphX_Debug_Print(buf);
        }
    }

    /*
    ** Process attack units
    */
    Process_Attack_Units();

    /*
    ** Process auto-team creation for units from autonomous factories
    */
    Process_Factory_Teams();
}


/***********************************************************************************************
 * Process_Factory_Teams -- Automatically create attack teams from factory-produced units      *
 *                                                                                             *
 *    This routine collects units that were created by autonomous factories and automatically *
 *    forms them into attack teams when enough units have gathered. Uses AI-like timing and   *
 *    team composition rules.                                                                  *
 *                                                                                             *
 * INPUT:   none                                                                               *
 *                                                                                             *
 * OUTPUT:  none                                                                               *
 *                                                                                             *
 * WARNINGS:   Called every frame from Autonomous_Player_AI                                    *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   02/2026 - Created for Autonomous Player Mod                                               *
 *=============================================================================================*/

/*
** Constants for automatic team creation
** Team sizes are now calculated dynamically based on available units
*/
#define AUTO_TEAM_MIN_SIZE      3       // Absolute minimum units to form a team
#define AUTO_TEAM_DELAY         (TICKS_PER_SECOND * 30)  // Check every 30 seconds for team formation
#define AUTO_TEAM_FORCE_DELAY   (TICKS_PER_MINUTE * 1)   // Force team creation after 1 minute if minimum met

static void Process_Factory_Teams(void)
{
    /*
    ** Static timers for team creation
    */
    static int TeamCheckTimer = AUTO_TEAM_DELAY;
    static int ForceTeamTimer = AUTO_TEAM_FORCE_DELAY;

    /*
    ** Only process for player house
    */
    if (!PlayerPtr) return;

    /*
    ** Decrement timers
    */
    if (TeamCheckTimer > 0) TeamCheckTimer--;
    if (ForceTeamTimer > 0) ForceTeamTimer--;

    /*
    ** Debug: count all units to see what flags they have
    */
    static int debug_timer = 0;
    debug_timer++;
    bool show_debug = (debug_timer >= TICKS_PER_SECOND * 5);  // Every 5 seconds
    if (show_debug) debug_timer = 0;

    int total_player_units = 0;
    int units_from_factory = 0;
    int units_in_patrol = 0;
    int units_not_in_team = 0;

    for (int i = 0; i < Units.Count(); i++) {
        UnitClass* unit = Units.Ptr(i);
        if (unit && unit->IsActive && unit->House->IsPlayerControl) {
            total_player_units++;
            if (unit->IsFromAutonomousFactory) units_from_factory++;
            if (unit->AutonomousMode == AUTOMODE_PATROL) units_in_patrol++;
            if (!unit->Team.Is_Valid()) units_not_in_team++;
        }
    }

    if (show_debug && total_player_units > 0) {
        char dbg[256];
        sprintf(dbg, "FactoryTeams DEBUG: %d player units, %d fromFactory, %d patrol, %d noTeam, timers=%d/%d",
                total_player_units, units_from_factory, units_in_patrol, units_not_in_team,
                TeamCheckTimer / TICKS_PER_SECOND, ForceTeamTimer / TICKS_PER_SECOND);
        GlyphX_Debug_Print(dbg);
    }

    /*
    ** Count available factory-produced units (not in teams, not currently attacking)
    */
    int available_count = 0;
    bool has_transport = false;
    bool has_infantry = false;
    COORDINATE avg_coord = 0;
    long total_x = 0, total_y = 0;
    HouseClass * team_house = NULL;  // Store the house from first found unit

    // Count units (exclude harvesters and MCVs)
    for (int i = 0; i < Units.Count(); i++) {
        UnitClass* unit = Units.Ptr(i);
        if (unit && unit->IsActive && unit->House->IsPlayerControl) {
            // Debug: show unit flags
            if (show_debug && unit->IsFromAutonomousFactory) {
                char dbg2[256];
                sprintf(dbg2, "  Unit %s: fromFactory=%d mode=%d inTeam=%d",
                        unit->Class->IniName,
                        unit->IsFromAutonomousFactory ? 1 : 0,
                        (int)unit->AutonomousMode,
                        unit->Team.Is_Valid() ? 1 : 0);
                GlyphX_Debug_Print(dbg2);
            }
            if (unit->IsFromAutonomousFactory && !unit->Team.Is_Valid() &&
                unit->AutonomousMode == AUTOMODE_PATROL &&
                *unit != UNIT_HARVESTER && *unit != UNIT_MCV) {
                if (!team_house) team_house = unit->House;
                available_count++;
                total_x += Coord_X(unit->Coord);
                total_y += Coord_Y(unit->Coord);
                if (unit->Class->Max_Passengers() > 0) {
                    has_transport = true;
                }
            }
        }
    }

    // Count infantry
    for (int i = 0; i < Infantry.Count(); i++) {
        InfantryClass* inf = Infantry.Ptr(i);
        if (inf && inf->IsActive && inf->House->IsPlayerControl &&
            inf->IsFromAutonomousFactory && !inf->Team.Is_Valid() &&
            inf->AutonomousMode == AUTOMODE_PATROL) {
            if (!team_house) team_house = inf->House;
            available_count++;
            total_x += Coord_X(inf->Coord);
            total_y += Coord_Y(inf->Coord);
            has_infantry = true;
        }
    }

    // Count vessels
    for (int i = 0; i < Vessels.Count(); i++) {
        VesselClass* ship = Vessels.Ptr(i);
        if (ship && ship->IsActive && ship->House->IsPlayerControl &&
            ship->IsFromAutonomousFactory && !ship->Team.Is_Valid() &&
            ship->AutonomousMode == AUTOMODE_PATROL) {
            if (!team_house) team_house = ship->House;
            available_count++;
            total_x += Coord_X(ship->Coord);
            total_y += Coord_Y(ship->Coord);
        }
    }

    // Count aircraft
    for (int i = 0; i < Aircraft.Count(); i++) {
        AircraftClass* air = Aircraft.Ptr(i);
        if (air && air->IsActive && air->House->IsPlayerControl &&
            air->IsFromAutonomousFactory && !air->Team.Is_Valid() &&
            air->AutonomousMode == AUTOMODE_PATROL) {
            if (!team_house) team_house = air->House;
            available_count++;
            total_x += Coord_X(air->Coord);
            total_y += Coord_Y(air->Coord);
        }
    }

    /*
    ** Calculate dynamic team sizes based on available units
    ** - Ideal: 40% of available, minimum 4
    ** - Max: 60% of available, minimum 8
    ** This scales team sizes with the player's army
    */
    int dynamic_ideal_size = (available_count * 40) / 100;
    if (dynamic_ideal_size < 4) dynamic_ideal_size = 4;

    int dynamic_max_size = (available_count * 60) / 100;
    if (dynamic_max_size < 8) dynamic_max_size = 8;

    /*
    ** Debug output for available count
    */
    if (show_debug) {
        char dbg[256];
        sprintf(dbg, "FactoryTeams: available=%d (min=%d, ideal=%d, max=%d)",
                available_count, AUTO_TEAM_MIN_SIZE, dynamic_ideal_size, dynamic_max_size);
        GlyphX_Debug_Print(dbg);
    }

    /*
    ** Check if we should create a team
    */
    bool should_create = false;
    int team_size_to_use = 0;

    if (available_count >= dynamic_max_size) {
        // Max size reached, must create immediately
        should_create = true;
        team_size_to_use = dynamic_max_size;
        GlyphX_Debug_Print("Process_Factory_Teams: Max team size reached");
    } else if (available_count >= dynamic_ideal_size && TeamCheckTimer == 0) {
        // Ideal size reached and timer expired
        should_create = true;
        team_size_to_use = dynamic_ideal_size;
        GlyphX_Debug_Print("Process_Factory_Teams: Ideal team size reached");
    } else if (available_count >= AUTO_TEAM_MIN_SIZE && ForceTeamTimer == 0) {
        // Force creation after timeout with whatever we have
        should_create = true;
        team_size_to_use = available_count;
        GlyphX_Debug_Print("Process_Factory_Teams: Force timer expired, creating team");
    }

    if (!should_create) return;
    if (!team_house) return;  // Safety check

    /*
    ** Calculate gather point (average position of available units)
    */
    if (available_count > 0) {
        avg_coord = XY_Coord(total_x / available_count, total_y / available_count);
    }
    CELL gather_cell = Coord_Cell(avg_coord);

    /*
    ** Create the attack team using similar logic to Create_Attack_Team
    ** but for factory-produced units instead of selected units
    */
    char dbg[256];
    sprintf(dbg, "Process_Factory_Teams: Creating team with %d units at cell %d", available_count, gather_cell);
    GlyphX_Debug_Print(dbg);

    /*
    ** Count unit types for the team
    */
    struct UnitTypeCount {
        TechnoTypeClass const * Class;
        int Count;
    };
    UnitTypeCount type_counts[20];
    int type_count = 0;
    int total_units = 0;

    // Process ground units (exclude harvesters and MCVs)
    for (int index = 0; index < Units.Count(); index++) {
        UnitClass * unit = Units.Ptr(index);
        if (unit != NULL && unit->House->IsPlayerControl && unit->IsFromAutonomousFactory &&
            !unit->Team.Is_Valid() && unit->AutonomousMode == AUTOMODE_PATROL &&
            *unit != UNIT_HARVESTER && *unit != UNIT_MCV) {
            TechnoTypeClass const * tclass = (TechnoTypeClass const *)&unit->Class_Of();
            bool found = false;
            for (int t = 0; t < type_count; t++) {
                if (type_counts[t].Class == tclass) {
                    type_counts[t].Count++;
                    found = true;
                    break;
                }
            }
            if (!found && type_count < 20) {
                type_counts[type_count].Class = tclass;
                type_counts[type_count].Count = 1;
                type_count++;
            }
            total_units++;
            if (total_units >= team_size_to_use) break;
        }
    }

    // Process infantry
    if (total_units < team_size_to_use) {
        for (int index = 0; index < Infantry.Count(); index++) {
            InfantryClass * inf = Infantry.Ptr(index);
            if (inf != NULL && inf->House->IsPlayerControl && inf->IsFromAutonomousFactory &&
                !inf->Team.Is_Valid() && inf->AutonomousMode == AUTOMODE_PATROL) {
                TechnoTypeClass const * tclass = (TechnoTypeClass const *)&inf->Class_Of();
                bool found = false;
                for (int t = 0; t < type_count; t++) {
                    if (type_counts[t].Class == tclass) {
                        type_counts[t].Count++;
                        found = true;
                        break;
                    }
                }
                if (!found && type_count < 20) {
                    type_counts[type_count].Class = tclass;
                    type_counts[type_count].Count = 1;
                    type_count++;
                }
                total_units++;
                if (total_units >= team_size_to_use) break;
            }
        }
    }

    // Process vessels
    if (total_units < team_size_to_use) {
        for (int index = 0; index < Vessels.Count(); index++) {
            VesselClass * vessel = Vessels.Ptr(index);
            if (vessel != NULL && vessel->House->IsPlayerControl && vessel->IsFromAutonomousFactory &&
                !vessel->Team.Is_Valid() && vessel->AutonomousMode == AUTOMODE_PATROL) {
                TechnoTypeClass const * tclass = (TechnoTypeClass const *)&vessel->Class_Of();
                bool found = false;
                for (int t = 0; t < type_count; t++) {
                    if (type_counts[t].Class == tclass) {
                        type_counts[t].Count++;
                        found = true;
                        break;
                    }
                }
                if (!found && type_count < 20) {
                    type_counts[type_count].Class = tclass;
                    type_counts[type_count].Count = 1;
                    type_count++;
                }
                total_units++;
                if (total_units >= team_size_to_use) break;
            }
        }
    }

    // Process aircraft
    if (total_units < team_size_to_use) {
        for (int index = 0; index < Aircraft.Count(); index++) {
            AircraftClass * aircraft = Aircraft.Ptr(index);
            if (aircraft != NULL && aircraft->House->IsPlayerControl && aircraft->IsFromAutonomousFactory &&
                !aircraft->Team.Is_Valid() && aircraft->AutonomousMode == AUTOMODE_PATROL) {
                TechnoTypeClass const * tclass = (TechnoTypeClass const *)&aircraft->Class_Of();
                bool found = false;
                for (int t = 0; t < type_count; t++) {
                    if (type_counts[t].Class == tclass) {
                        type_counts[t].Count++;
                        found = true;
                        break;
                    }
                }
                if (!found && type_count < 20) {
                    type_counts[type_count].Class = tclass;
                    type_counts[type_count].Count = 1;
                    type_count++;
                }
                total_units++;
                if (total_units >= team_size_to_use) break;
            }
        }
    }

    if (total_units == 0 || type_count == 0) {
        return;
    }

    /*
    ** Limit to MAX_TEAM_CLASSCOUNT (5) types
    */
    if (type_count > TeamTypeClass::MAX_TEAM_CLASSCOUNT) {
        type_count = TeamTypeClass::MAX_TEAM_CLASSCOUNT;
    }

    /*
    ** Create a new TeamTypeClass
    */
    TeamTypeClass * team_type = new TeamTypeClass();
    if (!team_type) {
        GlyphX_Debug_Print("Process_Factory_Teams: Failed to create TeamTypeClass");
        return;
    }

    /*
    ** Configure the team type
    */
    strcpy((char*)team_type->IniName, "AUTOFAC");
    team_type->House = team_house->Class->House;
    team_type->IsRoundAbout = false;
    team_type->IsSuicide = false;
    team_type->IsAutocreate = false;
    team_type->IsPrebuilt = false;
    team_type->IsReinforcable = false;
    team_type->IsTransient = true;
    team_type->RecruitPriority = 15;
    team_type->InitNum = 0;
    team_type->MaxAllowed = 1;
    team_type->Fear = 0;

    /*
    ** Set up member types
    */
    team_type->ClassCount = type_count;
    for (int i = 0; i < type_count; i++) {
        team_type->Members[i].Class = type_counts[i].Class;
        team_type->Members[i].Quantity = type_counts[i].Count;
    }

    /*
    ** Check if we have transport + infantry for load/unload missions
    */
    bool team_has_transport = false;
    bool team_has_infantry = false;
    for (int i = 0; i < type_count; i++) {
        if (type_counts[i].Class->Max_Passengers() > 0) {
            team_has_transport = true;
        }
        if (type_counts[i].Class->What_Am_I() == RTTI_INFANTRYTYPE) {
            team_has_infantry = true;
        }
    }

    /*
    ** Set up mission list based on team composition
    */
    if (team_has_transport && team_has_infantry) {
        GlyphX_Debug_Print("Process_Factory_Teams: Transport + Infantry detected, using LOAD/UNLOAD");
        team_type->MissionCount = 5;
        team_type->MissionList[0].Mission = TMISSION_LOAD;
        team_type->MissionList[0].Data.Value = 0;
        team_type->MissionList[1].Mission = TMISSION_GATHER;
        team_type->MissionList[1].Data.Value = gather_cell;
        team_type->MissionList[2].Mission = TMISSION_MOVE_NEAR_ENEMY;
        team_type->MissionList[2].Data.Value = 0;
        team_type->MissionList[3].Mission = TMISSION_UNLOAD;
        team_type->MissionList[3].Data.Value = 0;
        team_type->MissionList[4].Mission = TMISSION_ATTACK;
        team_type->MissionList[4].Data.Quarry = QUARRY_ANYTHING;
    } else {
        team_type->MissionCount = 3;
        team_type->MissionList[0].Mission = TMISSION_GATHER;
        team_type->MissionList[0].Data.Value = gather_cell;
        team_type->MissionList[1].Mission = TMISSION_MOVE_NEAR_ENEMY;
        team_type->MissionList[1].Data.Value = 0;
        team_type->MissionList[2].Mission = TMISSION_ATTACK;
        team_type->MissionList[2].Data.Quarry = QUARRY_ANYTHING;
    }

    /*
    ** Create the team
    */
    TeamClass * team = team_type->Create_One_Of();
    if (!team) {
        GlyphX_Debug_Print("Process_Factory_Teams: Failed to create TeamClass");
        delete team_type;
        return;
    }

    team->Force_Active();

    /*
    ** Add factory-produced units to the team
    */
    int units_added = 0;

    for (int index = 0; index < Units.Count() && units_added < team_size_to_use; index++) {
        UnitClass * unit = Units.Ptr(index);
        if (unit != NULL && unit->House->IsPlayerControl && unit->IsFromAutonomousFactory &&
            !unit->Team.Is_Valid() && unit->AutonomousMode == AUTOMODE_PATROL &&
            *unit != UNIT_HARVESTER && *unit != UNIT_MCV) {
            unit->AutonomousMode = AUTOMODE_NONE;
            unit->IsFromAutonomousFactory = false;  // Clear flag so they don't get re-recruited
            unit->Assign_Mission(MISSION_GUARD);
            if (team->Add(unit)) {
                units_added++;
            }
        }
    }

    for (int index = 0; index < Infantry.Count() && units_added < team_size_to_use; index++) {
        InfantryClass * inf = Infantry.Ptr(index);
        if (inf != NULL && inf->House->IsPlayerControl && inf->IsFromAutonomousFactory &&
            !inf->Team.Is_Valid() && inf->AutonomousMode == AUTOMODE_PATROL) {
            inf->AutonomousMode = AUTOMODE_NONE;
            inf->IsFromAutonomousFactory = false;
            inf->Assign_Mission(MISSION_GUARD);
            if (team->Add(inf)) {
                units_added++;
            }
        }
    }

    for (int index = 0; index < Vessels.Count() && units_added < team_size_to_use; index++) {
        VesselClass * vessel = Vessels.Ptr(index);
        if (vessel != NULL && vessel->House->IsPlayerControl && vessel->IsFromAutonomousFactory &&
            !vessel->Team.Is_Valid() && vessel->AutonomousMode == AUTOMODE_PATROL) {
            vessel->AutonomousMode = AUTOMODE_NONE;
            vessel->IsFromAutonomousFactory = false;
            vessel->Assign_Mission(MISSION_GUARD);
            if (team->Add(vessel)) {
                units_added++;
            }
        }
    }

    for (int index = 0; index < Aircraft.Count() && units_added < team_size_to_use; index++) {
        AircraftClass * aircraft = Aircraft.Ptr(index);
        if (aircraft != NULL && aircraft->House->IsPlayerControl && aircraft->IsFromAutonomousFactory &&
            !aircraft->Team.Is_Valid() && aircraft->AutonomousMode == AUTOMODE_PATROL) {
            aircraft->AutonomousMode = AUTOMODE_NONE;
            aircraft->IsFromAutonomousFactory = false;
            aircraft->Assign_Mission(MISSION_GUARD);
            if (team->Add(aircraft)) {
                units_added++;
            }
        }
    }

    sprintf(dbg, "Process_Factory_Teams: Created auto-team with %d units", units_added);
    GlyphX_Debug_Print(dbg);

    /*
    ** Reset timers
    */
    TeamCheckTimer = AUTO_TEAM_DELAY;
    ForceTeamTimer = AUTO_TEAM_FORCE_DELAY;
}
