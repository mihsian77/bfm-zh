# BFM 中文美化增强版

Banner File Manager 的中文增强分支，针对 Winlator / Wine 桌面环境优化。

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Version](https://img.shields.io/badge/version-1.2.1--zh.5-green.svg)](https://github.com/mihsian77/bfm-zh/releases)
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/mihsian77/bfm-zh/actions)
[![Platform](https://img.shields.io/badge/platform-Winlator%20%7C%20Wine-orange.svg)]()

上游：[The412Banner/banner-file-manager](https://github.com/The412Banner/banner-file-manager) v1.2.1（GPL-3.0-or-later）
原始 WFM：BrunoSX（MIT，见 LICENSE.WFM）

---

## 目录

- [功能特性](#功能特性)
- [界面预览](#界面预览)
- [安装方法](#安装方法)
- [7-Zip 集成](#7-zip-集成)
- [自行构建](#自行构建)
- [实验性功能](#实验性功能)
- [许可证](#许可证)

---

## 功能特性

### 文件管理
- 双面板：左右独立浏览，支持跨面板复制/移动
- 四种视图：大图标 / 小图标 / 列表 / 详细信息，均支持按名称/类型/大小/日期排序
- 收藏夹：独立星标图标，注册表持久化
- 预览窗格：图片缩略预览、文本内容预览、文件属性
- 拖拽操作：面板内移动、面板间复制、拖到外部程序运行

### 智能图标
- 图片文件：自动生成缩略图预览
- 格式识别：音乐、视频、压缩包、文档、脚本等格式均有对应图标
- exe 图标增强：优先从 exe 提取原生图标，失败时查找同目录 .ico 文件

### 右键增强菜单
- 7-Zip 压缩 / 解压到当前目录 / 解压到同名文件夹 / 测试完整性
- 加速运行 / 激进加速（清理内存后启动）
- 启动参数：窗口化 / 全屏 / 无边框 / DX9 / DX11 / OpenGL / Vulkan 等
- 转区启动（日区编码，适用于部分 Galgame）
- 提取图标（保存为 .ico / .bmp）
- 创建快捷方式

### 界面与主题
- 三模式主题：亮色 / 暗色 / 跟随系统
- 字体优化：优先微软雅黑，Wine 下更清晰
- 磁盘容量条：已用/总容量百分比显示
- 状态栏：选中文件数量、总大小、当前目录文件数

### 文件操作安全
- 覆盖确认：全部覆盖 / 全部跳过 / 逐个决定
- 失败汇总：操作完成后显示失败文件列表和原因
- 完成提示：操作完成弹窗，含成功/失败统计

---

## 界面预览

| 主界面 | 大图标视图 |
|---|---|
| ![主界面](docs/screenshot.png) | ![大图标视图](docs/preview-single.jpg) |
| 深色主题 · 磁盘容量条 | 智能图标 · 图片缩略图 |

| 双面板 | 右键增强菜单 |
|---|---|
| ![双面板](docs/preview-split.jpg) | ![右键菜单](docs/preview-menu.jpg) |
| 双面板 · 预览窗格 | 压缩 / 加速 / 启动参数 / 转区 |

---

## 安装方法

### 自动安装（推荐）

1. 从 [Releases](https://github.com/mihsian77/bfm-zh/releases) 下载 `bfm-zh-x64.zip`
2. 解压后双击 `安装.bat`
3. 脚本自动完成：关闭 WFM → 备份原版 → 部署 7-Zip → 复制文件 → 启动 WFM

### 手动安装

将 `wfm.exe` 和 `libcdio.dll` 复制到 `C:\windows\` 目录覆盖原文件。

### 还原原版

双击 `卸载还原.bat`，脚本会自动恢复安装前备份的原版文件。

---

## 7-Zip 集成

发布包内置 7-Zip 便携版（当前 26.03，含中文语言包），安装脚本自动部署到 `Z:\opt\apps\7-Zip\`，无需手动安装。

支持的右键操作：
- 解压到当前目录
- 解压到 `<压缩包名>\` 文件夹
- 测试压缩包完整性
- 添加到压缩包

支持格式：7z、zip、rar、tar、gz、bz2、xz、iso 等常见格式。

---

## 自行构建

### 依赖

- x86_64-w64-mingw32-gcc
- x86_64-w64-mingw32-windres
- p7zip-full（CI 自动下载 7-Zip 时需要）

### 构建

```bash
# 使用统一构建脚本
bash scripts/build.sh

# 或使用 Makefile
make

# 或手动编译
CC=x86_64-w64-mingw32-gcc
CFLAGS="-O2 -std=c99 -DUNICODE -D_UNICODE -DCOBJMACROS -DWINVER=0x0600 -Wall"
SRCS="main theme config favorites diff content_view toolbar navbar treeview sizebar statusbar file_node file_actions input_dialog"
mkdir -p obj
for f in $SRCS; do
  $CC $CFLAGS -I./include -I./include/libcdio -c "src/$f.c" -o "obj/$f.o"
done
x86_64-w64-mingw32-windres -I./include -I./res -i res/resource.rc -o obj/resource.o
$CC -o wfm.exe obj/*.o -s -lcomctl32 -lgdi32 -lole32 -loleaut32 -luuid -luxtheme -lshlwapi -lcrypt32 -lmsimg32 ./libcdio.dll -Wl,--subsystem,windows
```

### CI 构建

推送代码到 `main` 分支自动触发构建，构建产物在 Actions 页面下载。Tag 推送（`v*`）自动创建 Release。

---

## 实验性功能

以下功能正在测试中，可能存在兼容性问题：

| 功能 | 说明 |
|---|---|
| 转区启动 | 模拟日区编码，部分 Galgame 可用，可能与容器编码冲突 |
| 加速 / 激进加速 | 清理内存效果因游戏而异，激进模式可能不稳定 |
| 窗口化 / 全屏参数 | 部分游戏不识别命令行参数，属正常现象 |
| 拖拽到外部程序 | 依赖 Wine 拖放支持，个别程序可能无响应 |

---

## 许可证

- 本项目基于 The412Banner/banner-file-manager（GPL-3.0-or-later）修改
- 上游含 BrunoSX 的 WFM 代码（MIT），声明见 LICENSE.WFM
- 7-Zip 为独立软件（LGPL + unRAR 限制），仅通过命令行调用，不构成衍生作品
