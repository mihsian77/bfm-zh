#include "config.h"
#include <string.h>
#include <stdlib.h>

static HKEY openSection(const wchar_t* section, bool create) {
    wchar_t path[256] = {0};
    wcscpy_s(path, 256, CONFIG_ROOT);
    if (section && section[0]) {
        wcscat_s(path, 256, L"\\");
        wcscat_s(path, 256, section);
    }
    HKEY hKey = NULL;
    if (create) {
        RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
    } else {
        RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &hKey);
    }
    return hKey;
}

int cfgGetInt(const wchar_t* section, const wchar_t* key, int def) {
    HKEY hKey = openSection(section, false);
    if (!hKey) return def;
    DWORD val = 0, sz = sizeof(val);
    LRESULT r = RegQueryValueExW(hKey, key, NULL, NULL, (LPBYTE)&val, &sz);
    RegCloseKey(hKey);
    return (r == ERROR_SUCCESS) ? (int)val : def;
}

void cfgSetInt(const wchar_t* section, const wchar_t* key, int val) {
    HKEY hKey = openSection(section, true);
    if (!hKey) return;
    RegSetValueExW(hKey, key, 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(hKey);
}

bool cfgGetStr(const wchar_t* section, const wchar_t* key, wchar_t* buf, int bufLen, const wchar_t* def) {
    HKEY hKey = openSection(section, false);
    if (!hKey) {
        if (def && buf) wcscpy_s(buf, bufLen, def);
        return false;
    }
    DWORD sz = (DWORD)(bufLen * sizeof(wchar_t));
    LRESULT r = RegQueryValueExW(hKey, key, NULL, NULL, (LPBYTE)buf, &sz);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS) {
        if (def && buf) wcscpy_s(buf, bufLen, def);
        return false;
    }
    return true;
}

void cfgSetStr(const wchar_t* section, const wchar_t* key, const wchar_t* val) {
    HKEY hKey = openSection(section, true);
    if (!hKey) return;
    RegSetValueExW(hKey, key, 0, REG_SZ, (const BYTE*)val, (wcslen(val) + 1) * sizeof(wchar_t));
    RegCloseKey(hKey);
}

bool cfgDeleteKey(const wchar_t* section, const wchar_t* key) {
    HKEY hKey = openSection(section, false);
    if (!hKey) return false;
    LRESULT r = RegDeleteValueW(hKey, key);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

bool cfgDeleteSection(const wchar_t* section) {
    wchar_t path[256] = {0};
    wcscpy_s(path, 256, CONFIG_ROOT);
    if (section && section[0]) {
        wcscat_s(path, 256, L"\\");
        wcscat_s(path, 256, section);
    }
    return RegDeleteKeyW(HKEY_CURRENT_USER, path) == ERROR_SUCCESS;
}

// ============================================================================
// 旧配置迁移
//
// 规则：只把旧根里的值补进新根，新根已有同名值则不覆盖；旧键一律不清除。
// 这样即使迁移逻辑有偏差，用户原设置仍在原处，可随时回退到旧版本。
// ============================================================================

static bool regValueExists(HKEY key, const wchar_t* name) {
    DWORD type = 0, size = 0;
    return RegQueryValueExW(key, name, NULL, &type, NULL, &size) == ERROR_SUCCESS;
}

static void regCopyValues(HKEY src, HKEY dst) {
    DWORD index = 0;
    wchar_t name[256];

    for (;;) {
        DWORD nameLen = 256, size = 0, type = 0;
        if (RegEnumValueW(src, index, name, &nameLen, NULL, &type, NULL, &size) != ERROR_SUCCESS) break;
        index++;

        if (type != REG_DWORD && type != REG_SZ && type != REG_EXPAND_SZ && type != REG_BINARY) continue;
        if (size == 0) continue;
        if (regValueExists(dst, name)) continue;

        BYTE* data = (BYTE*)malloc(size);
        if (!data) return;

        DWORD got = size;
        if (RegQueryValueExW(src, name, NULL, &type, data, &got) == ERROR_SUCCESS && got > 0) {
            RegSetValueExW(dst, name, 0, type, data, got);
        }
        free(data);
    }
}

static void regCopySubKeys(HKEY src, HKEY dst, int depth) {
    if (depth > 3) return;

    DWORD index = 0;
    wchar_t name[256];

    for (;;) {
        DWORD nameLen = 256;
        if (RegEnumKeyExW(src, index, name, &nameLen, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        index++;

        HKEY srcSub = NULL;
        if (RegOpenKeyExW(src, name, 0, KEY_READ, &srcSub) != ERROR_SUCCESS) continue;

        HKEY dstSub = NULL;
        if (RegCreateKeyExW(dst, name, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &dstSub, NULL) == ERROR_SUCCESS) {
            regCopyValues(srcSub, dstSub);
            regCopySubKeys(srcSub, dstSub, depth + 1);
            RegCloseKey(dstSub);
        }
        RegCloseKey(srcSub);
    }
}

static void regMigrateFrom(const wchar_t* legacyRoot) {
    HKEY src = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, legacyRoot, 0, KEY_READ, &src) != ERROR_SUCCESS) return;

    HKEY dst = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CONFIG_ROOT, 0, NULL, 0,
                        KEY_READ | KEY_WRITE, NULL, &dst, NULL) == ERROR_SUCCESS) {
        regCopyValues(src, dst);
        regCopySubKeys(src, dst, 0);
        RegCloseKey(dst);
    }
    RegCloseKey(src);
}

void cfgMigrateLegacy(void) {
    if (cfgGetInt(L"", L"LegacyMigrated", 0) != 0) return;

    regMigrateFrom(CONFIG_LEGACY_ROOT);
    regMigrateFrom(CONFIG_LEGACY_ROOT2);

    cfgSetInt(L"", L"LegacyMigrated", 1);
}
