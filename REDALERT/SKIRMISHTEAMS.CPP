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
 *                        S K I R M I S H T E A M S . C P P                                  *
 *                                                                                             *
 * Loads teams.ini from the DLL directory and injects per-house TeamTypeClass instances        *
 * for every active non-human house at skirmish game start.                                    *
 *                                                                                             *
 ***********************************************************************************************/

#include "function.h"
#include "skirmishteams.h"
#include "ini.h"
#include "rawfile.h"
#include <stdio.h>
#include <stdarg.h>

/*
** Module-level DLL instance handle (stored by DllMain in STARTUP.CPP).
*/
extern HINSTANCE ProgramInstance;

/*
** NewINIFormat: controls how TeamTypeClass::Fill_In parses the data string.
** We always write teams.ini in format >= 2 (the current/default format).
*/
extern int NewINIFormat;

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------
SkirmishTeamsClass::TemplateEntry SkirmishTeamsClass::Templates[MAX_SKIRMISH_TEAM_TEMPLATES];
int                                SkirmishTeamsClass::TemplateCount = 0;
FILE *                             SkirmishTeamsClass::s_LogFile = NULL;


// ---------------------------------------------------------------------------
// Helper: get full path to teams.ini (same directory as the DLL)
// ---------------------------------------------------------------------------
static void Get_Teams_INI_Path(char * out, int maxLen)
{
	GetModuleFileName(ProgramInstance, out, maxLen);
	char * slash = strrchr(out, '\\');
	if (slash) {
		*(slash + 1) = '\0';
		strncat(out, "teams.ini", maxLen - (int)(strlen(out)) - 1);
	} else {
		strncpy(out, "teams.ini", maxLen - 1);
		out[maxLen - 1] = '\0';
	}
}


// ---------------------------------------------------------------------------
// SkirmishTeamsClass::Log_Open  --  Create/truncate teams_debug.log next to DLL
// ---------------------------------------------------------------------------
void SkirmishTeamsClass::Log_Open(void)
{
	if (s_LogFile) { fclose(s_LogFile); s_LogFile = NULL; }

	// Write to %TEMP% to avoid OneDrive / permissions issues
	char tempDir[MAX_PATH];
	GetTempPathA(sizeof(tempDir), tempDir);

	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%steams_debug.log", tempDir);

	s_LogFile = fopen(path, "w");
	if (s_LogFile) {
		fprintf(s_LogFile, "=== SkirmishTeams debug log ===\n");
		fflush(s_LogFile);
	}

	// Always print the path so we can find it even if fopen fails
	char dbg[MAX_PATH + 64];
	snprintf(dbg, sizeof(dbg), "SkirmishTeams: log -> %s (%s)", path, s_LogFile ? "OK" : "FAILED");
	GlyphX_Debug_Print(dbg);
}

// ---------------------------------------------------------------------------
// SkirmishTeamsClass::Log  --  printf-style log to teams_debug.log
// ---------------------------------------------------------------------------
void SkirmishTeamsClass::Log(const char * fmt, ...)
{
	if (!s_LogFile) return;
	va_list args;
	va_start(args, fmt);
	vfprintf(s_LogFile, fmt, args);
	va_end(args);
	fputc('\n', s_LogFile);
	fflush(s_LogFile);
}

// ---------------------------------------------------------------------------
// SkirmishTeamsClass::Log_Close
// ---------------------------------------------------------------------------
void SkirmishTeamsClass::Log_Close(void)
{
	if (s_LogFile) { fclose(s_LogFile); s_LogFile = NULL; }
}

// ---------------------------------------------------------------------------
// SkirmishTeams_Log  --  free-function wrapper so other TUs can log without
//                        including skirmishteams.h (which causes include
//                        conflicts in HOUSE.CPP / TEAMTYPE.CPP).
// ---------------------------------------------------------------------------
void SkirmishTeams_Log(const char * fmt, ...)
{
	if (!SkirmishTeamsClass::s_LogFile) return;
	va_list args;
	va_start(args, fmt);
	vfprintf(SkirmishTeamsClass::s_LogFile, fmt, args);
	va_end(args);
	fputc('\n', SkirmishTeamsClass::s_LogFile);
	fflush(SkirmishTeamsClass::s_LogFile);
}


