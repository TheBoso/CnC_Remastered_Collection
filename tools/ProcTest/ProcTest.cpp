// ProcTest.cpp
//
// Headless host for RedAlert.dll. Plays the role of the GlyphX engine for one
// skirmish game: sets up the lobby (players, teams, tech level), starts the map
// and advances the simulation as fast as possible, printing every event the DLL
// sends back. Used by tools/ProcTest/run_tests.ps1 to test the procedural
// mission generator without launching the game.
//
// Output lines (one per event):
//   DBG  <frame> <text>      GlyphX_Debug_Print output
//   MSG  <frame> <text>      on-screen message to the player
//   OVER <frame> <win|lose>  game over for the human player
//   END  <frame> <reason>    harness finished (frames | gameover | startfail | crash)
//   TEST <frame> <cmd> <objective> <value> -> <result>   a --do action (ProcMission_Test)
//

#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "../../REDALERT/DLLInterface.h"

typedef void (__cdecl * CNC_Event_Callback_Type)(const EventCallbackStruct & event);

typedef unsigned int (__cdecl * CNC_Version_Type)(unsigned int);
typedef void (__cdecl * CNC_Init_Type)(const char *, CNC_Event_Callback_Type);
typedef void (__cdecl * CNC_Config_Type)(const CNCRulesDataStruct &);
typedef void (__cdecl * CNC_Add_Mod_Path_Type)(const char *);
typedef bool (__cdecl * CNC_Set_Multiplayer_Data_Type)(int, CNCMultiplayerOptionsStruct &, int, CNCPlayerInfoStruct *, int);
typedef bool (__cdecl * CNC_Start_Instance_Variation_Type)(int, int, int, int, const char *, const char *, const char *, int, const char *);
typedef bool (__cdecl * CNC_Advance_Instance_Type)(unsigned __int64);
typedef int (__cdecl * ProcMission_Test_Type)(const char *, const char *, int);

// --do frame[+every]:command[:objective[:value]] - see ProcMission_Test in PROCMISSION.CPP
struct TestAction {
    long Frame;
    long Every;         // 0 = once
    char Command[32];
    char Objective[48];
    int Value;
};
static TestAction s_Actions[32];
static int s_ActionCount = 0;
static ProcMission_Test_Type s_Test = NULL;

// HousesType values (DEFINES.H)
static const char * s_Countries[] = {"Spain", "Greece", "USSR", "England", "Ukraine", "Germany", "France", "Turkey"};

static long s_Frame = 0;
static bool s_GameOver = false;
static FILE * s_Log = NULL;
static const unsigned __int64 HUMAN_ID = 1000;

static void Out(const char * tag, const char * text)
{
    // Keep one event per line.
    std::string line = text ? text : "";
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '\n' || line[i] == '\r') line[i] = ' ';
    }
    fprintf(stdout, "%-4s %ld %s\n", tag, s_Frame, line.c_str());
    if (s_Log) fprintf(s_Log, "%-4s %ld %s\n", tag, s_Frame, line.c_str());
}

/*
**	Crash reporting: print a symbolised stack trace (needs RedAlert.pdb next to the DLL).
*/
static void Print_Stack(CONTEXT * context)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, NULL, TRUE);

    STACKFRAME64 frame;
    memset(&frame, 0, sizeof(frame));
    frame.AddrPC.Offset = context->Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context->Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context->Esp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int depth = 0; depth < 32; depth++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &frame, context, NULL,
                SymFunctionTableAccess64, SymGetModuleBase64, NULL) || frame.AddrPC.Offset == 0) break;

        char symbuf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO * symbol = (SYMBOL_INFO *)symbuf;
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        const char * name = SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol) ? symbol->Name : "?";

        IMAGEHLP_LINE64 line;
        memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(line);
        DWORD linedisp = 0;
        char buf[512];
        if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &linedisp, &line)) {
            sprintf(buf, "  %s  %s:%lu", name, line.FileName, line.LineNumber);
        } else {
            sprintf(buf, "  %s  0x%08llx", name, frame.AddrPC.Offset);
        }
        Out("STK", buf);
    }
}

