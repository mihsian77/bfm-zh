# Changelog

All notable changes to **BFM-Zh (Banner File Manager 中文美化增强版)** are documented in this file.

Base project: [The412Banner/banner-file-manager](https://github.com/The412Banner/banner-file-manager) v1.2.1
License: GPL-3.0-or-later

---

## [1.2.1-zh.4] - 2026-09-29

### Fixed
- 覆盖确认"逐个决定"模式：递归复制文件夹时子文件不再绕过覆盖确认
- 修复 OVERWRITE_RESULT_EACH 与 OVERWRITE_EACH 枚举值冲突
- 安装脚本：删除末尾调用 7z2603-x64.exe 的旧代码残留
- release.yml：统一 7-Zip 打包方式为便携版，修复正式发布包 7-Zip 安装失败
- Makefile：添加缺少的 config/theme/favorites/diff 四个源文件

### Added
- 7-Zip 便携版集成：CI 自动下载官方最新版并解压为完整已安装文件
- 7-Zip 查找路径扩展：支持 Z:\opt\apps\7-Zip\ 等多个位置

### Changed
- 版本号更新为 v1.2.1-zh.4
- README.md 优化：添加项目徽章、目录导航、7-Zip 便携版描述

---

## [1.2.1-zh.3] - 2026-09-16

### Added
- 深色主题：文件列表、左侧树、状态栏、搜索框全面深色化
- 全视图图标识别：大图标/小图标/列表/详细信息均支持格式图标
- 图片缩略图预览
- 7-Zip 压缩/解压/测试完整性集成

### Fixed
- 字体模糊问题
- 右键菜单英文残留
- 安装脚本兼容性

---

## [1.2.1-zh.2] - 2026-09-13

### Added
- 右键增强菜单（压缩、加速运行、启动参数、转区、提取图标）
- 预览窗格（图片、文本、文件属性）
- 双面板内容对比
- 批量重命名
- 哈希计算（MD5/SHA1/SHA256）

### Fixed
- 稳定性优化，减少目录加载崩溃
- 字符截断问题

---

## [1.2.1-zh.1] - 2026-09-09

### Added
- 完整中文界面（中/英/葡/俄 四语言）
- 双面板文件管理
- 收藏夹功能
- 三模式主题（亮色/暗色/跟随系统）
- 注册表配置持久化
- 标签页容器
- 文本逐行 diff 比较（LCS 算法）
- 右键"加速运行（清内存）"
- 工具菜单快速启动（记事本/CMD/注册表/任务管理器）
- 地址栏自动补全

### Base
- 同步上游 The412Banner/banner-file-manager v1.2.1
