/*
 * ImFast for GTA Vice City (VC 1.0 EN)
 *
 * Intro sequencer 0x5FFAB0 (blocking loop, jump table 0x6D68E4):
 *   0-4  movies (Logo / GTAtitles) — retargeted to state 5
 *   5    InitialiseOnceAfterRW + LoadingScreen
 *   6    LoadingScreen + start frontend flags
 *   7    if m_bWantToLoad → InitialiseGame → 9
 *   8    InitialiseGame
 *   9    frontend loop (also inside 0x5FFAB0 — NOT FrontendIdle)
 *
 * FrontendIdle is only used by AppEventHandler after this function returns,
 * so auto-load is driven from an inline hook on the intro switch itself.
 */
#include <plugin.h>
#include <CGame.h>
#include <CMenuManager.h>
#include <Windows.h>
#include <Shlobj.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#pragma comment(lib, "Shell32.lib")

using namespace plugin;

static void Log(const char* fmt, ...)
{
    char dir[MAX_PATH] = {};
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    if (char* s = strrchr(dir, '\\')) *s = 0;
    char path[MAX_PATH] = {};
    _snprintf(path, sizeof(path), "%s\\imfast.log", dir);
    FILE* f = fopen(path, "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static bool GetPrivateProfileBoolA(const char* sec, const char* key, bool def, const char* file)
{
    char buf[256];
    if (GetPrivateProfileStringA(sec, key, 0, buf, sizeof(buf), file))
    {
        if (buf[0] == 0) return def;
        if (buf[1] == 0) return buf[0] != '0';
        return _stricmp(buf, "FALSE") != 0;
    }
    return def;
}

struct imfast_t { uint32_t version; int slot; };

static bool bEnable       = true;
static bool bAutoLoad     = true;
static bool bSkipIntro    = true;
static bool bNoLoadScreen = true;
static bool bDetectSave   = true;
static int  vkAvoidLoad   = 17;

static uintptr_t A(int ten, int eleven, int steam)
{
    switch (GetGameVersion())
    {
    case GAME_11EN:  return eleven;
    case GAME_STEAM: return steam;
    default:         return ten;
    }
}

static uintptr_t AddrJumpTable()       { return A(0x6D68E4, 0x6D6904, 0x6D5904); }
static uintptr_t AddrState5()          { return A(0x6002F9, 0x600319, 0x5FFF39); }
static uintptr_t AddrCallLogo()        { return A(0x5FFFC3, 0x5FFFE3, 0x5FFC23); }
static uintptr_t AddrCallTitles()      { return A(0x600179, 0x600199, 0x5FFE19); }
static uintptr_t AddrPlayMovie()       { return A(0x6007D0, 0x6007F0, 0x600430); }
static uintptr_t AddrLoadingScreen()   { return A(0x4A69D0, 0x4A69F0, 0x4A68A0); }
// mov edx,[gGameState] ; mov eax,edx  — start of intro switch
static uintptr_t AddrIntroSwitch()     { return A(0x5FFF8D, 0x5FFFAD, 0x5FFBCD); }

static void BuildIniPath(char* out, size_t n)
{
    char gameDir[MAX_PATH] = {};
    GetModuleFileNameA(NULL, gameDir, MAX_PATH);
    if (char* s = strrchr(gameDir, '\\')) *s = 0;
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&BuildIniPath), &self);
    char asi[MAX_PATH] = {};
    if (self) GetModuleFileNameA(self, asi, MAX_PATH);
    if (char* s = strrchr(asi, '\\'))
    {
        *s = 0;
        _snprintf(out, n, "%s\\imfast.ini", asi);
        if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return;
    }
    _snprintf(out, n, "%s\\imfast.ini", gameDir);
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return;
    _snprintf(out, n, "%s\\scripts\\imfast.ini", gameDir);
}

static bool ReadConfigINI()
{
    char ini[MAX_PATH] = {};
    BuildIniPath(ini, sizeof(ini));
    bEnable       = GetPrivateProfileBoolA("CONFIG", "ENABLE", true, ini);
    bDetectSave   = GetPrivateProfileBoolA("CONFIG", "DETECT_SAVE", true, ini);
    bAutoLoad     = GetPrivateProfileBoolA("CONFIG", "AUTO_LOAD", true, ini);
    bSkipIntro    = GetPrivateProfileBoolA("CONFIG", "SKIP_INTRO", true, ini);
    bSkipIntro    = GetPrivateProfileBoolA("CONFIG", "NO_COPYRIGHT", bSkipIntro, ini);
    bNoLoadScreen = GetPrivateProfileBoolA("CONFIG", "NO_LOADSCREEN", true, ini);
    vkAvoidLoad   = GetPrivateProfileIntA("CONFIG", "VKEY_AVOID_LOAD", 17, ini);
    return bEnable;
}

static bool ResolveUserFilesDir(char* out, size_t n)
{
    char docs[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docs)))
    {
        _snprintf(out, n, "%s\\GTA Vice City User Files", docs);
        if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    char gameDir[MAX_PATH] = {};
    GetModuleFileNameA(NULL, gameDir, MAX_PATH);
    if (char* s = strrchr(gameDir, '\\')) *s = 0;
    _snprintf(out, n, "%s\\GTA Vice City User Files", gameDir);
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
    _snprintf(out, n, "%s", gameDir[0] ? gameDir : ".");
    return false;
}

