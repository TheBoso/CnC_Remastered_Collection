// PROCINI.H
//
// Preset library file names and parsing helpers for the procedural mission generator.
// See PROCMISSION.H for an overview and PROCMISSION.INI for the file formats.
//

#ifndef PROCINI_H
#define PROCINI_H

#include "procmission.h"

#define PROC_SETTINGS_INI       "PROCMISSION.INI"
#define PROC_TEAMS_INI          "PROCTEAMS.INI"
#define PROC_BASES_INI          "PROCBASES.INI"
#define PROC_OBJECTIVES_INI     "PROCOBJECTIVES.INI"

#define PROC_GENERATOR_SECTION  "Generator"     // In PROCMISSION.INI
#define PROC_OVERRIDE_SECTION   "Procedural"    // In the scenario INI

char const * Proc_Side_Name(ProcSideType side);
char const * Proc_Objective_Kind_Name(ProcObjectiveKindType kind);

#endif // PROCINI_H
