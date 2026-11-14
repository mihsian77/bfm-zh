#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>
#include <stdbool.h>

// 统一配置存储在 HKCU\Software\BFM-zh\<section>\<key> 下
//
// 所有持久化设置（主题、收藏、视图状态、运行参数）都经由这里读写。
// 早期版本把同一批设置分散在 Software\Winlator\WFM 与 Software\WFM 两处，
// 首次启动由 cfgMigrateLegacy() 只读迁入本根，旧键保留不删。

#define CONFIG_ROOT L"Software\\BFM-zh"

// 旧版本注册表根，仅用于迁移读取
#define CONFIG_LEGACY_ROOT  L"Software\\Winlator\\WFM"
#define CONFIG_LEGACY_ROOT2 L"Software\\WFM"

// 子键
#define CONFIG_SUB_CONTEXT   CONFIG_ROOT L"\\ContextMenu"
#define CONFIG_SUB_ISO       CONFIG_ROOT L"\\CurrentISOPath"
#define CONFIG_SUB_FILEASSOC CONFIG_ROOT L"\\FileAssociations"

// 把旧位置的设置迁入 CONFIG_ROOT；已迁移过则直接返回
void cfgMigrateLegacy(void);

int    cfgGetInt(const wchar_t* section, const wchar_t* key, int def);
void   cfgSetInt(const wchar_t* section, const wchar_t* key, int val);
bool   cfgGetStr(const wchar_t* section, const wchar_t* key, wchar_t* buf, int bufLen, const wchar_t* def);
void   cfgSetStr(const wchar_t* section, const wchar_t* key, const wchar_t* val);
bool   cfgDeleteKey(const wchar_t* section, const wchar_t* key);
bool   cfgDeleteSection(const wchar_t* section);

#endif