// ---------------------------------------------------------------------------
// SkirmishTeamsClass::Is_Allied_House
// ---------------------------------------------------------------------------
bool SkirmishTeamsClass::Is_Allied_House(HousesType h)
{
	return (h == HOUSE_SPAIN   || h == HOUSE_GREECE  || h == HOUSE_ENGLAND ||
	        h == HOUSE_GERMANY || h == HOUSE_FRANCE   || h == HOUSE_TURKEY);
}


// ---------------------------------------------------------------------------
// SkirmishTeamsClass::Is_Soviet_House
// ---------------------------------------------------------------------------
bool SkirmishTeamsClass::Is_Soviet_House(HousesType h)
{
	return (h == HOUSE_USSR || h == HOUSE_UKRAINE);
}


// ---------------------------------------------------------------------------
// SkirmishTeamsClass::Load_And_Inject
//
// Loads teams.ini, caches the templates, then injects copies for every active
// non-human house. Must be called AFTER GlyphX_Assign_Houses() so that each
// HouseClass::ActLike field reflects the player's chosen faction.
// ---------------------------------------------------------------------------
void SkirmishTeamsClass::Load_And_Inject(void)
{
	// -----------------------------------------------------------------------
	// Open log file, then load templates from teams.ini
	// -----------------------------------------------------------------------
	GlyphX_Debug_Print("SkirmishTeams: Load_And_Inject called.");
	Log_Open();
	TemplateCount = 0;

	char path[MAX_PATH];
	Get_Teams_INI_Path(path, sizeof(path));
	Log("Loading teams.ini from: %s", path);

	RawFileClass file(path);
	if (!file.Is_Available()) {
		GlyphX_Debug_Print("SkirmishTeams: teams.ini not found next to DLL, skipping.");
		Log("ERROR: teams.ini not found.");
		return;
	}

	INIClass ini;
	if (!ini.Load(file)) {
		GlyphX_Debug_Print("SkirmishTeams: failed to parse teams.ini.");
		Log("ERROR: failed to parse teams.ini.");
		return;
	}

	// Parse [TeamTypes] section
	int teamCount = ini.Entry_Count("TeamTypes");
	for (int i = 0; i < teamCount && TemplateCount < MAX_SKIRMISH_TEAM_TEMPLATES; i++) {
		char const * entryName = ini.Get_Entry("TeamTypes", i);
		if (entryName == NULL) continue;

		char buf[512];
		buf[0] = '\0';
		ini.Get_String("TeamTypes", entryName, "", buf, sizeof(buf));
		if (buf[0] == '\0') continue;

		// Determine faction from the first token (house integer)
		int houseInt = atoi(buf);
		HousesType houseType = (HousesType)houseInt;

		bool isAllied = Is_Allied_House(houseType);
		bool isSoviet = Is_Soviet_House(houseType);
		if (!isAllied && !isSoviet) {
			// Unknown faction — skip
			continue;
		}

		TemplateEntry & t = Templates[TemplateCount];
		strncpy(t.Name, entryName, sizeof(t.Name) - 1);
		t.Name[sizeof(t.Name) - 1] = '\0';
		strncpy(t.IniData, buf, sizeof(t.IniData) - 1);
		t.IniData[sizeof(t.IniData) - 1] = '\0';
		t.IsAllied      = isAllied;
		t.MinDifficulty = DIFF_EASY; // default: available at all difficulties
		Log("  Template[%d]: %s  faction=%s  data=%s", TemplateCount, t.Name, isAllied ? "Allied" : "Soviet", t.IniData);
		TemplateCount++;
	}

	// Parse optional [TeamDifficulty] section for minimum difficulty overrides
	int diffCount = ini.Entry_Count("TeamDifficulty");
	for (int i = 0; i < diffCount; i++) {
		char const * entryName = ini.Get_Entry("TeamDifficulty", i);
		if (entryName == NULL) continue;

		char val[32];
		val[0] = '\0';
		ini.Get_String("TeamDifficulty", entryName, "Easy", val, sizeof(val));

		DiffType minDiff = DIFF_EASY;
		if (stricmp(val, "Normal") == 0) minDiff = DIFF_NORMAL;
		else if (stricmp(val, "Hard") == 0) minDiff = DIFF_HARD;

		// Find matching template and update its MinDifficulty
		for (int j = 0; j < TemplateCount; j++) {
			if (stricmp(Templates[j].Name, entryName) == 0) {
				Templates[j].MinDifficulty = minDiff;
				break;
			}
		}
	}

	char dbgbuf[128];
	sprintf(dbgbuf, "SkirmishTeams: loaded %d team templates from teams.ini.", TemplateCount);
	GlyphX_Debug_Print(dbgbuf);
	Log("Loaded %d templates total.", TemplateCount);

	// -----------------------------------------------------------------------
	// Inject copies for every active non-human house
	// -----------------------------------------------------------------------
	for (HousesType htype = HOUSE_MULTI1; htype <= HOUSE_MULTI8; htype++) {
		HouseClass * house = HouseClass::As_Pointer(htype);
		if (house == NULL || !house->IsActive || house->IsHuman || house->IsDefeated) continue;
		Inject_For_House(house);
	}
}


