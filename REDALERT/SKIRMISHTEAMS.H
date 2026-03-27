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
 *                                                                                             *
 *                          S K I R M I S H T E A M S . H                                    *
 *                                                                                             *
 * Loads a data-driven team definitions file (teams.ini) from the DLL directory and injects   *
 * per-house copies of all faction-appropriate TeamTypeClass instances at skirmish game start. *
 *                                                                                             *
 * teams.ini format (placed next to the game DLL):                                            *
 *   [TeamTypes]                                                                               *
 *     <name> = <HouseInt>,<Flags>,<RecruitPri>,<InitNum>,<MaxAllowed>,<Origin>,<Trigger>,   *
 *              <ClassCount>,<Type:Qty>,...,<MissionCount>,<MissionNum:Arg>,...               *
 *   [TeamDifficulty]                                                                          *
 *     <name> = Easy | Normal | Hard                                                          *
 *                                                                                             *
 * Faction is determined from the House integer in the TeamTypes entry:                        *
 *   Allied: Spain(0), Greece(1), England(3), Germany(5), France(6), Turkey(7)                *
 *   Soviet: USSR(2), Ukraine(4)                                                               *
 *                                                                                             *
 ***********************************************************************************************/

#ifndef SKIRMISHTEAMS_H
#define SKIRMISHTEAMS_H

#include "defines.h"

class HouseClass;

#define MAX_SKIRMISH_TEAM_TEMPLATES  64

/*
** SkirmishTeamsClass
**
** Static-only manager. Call Load_And_Inject() once per skirmish game start
** (after GlyphX_Assign_Houses so ActLike fields are populated).
** TeamTypeClass::Init() called at scenario clear will free the injected copies.
*/
class SkirmishTeamsClass
{
public:
	/*
	** Load teams.ini from the DLL directory and inject per-house copies for all
	** active non-human houses. Safe to call multiple times (reloads each call).
	*/
	static void Load_And_Inject(void);

	/*
	** Inject team type copies for a single AI house. Called internally by
	** Load_And_Inject, but also available for deferred injection.
	*/
	static void Inject_For_House(HouseClass * house);

	/* Returns true if the given HousesType is an Allied faction house. */
	static bool Is_Allied_House(HousesType h);

	/* Returns true if the given HousesType is a Soviet faction house. */
	static bool Is_Soviet_House(HousesType h);

private:
	/*
	** One template entry loaded from [TeamTypes] + [TeamDifficulty] sections.
	*/
	struct TemplateEntry
	{
		char       Name[24];       // Team template name (prefix for per-house copies)
		char       IniData[512];   // Raw comma-separated Fill_In() data string
		DiffType   MinDifficulty;  // Minimum AI difficulty required
		bool       IsAllied;       // true=Allied faction, false=Soviet faction
	};

	static TemplateEntry Templates[MAX_SKIRMISH_TEAM_TEMPLATES];
	static int           TemplateCount;

public:
	/* Logging helpers — open/close a teams_debug.log next to the DLL. */
	static void Log_Open(void);
	static void Log(const char * fmt, ...);
	static void Log_Close(void);

	static FILE * s_LogFile;
};

#endif // SKIRMISHTEAMS_H
