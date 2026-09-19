/*
 * ImFast for GTA III / Liberty City (1.0 EN, also used by GTA-LC)
 *
 * Skip intro movies only — land on the main menu. No auto-load / save detect.
 *
 * Intro sequencer gGameState switch (jump table 0x60FCC0):
 *   0-4  Logo.mpg / GTAtitles.mpg
 *   5    LoadingScreen + InitialiseOnceAfterRW
 *   6    frontend flags
 *   7    menu-idle
 *   9    frontend loop
 */
#include <plugin.h>
#include <CGame.h>
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>

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

static bool bEnable       = true;
static bool bSkipIntro    = true;
static bool bNoLoadScreen = true;

// III 1.0 EN (GTA-LC uses this exe).
static uintptr_t AddrJumpTable()     { return 0x60FCC0; }
static uintptr_t AddrState5()        { return 0x582D8A; }
static uintptr_t AddrCallLogo()      { return 0x582A93; }
static uintptr_t AddrCallTitles()    { return 0x582C26; }
static uintptr_t AddrPlayMovie()     { return 0x582380; }
static uintptr_t AddrLoadingScreen() { return 0x48D770; }
static uintptr_t AddrIntroSwitch()   { return 0x582A5D; }

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
    bSkipIntro    = GetPrivateProfileBoolA("CONFIG", "SKIP_INTRO", true, ini);
    bSkipIntro    = GetPrivateProfileBoolA("CONFIG", "NO_COPYRIGHT", bSkipIntro, ini);
    bNoLoadScreen = GetPrivateProfileBoolA("CONFIG", "NO_LOADSCREEN", true, ini);
    return bEnable;
}

static void IntroTick()
{
    if (!bSkipIntro) return;
    CGame::playingIntro = false;
    if (gGameState >= 0 && gGameState <= 4)
        gGameState = 5;
}

static void IntroSwitchHook(injector::reg_pack& regs)
{
    IntroTick();
    regs.eax = static_cast<uint32_t>(gGameState);
    regs.edx = static_cast<uint32_t>(gGameState);
}

struct ImFastIII
{
    ImFastIII()
    {
        Log("ImFast III loading ver=%d", (int)GetGameVersion());
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
            {
                patch::PutRetn(AddrLoadingScreen());
                Log("PutRetn LoadingScreen");
            }

            patch::StaticHook(AddrIntroSwitch(), AddrIntroSwitch() + 6, IntroSwitchHook);
            Log("intro switch hooked (menu only, no autoload)");

            Events::initGameEvent += []
            {
                CGame::playingIntro = false;
            };
        }

        Log("ImFast III ready");
    }
} g_ImFastIII;
