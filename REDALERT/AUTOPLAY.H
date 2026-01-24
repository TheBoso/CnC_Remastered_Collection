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
 *                    File Name : AUTOPLAY.H                                                   *
 *                                                                                             *
 *                   Programmer : Autonomous Player Mod                                        *
 *                                                                                             *
 *                   Start Date : January 2026                                                 *
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   Autonomous_Player_AI -- Process autonomous player units each frame                        *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef AUTOPLAY_H
#define AUTOPLAY_H

/*
** Autonomous mode types for player units
*/
typedef enum AutoModeType : unsigned char {
    AUTOMODE_NONE = 0,      // Not in autonomous mode (normal control)
    AUTOMODE_SCOUT,         // Hunt mode - search and destroy enemies
    AUTOMODE_PATROL,        // Guard area - default for new units
    AUTOMODE_ATTACK,        // Coordinated assault on enemy base
    AUTOMODE_COUNT
} AutoModeType;

/*
** Attack phase for coordinated assault behavior
*/
typedef enum AttackPhaseType : unsigned char {
    ATTACK_PHASE_GROUPING,      // Units gathering together at friendly location
    ATTACK_PHASE_APPROACHING,   // Moving toward enemy base
    ATTACK_PHASE_REGROUPING,    // Gathering near enemy base before attack
    ATTACK_PHASE_ATTACKING,     // Engaging enemies
    ATTACK_PHASE_COUNT
} AttackPhaseType;

/*
** Patrol phase for patrol behavior
*/
typedef enum PatrolPhaseType : unsigned char {
    PATROL_PHASE_GUARDING,      // Guarding current position
    PATROL_PHASE_MOVING,        // Moving to new patrol point
    PATROL_PHASE_COUNT
} PatrolPhaseType;

/*
** Constants for autonomous behavior
*/
#define GROUPING_RANGE   (CELL_LEPTON_W * 3)  // 3 cells - range for units to be considered grouped
#define APPROACH_RANGE   (CELL_LEPTON_W * 18) // 18 cells - distance from enemy base to start regrouping
#define ATTACK_RANGE     (CELL_LEPTON_W * 6)  // 6 cells - distance from enemy base to start attacking
#define PATROL_RADIUS    12                    // Max cells from home to patrol
#define PATROL_GUARD_TIME (TICKS_PER_SECOND * 20) // Guard for 20 seconds before moving

/*
** Main AI function called each game frame
*/
void Autonomous_Player_AI(void);

/*
** Create a temporary attack team from selected units
** gather_cell is the player-specified rally point for initial gathering
*/
class HouseClass;
int Create_Attack_Team(HouseClass * house, CELL gather_cell);

#endif