static LONG CALLBACK Crash_Handler(EXCEPTION_POINTERS * info)
{
    DWORD code = info->ExceptionRecord->ExceptionCode;
    // Only fatal errors; let the DLL's own first-chance exceptions (if any) through.
    if (code != EXCEPTION_ACCESS_VIOLATION && code != 0xC0000374 && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != 0xC0000409) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char buf[64];
    sprintf(buf, "crash 0x%08lx", code);
    Out("END", buf);
    Print_Stack(info->ContextRecord);
    fflush(stdout);
    if (s_Log) fflush(s_Log);
    TerminateProcess(GetCurrentProcess(), 5);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void __cdecl Event_Callback(const EventCallbackStruct & event)
{
    switch (event.EventType) {
        case CALLBACK_EVENT_DEBUG_PRINT:
            Out("DBG", event.DebugPrint.PrintString);
            break;

        case CALLBACK_EVENT_MESSAGE:
            if (event.Message.Message && *event.Message.Message) {
                Out("MSG", event.Message.Message);
            }
            break;

        case CALLBACK_EVENT_GAME_OVER: {
            bool win = false;
            for (int i = 0; i < event.GameOver.MultiPlayerTotalPlayers && i < GAME_OVER_MULTIPLAYER_MAX_PLAYERS_TRACKED; i++) {
                if ((unsigned __int64)event.GameOver.MultiPlayerPlayersData[i].GlyphXPlayerID == HUMAN_ID) {
                    win = event.GameOver.MultiPlayerPlayersData[i].IsWinner;
                }
            }
            if (!event.GameOver.Multiplayer) win = event.GameOver.PlayerWins;
            Out("OVER", win ? "win" : "lose");
            s_GameOver = true;
            break;
        }

        default:
            break;
    }
}

static int Country_From_Name(const char * name)
{
    for (int i = 0; i < 8; i++) {
        if (_stricmp(name, s_Countries[i]) == 0) return i;
    }
    return -1;
}

static bool Parse_Action(const char * text, TestAction & action)
{
    memset(&action, 0, sizeof(action));
    char buf[160];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char * fields[4] = {NULL, NULL, NULL, NULL};
    int count = 0;
    for (char * tok = strtok(buf, ":"); tok != NULL && count < 4; tok = strtok(NULL, ":")) fields[count++] = tok;
    if (count < 2) return false;

    action.Frame = atol(fields[0]);
    char * plus = strchr(fields[0], '+');
    if (plus != NULL) action.Every = atol(plus + 1);
    strncpy(action.Command, fields[1], sizeof(action.Command) - 1);
    if (count > 2) strncpy(action.Objective, fields[2], sizeof(action.Objective) - 1);
    if (count > 3) action.Value = atoi(fields[3]);
    return true;
}

static void Run_Actions()
{
    for (int i = 0; i < s_ActionCount; i++) {
        TestAction & a = s_Actions[i];
        if (s_Frame < a.Frame) continue;
        if (a.Every > 0 ? ((s_Frame - a.Frame) % a.Every) != 0 : s_Frame != a.Frame) continue;
        int result = s_Test(a.Command, a.Objective, a.Value);
        char buf[160];
        sprintf(buf, "%s %s %d -> %d", a.Command, a.Objective[0] ? a.Objective : "-", a.Value, result);
        Out("TEST", buf);
    }
}

static void Split(const std::string & text, std::vector<std::string> & out)
{
    size_t start = 0;
    while (start <= text.size()) {
        size_t comma = text.find(',', start);
        if (comma == std::string::npos) comma = text.size();
        if (comma > start) out.push_back(text.substr(start, comma - start));
        start = comma + 1;
    }
}

// Runs the simulation. Returns the stop reason, or NULL after a crash (already reported).
// Kept free of C++ objects so it can use structured exception handling.
static const char * Run_Frames(CNC_Advance_Instance_Type advance, long frames)
{
    __try {
        for (s_Frame = 0; s_Frame < frames; s_Frame++) {
            if (!advance(HUMAN_ID) || s_GameOver) return "gameover";
            if (s_ActionCount > 0) Run_Actions();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char buf[64];
        sprintf(buf, "crash 0x%08lx", GetExceptionCode());
        Out("END", buf);
        return NULL;
    }
    return "frames";
}

static void Usage()
{
    printf("ProcTest --dll <RedAlert.dll> --content <RED_ALERT\\CD1 dir> [options]\n"
           "  --mod <dir>          add a mod search path (repeatable, first wins)\n"
           "  --scenario <n>       skirmish scenario index (SCMnnEA.INI), default 1\n"
           "  --map <name>         override map file name, e.g. SCM01EA.INI\n"
           "  --player <country>   human country, default Greece\n"
           "  --allies <list>      AI countries on the human's team, e.g. England\n"
           "  --enemies <list>     AI countries on other teams, default USSR\n"
           "  --ffa                each enemy on its own team (default: enemies share a team)\n"
           "  --no-early-win       houses only lose when everything is dead (default: like the\n"
           "                       real client, a house with no structures or MCV is blown up)\n"
           "  --tech <1-10>        lobby tech level, default 10\n"
           "  --credits <n>        starting credits, default 10000\n"
           "  --frames <n>         frames to run, default 27000 (30 min at 15 fps)\n"
           "  --log <file>         also write the output to a file\n");
}

int main(int argc, char ** argv)
{
    std::string dll, content, map, player = "Greece", allies, enemies = "USSR", logfile;
    std::vector<std::string> mods;
    int scenario = 1, tech = 10, credits = 10000;
    long frames = 27000;
    bool ffa = false;
    bool earlywin = true;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        std::string v = (i + 1 < argc) ? argv[i + 1] : "";
        if (a == "--dll") { dll = v; i++; }
        else if (a == "--content") { content = v; i++; }
        else if (a == "--mod") { mods.push_back(v); i++; }
        else if (a == "--scenario") { scenario = atoi(v.c_str()); i++; }
        else if (a == "--map") { map = v; i++; }
        else if (a == "--player") { player = v; i++; }
        else if (a == "--allies") { allies = v; i++; }
        else if (a == "--enemies") { enemies = v; i++; }
        else if (a == "--ffa") { ffa = true; }
        else if (a == "--no-early-win") { earlywin = false; }
        else if (a == "--tech") { tech = atoi(v.c_str()); i++; }
        else if (a == "--credits") { credits = atoi(v.c_str()); i++; }
        else if (a == "--frames") { frames = atol(v.c_str()); i++; }
        else if (a == "--log") { logfile = v; i++; }
        else if (a == "--do") {
            if (s_ActionCount >= 32 || !Parse_Action(v.c_str(), s_Actions[s_ActionCount])) { Usage(); return 2; }
            s_ActionCount++;
            i++;
        }
        else { Usage(); return 2; }
    }
    if (dll.empty() || content.empty()) { Usage(); return 2; }

    // Search paths must end in a backslash; callers pass them without one ("dir\" escapes the quote).
    if (content.back() != '\\') content += '\\';
    for (size_t i = 0; i < mods.size(); i++) {
        if (!mods[i].empty() && mods[i].back() != '\\') mods[i] += '\\';
    }
    if (!logfile.empty()) s_Log = fopen(logfile.c_str(), "w");

    // Unbuffered, so the output survives a hard crash inside the DLL.
    setvbuf(stdout, NULL, _IONBF, 0);
    if (s_Log) setvbuf(s_Log, NULL, _IONBF, 0);

    // Never let a crash dialog block an unattended run.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    AddVectoredExceptionHandler(1, Crash_Handler);

    HMODULE module = LoadLibraryA(dll.c_str());
    if (module == NULL) {
        char buf[128];
        sprintf(buf, "LoadLibrary failed (%lu)", GetLastError());
        Out("END", buf);
        return 3;
    }

    CNC_Version_Type version = (CNC_Version_Type)GetProcAddress(module, "CNC_Version");
    CNC_Init_Type init = (CNC_Init_Type)GetProcAddress(module, "CNC_Init");
    CNC_Config_Type config = (CNC_Config_Type)GetProcAddress(module, "CNC_Config");
    CNC_Add_Mod_Path_Type add_mod_path = (CNC_Add_Mod_Path_Type)GetProcAddress(module, "CNC_Add_Mod_Path");
    CNC_Set_Multiplayer_Data_Type set_mp = (CNC_Set_Multiplayer_Data_Type)GetProcAddress(module, "CNC_Set_Multiplayer_Data");
    CNC_Start_Instance_Variation_Type start = (CNC_Start_Instance_Variation_Type)GetProcAddress(module, "CNC_Start_Instance_Variation");
    CNC_Advance_Instance_Type advance = (CNC_Advance_Instance_Type)GetProcAddress(module, "CNC_Advance_Instance");
    if (!version || !init || !config || !add_mod_path || !set_mp || !start || !advance) {
        Out("END", "missing DLL exports");
        return 3;
    }
    s_Test = (ProcMission_Test_Type)GetProcAddress(module, "ProcMission_Test");
    if (s_ActionCount > 0 && s_Test == NULL) {
        Out("END", "--do needs the ProcMission_Test export");
        return 3;
    }

    char buf[256];
    sprintf(buf, "DLL API version 0x%x", version(CNC_DLL_API_VERSION));
    Out("HOST", buf);

    /*
    **	-CD<path> makes the MIX files visible during startup (Init_Game loads PALETTE.CPS etc.
    **	before any content directory is set). The DLL splits its command line on spaces and
    **	keeps quotes, so pass the short 8.3 form of the path.
    */
    char shortpath[MAX_PATH];
    if (GetShortPathNameA(content.c_str(), shortpath, sizeof(shortpath)) == 0) {
        strncpy(shortpath, content.c_str(), sizeof(shortpath) - 1);
        shortpath[sizeof(shortpath) - 1] = '\0';
    }
    std::string cmdline = std::string("-CD") + shortpath;
    Out("HOST", cmdline.c_str());
    init(cmdline.c_str(), Event_Callback);

    CNCRulesDataStruct rules;
    memset(&rules, 0, sizeof(rules));
    for (int d = 0; d < 3; d++) {
        CNCDifficultyDataStruct & diff = rules.Difficulties[d];
        diff.FirepowerBias = diff.GroundspeedBias = diff.AirspeedBias = diff.ArmorBias = 1.0f;
        diff.ROFBias = diff.CostBias = diff.BuildSpeedBias = 1.0f;
        diff.RepairDelay = 0.02f;
        diff.BuildDelay = 0.03f;
        diff.IsBuildSlowdown = true;
        diff.IsContentScan = true;
    }
    config(rules);

    for (size_t i = 0; i < mods.size(); i++) {
        add_mod_path(mods[i].c_str());
    }

    /*
    **	Lobby: the human is always player 0 on team 0.
    */
    std::vector<std::string> allylist, enemylist;
    Split(allies, allylist);
    Split(enemies, enemylist);

    std::vector<CNCPlayerInfoStruct> players;
    struct Slot { std::string Country; bool IsAI; int Team; };
    std::vector<Slot> slots;
    slots.push_back(Slot{player, false, 0});
    for (size_t i = 0; i < allylist.size(); i++) slots.push_back(Slot{allylist[i], true, 0});
    for (size_t i = 0; i < enemylist.size(); i++) slots.push_back(Slot{enemylist[i], true, ffa ? (int)i + 1 : 1});

    for (size_t i = 0; i < slots.size(); i++) {
        int country = Country_From_Name(slots[i].Country.c_str());
        if (country < 0) {
            sprintf(buf, "unknown country '%s'", slots[i].Country.c_str());
            Out("END", buf);
            return 2;
        }
        CNCPlayerInfoStruct info;
        memset(&info, 0, sizeof(info));
        sprintf(info.Name, slots[i].IsAI ? "AI %d" : "Tester", (int)i);
        info.House = (unsigned char)country;
        info.ColorIndex = (int)i;
        info.GlyphxPlayerID = HUMAN_ID + i;
        info.Team = slots[i].Team;
        info.StartLocationIndex = (int)i;
        info.IsAI = slots[i].IsAI;
        players.push_back(info);

        sprintf(buf, "slot %d: %s %s team %d", (int)i, slots[i].Country.c_str(), slots[i].IsAI ? "AI" : "human", slots[i].Team);
        Out("HOST", buf);
    }

    CNCMultiplayerOptionsStruct options;
    memset(&options, 0, sizeof(options));
    options.MPlayerCount = 1;
    options.MPlayerBases = 1;
    options.MPlayerCredits = credits;
    options.MPlayerTiberium = 1;
    options.MPlayerGoodies = 0;
    options.MPlayerGhosts = 0;
    options.MPlayerSolo = 1;
    options.MPlayerUnitCount = 0;
    options.IsMCVDeploy = false;
    options.EnableSuperweapons = true;
    options.MPlayerShadowRegrow = false;
    options.MPlayerAftermathUnits = false;
    options.DestroyStructures = earlywin;     // Like the real client's lobby: a house with no structures or MCV is blown up
    options.ModernBalance = false;

    if (!set_mp(scenario, options, (int)players.size(), &players[0], 8)) {
        Out("END", "CNC_Set_Multiplayer_Data failed");
        return 4;
    }

    if (!start(scenario, -1, 0, tech, "MULTI", "GAME_GLYPHX_MULTIPLAYER", content.c_str(), -1, map.empty() ? NULL : map.c_str())) {
        Out("END", "startfail");
        return 4;
    }
    Out("HOST", "scenario started");

    const char * reason = Run_Frames(advance, frames);
    if (reason == NULL) {
        if (s_Log) fclose(s_Log);
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 5);
    }

    Out("END", reason);
    if (s_Log) fclose(s_Log);
    fflush(stdout);

    // The DLL has no clean shutdown path outside the engine; skip its static destructors.
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