static void RegisterLastSlot(int slot)
{
    char dir[MAX_PATH] = {}, path[MAX_PATH] = {};
    ResolveUserFilesDir(dir, sizeof(dir));
    _snprintf(path, sizeof(path), "%s\\imfast.b", dir);
    imfast_t data = {};
    data.slot = slot;
    if (FILE* f = fopen(path, "wb")) { fwrite(&data, sizeof(data), 1, f); fclose(f); }
}

static int GetLastSlot()
{
    char dir[MAX_PATH] = {}, path[MAX_PATH] = {};
    ResolveUserFilesDir(dir, sizeof(dir));
    _snprintf(path, sizeof(path), "%s\\imfast.b", dir);
    int slot = -2;
    imfast_t data = {};
    if (FILE* f = fopen(path, "rb"))
    {
        if (fread(&data, sizeof(data), 1, f)) slot = data.slot;
        fclose(f);
    }
    if (slot >= 0)
    {
        char save[MAX_PATH] = {};
        _snprintf(save, sizeof(save), "%s\\GTAVCsf%d.b", dir, slot + 1);
        if (GetFileAttributesA(save) == INVALID_FILE_ATTRIBUTES) slot = -2;
    }
    return slot;
}

static bool sLoadRequested = false;

static void ArmAutoLoad()
{
    if (!bAutoLoad || sLoadRequested)
        return;

    if (GetAsyncKeyState(vkAvoidLoad) & 0xF000)
    {
        Log("autoload cancelled by key");
        sLoadRequested = true;
        return;
    }

    const int slot = GetLastSlot();
    Log("autoload arm slot=%d state=%d", slot, gGameState);
    sLoadRequested = true;

    if (slot >= 0)
    {
        auto& m = FrontEndMenuManager;
        m.m_nCurrentSaveSlot = slot;
        m.m_bWantToLoad = true;
        m.m_bShutDownFrontEndRequested = true;
    }
    // slot -1 / -2: stay in menu
}

// Runs on every intro-sequencer switch iteration (state 0..9).
static void IntroTick()
{
    if (bSkipIntro)
    {
        CGame::playingIntro = false;
        if (gGameState >= 0 && gGameState <= 4)
            gGameState = 5;
    }

    // Once AfterRW is done (state >= 6), arm load so state 7/9 start immediately.
    if (bAutoLoad && !sLoadRequested && gGameState >= 6)
        ArmAutoLoad();
}

// Replaces: mov edx,[gGameState] ; mov eax,edx   (then original cmp eax,9 follows)
static void IntroSwitchHook(injector::reg_pack& regs)
{
    IntroTick();
    regs.eax = static_cast<uint32_t>(gGameState);
    regs.edx = static_cast<uint32_t>(gGameState);
}

static void __cdecl FrontendIdleHook()
{
    IntroTick();
    FrontendIdle();
}

static int sLastSeenSlot = -999;

static void OnGameProcess()
{
    if (!bDetectSave) return;
    const int slot = FrontEndMenuManager.m_nCurrentSaveSlot;
    if (slot == sLastSeenSlot) return;
    if (!FrontEndMenuManager.m_bMenuActive
        && FrontEndMenuManager.m_bGameNotLoaded == false
        && slot >= 0)
    {
        sLastSeenSlot = slot;
        RegisterLastSlot(slot);
        Log("detect save slot=%d", slot);
    }
}

struct ImFastVC
{
    ImFastVC()
    {
        Log("ImFast VC loading ver=%d", (int)GetGameVersion());
        if (!ReadConfigINI())
        {
            Log("disabled");
            return;
        }

        if (bSkipIntro)
        {
            const uintptr_t table = AddrJumpTable();
            const uintptr_t state5 = AddrState5();
            for (int i = 0; i <= 4; ++i)
                patch::Set<uint32_t>(table + i * 4, static_cast<uint32_t>(state5));
            Log("jump table 0-4 -> %p", (void*)state5);

            patch::Nop(AddrCallLogo(), 5);
            patch::Nop(AddrCallTitles(), 5);
            patch::PutRetn(AddrPlayMovie());

            if (bNoLoadScreen)
                patch::PutRetn(AddrLoadingScreen());

            CGame::playingIntro = false;
            *reinterpret_cast<int*>(GetGlobalAddress(0x9B5F08)) = 5;
            gGameState = 5;
            Log("movies/splash patched, gGameState=5");

            // Per-iteration hook of the intro switch
            patch::StaticHook(AddrIntroSwitch(), AddrIntroSwitch() + 6, IntroSwitchHook);
            Log("intro switch hooked @%p", (void*)AddrIntroSwitch());

            Events::initGameEvent += []
            {
                CGame::playingIntro = false;
            };
        }

        // Backup if the main loop later uses FrontendIdle
        if (bAutoLoad)
            patch::RedirectCall(A(0x4A5BF2, 0x4A5C12, 0x4A5AC2), FrontendIdleHook);

        if (bDetectSave)
            Events::gameProcessEvent += OnGameProcess;

        Log("ImFast VC ready");
    }
} g_ImFastVC;
