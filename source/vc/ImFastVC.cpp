/*
 * ImFast for GTA Vice City (VC 1.0 EN)
 *
 * Skip intro movies only — land on the main menu. No auto-load / save detect.
 *
 * Intro sequencer jump table 0x6D68E4:
 *   0-4  movies (Logo / GTAtitles)
 *   5    InitialiseOnceAfterRW + LoadingScreen
 *   6-9  frontend / init
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

static uintptr_t A(int ten, int eleven, int steam)
{
    switch (GetGameVersion())
    {
    case GAME_11EN:  return eleven;
    case GAME_STEAM: return steam;
    default:         return ten;
    }
}

static uintptr_t AddrJumpTable()     { return A(0x6D68E4, 0x6D6904, 0x6D5904); }
static uintptr_t AddrState5()        { return A(0x6002F9, 0x600319, 0x5FFF39); }
static uintptr_t AddrCallLogo()      { return A(0x5FFFC3, 0x5FFFE3, 0x5FFC23); }
static uintptr_t AddrCallTitles()    { return A(0x600179, 0x600199, 0x5FFE19); }
static uintptr_t AddrPlayMovie()     { return A(0x6007D0, 0x6007F0, 0x600430); }
static uintptr_t AddrLoadingScreen() { return A(0x4A69D0, 0x4A69F0, 0x4A68A0); }
static uintptr_t AddrIntroSwitch()   { return A(0x5FFF8D, 0x5FFFAD, 0x5FFBCD); }

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

            patch::StaticHook(AddrIntroSwitch(), AddrIntroSwitch() + 6, IntroSwitchHook);
            Log("intro switch hooked @%p (menu only, no autoload)", (void*)AddrIntroSwitch());

            Events::initGameEvent += []
            {
                CGame::playingIntro = false;
            };
        }

        Log("ImFast VC ready");
    }
} g_ImFastVC;