// ---------------------------------------------------------------------------
// SkirmishTeamsClass::Inject_For_House
//
// Creates a TeamTypeClass instance for each template that matches this house's
// faction and meets the difficulty threshold.
// ---------------------------------------------------------------------------
void SkirmishTeamsClass::Inject_For_House(HouseClass * house)
{
	if (house == NULL || TemplateCount == 0) return;

	// Determine this house's faction via ActLike
	HousesType actLike = house->ActLike;
	bool houseIsAllied = Is_Allied_House(actLike);
	bool houseIsSoviet = Is_Soviet_House(actLike);

	int slot = (int)house->Class->House - (int)HOUSE_MULTI1 + 1;
	if (slot < 1 || slot > 8) slot = 0;

	if (!houseIsAllied && !houseIsSoviet) {
		Log("House slot %d (ActLike=%d) has no recognised faction — skipping.", slot, (int)actLike);
		return;
	}

	Log("--- Injecting for house slot %d  ActLike=%d (%s)  Difficulty=%d ---",
	    slot, (int)actLike, houseIsAllied ? "Allied" : "Soviet", (int)house->Difficulty);

	// Save and restore NewINIFormat so Fill_In uses the current (>=2) format
	int savedFormat = NewINIFormat;
	NewINIFormat = 2;

	int injected = 0;
	for (int i = 0; i < TemplateCount; i++) {
		TemplateEntry const & t = Templates[i];

		// Faction gate
		if (houseIsAllied && !t.IsAllied) continue;
		if (houseIsSoviet &&  t.IsAllied) continue;

		// Difficulty gate
		if (t.MinDifficulty > house->Difficulty) continue;

		// Build per-house team name: "H<slot>_<templateName>" truncated to 23 chars
		char teamName[24];
		if (slot > 0) {
			sprintf(teamName, "H%d_%s", slot, t.Name);
		} else {
			strncpy(teamName, t.Name, sizeof(teamName) - 1);
		}
		teamName[sizeof(teamName) - 1] = '\0';

		// Build patched data string: replace first token (house int) with this house's int
		char dataStr[512];
		char const * comma = strchr(t.IniData, ',');
		if (comma == NULL) continue; // malformed entry

		sprintf(dataStr, "%d%s", (int)house->Class->House, comma);
		dataStr[sizeof(dataStr) - 1] = '\0';

		// Allocate a new TeamTypeClass in the global heap and fill it in
		TeamTypeClass * team = new TeamTypeClass();
		if (team == NULL) {
			GlyphX_Debug_Print("SkirmishTeams: TeamTypes heap full, stopping injection.");
			Log("ERROR: TeamTypes heap full after %d injections.", injected);
			break;
		}
		Log("  Injecting: %s  data=%s", teamName, dataStr);
		team->Fill_In(teamName, dataStr);
		team->Trigger.Set_Raw(-1);  // CCPtr: -1 = null; 0 would index into empty TriggerTypes heap and crash
		injected++;
	}

	NewINIFormat = savedFormat;

	// Set IsAlerted so autocreate teams are eligible, but delay the first fire
	// by one full AutocreateTime interval so the AI has time to build units first.
	// Without this, the first alert fires at tick 0 when the map is empty, teams
	// are created with no members to recruit, and they immediately disband.
	house->IsAlerted = true;
	house->AlertTime = Rule.AutocreateTime * TICKS_PER_MINUTE;
	Log("Set IsAlerted=true, AlertTime=%d ticks (1x AutocreateTime) for house slot %d.",
	    (int)(Rule.AutocreateTime * TICKS_PER_MINUTE), slot);

	char dbgbuf[128];
	sprintf(dbgbuf, "SkirmishTeams: injected %d teams for house slot %d (faction=%s, diff=%d).",
	        injected, slot, houseIsAllied ? "Allied" : "Soviet", (int)house->Difficulty);
	GlyphX_Debug_Print(dbgbuf);
	Log("Total injected for slot %d: %d", slot, injected);
}
