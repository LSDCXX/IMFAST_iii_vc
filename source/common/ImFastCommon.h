/*
 * Shared ImFast helpers (header-only).
 * Game-specific patches live in source/sa, source/vc, source/iii.
 */
#pragma once
#include <Windows.h>
#include <Shlobj.h>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#pragma comment(lib, "Shell32.lib")

namespace imfast {

inline void Log(const char* fmt, ...)
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

inline bool GetBool(const char* sec, const char* key, bool def, const char* file)
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

inline void BuildIniPath(char* out, size_t n)
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

inline bool ResolveUserFilesDir(const char* folderName, char* out, size_t n)
{
    char docs[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docs)))
    {
        _snprintf(out, n, "%s\\%s", docs, folderName);
        if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
    }
    char gameDir[MAX_PATH] = {};
    GetModuleFileNameA(NULL, gameDir, MAX_PATH);
    if (char* s = strrchr(gameDir, '\\')) *s = 0;
    _snprintf(out, n, "%s\\%s", gameDir, folderName);
    if (GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES) return true;
    _snprintf(out, n, "%s", gameDir[0] ? gameDir : ".");
    return false;
}

struct SlotFile { uint32_t version; int slot; };

inline void RegisterLastSlot(const char* userFilesFolder, int slot)
{
    char dir[MAX_PATH] = {}, path[MAX_PATH] = {};
    ResolveUserFilesDir(userFilesFolder, dir, sizeof(dir));
    _snprintf(path, sizeof(path), "%s\\imfast.b", dir);
    SlotFile data = {};
    data.slot = slot;
    if (FILE* f = fopen(path, "wb")) { fwrite(&data, sizeof(data), 1, f); fclose(f); }
}

inline int GetLastSlot(const char* userFilesFolder, const char* saveMask /* e.g. "GTAVCsf%d.b" */)
{
    char dir[MAX_PATH] = {}, path[MAX_PATH] = {};
    ResolveUserFilesDir(userFilesFolder, dir, sizeof(dir));
    _snprintf(path, sizeof(path), "%s\\imfast.b", dir);
    int slot = -2;
    SlotFile data = {};
    if (FILE* f = fopen(path, "rb"))
    {
        if (fread(&data, sizeof(data), 1, f)) slot = data.slot;
        fclose(f);
    }
    if (slot >= 0)
    {
        char name[64] = {}, save[MAX_PATH] = {};
        _snprintf(name, sizeof(name), saveMask, slot + 1);
        _snprintf(save, sizeof(save), "%s\\%s", dir, name);
        if (GetFileAttributesA(save) == INVALID_FILE_ATTRIBUTES) slot = -2;
    }
    return slot;
}

} // namespace imfast
