#ifndef IMAGE_VIEWER_H
#define IMAGE_VIEWER_H

#include <windows.h>
#include <stdbool.h>

/*
 * 独立图片查看器
 *
 * 与预览窗格的区别：预览窗格只做固定尺寸的缩略图，本模块提供可缩放、
 * 可平移、可同目录翻页的独立窗口。
 *
 * 打开方式：if (!ivOpen(owner, path)) { ... }
 * 同一时间只维护一个查看器实例，重复调用会切换到新图片。
 */

bool ivOpen(HWND owner, const wchar_t* path);

// 查看器当前是否处于打开状态
bool ivIsOpen(void);

// 关闭查看器（主窗口退出时调用）
void ivClose(void);

#endif
