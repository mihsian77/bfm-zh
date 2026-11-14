/*
 * image_viewer.c - 独立图片查看器
 *
 * 功能：滚轮缩放（以指针位置为中心）、左键拖动平移、同目录翻页、
 *       适应窗口与 1:1 切换、像素图最近邻放大、窗口位置记忆。
 *
 * 解码分两条路径：优先动态加载系统的 gdiplus.dll，失败时回退到
 * 早期的 IPicture 实现。两条路径最终都产出一张 HBITMAP，缩放和平移
 * 只在 HBITMAP 上做，不依赖解码器本身的能力，因此在 Wine 里缺少
 * GDI+ 的容器中仍可用，只是支持的格式少了 webp 一类。
 */

#include "main.h"
#include "image_viewer.h"
#include <olectl.h>
#include <ocidl.h>
#include <windowsx.h>

#define IV_CLASS          L"BFM-ImageViewer"
#define IV_MAX_FILES      512
#define IV_STATUS_H       26
#define IV_MIN_ZOOM       0.10
#define IV_MAX_ZOOM       8.00
#define IV_ZOOM_STEP      1.15

// 解码失败时的提示

#define IV_MSG_LOAD_FAIL  L"无法读取这张图片，可能是格式不受支持或文件已损坏。"

// ---------------------------------------------------------------------------
// GDI+ 动态加载
//
// 不直接链接 gdiplus，避免在没有该 DLL 的 Wine 环境里启动即失败。
// 结构体按 ABI 手工声明，避免依赖 gdiplus.h 在 C 编译单元里的可用性。
// ---------------------------------------------------------------------------

typedef struct {
    ULONG32 GdiplusVersion;
    void*   DebugEventCallback;
    BOOL    SuppressBackgroundThread;
    BOOL    SuppressExternalCodecs;
} IV_GDIP_STARTUP_INPUT;

typedef LONG (WINAPI* PFN_GDIP_STARTUP)(ULONG_PTR*, const IV_GDIP_STARTUP_INPUT*, void*);
typedef void (WINAPI* PFN_GDIP_SHUTDOWN)(ULONG_PTR);
typedef LONG (WINAPI* PFN_GDIP_LOAD_FILE)(const WCHAR*, void**);
typedef LONG (WINAPI* PFN_GDIP_TO_HBITMAP)(void*, HBITMAP*, DWORD);
typedef LONG (WINAPI* PFN_GDIP_DISPOSE)(void*);

static HMODULE            ivGdipDll = NULL;
static ULONG_PTR          ivGdipToken = 0;
static bool               ivGdipReady = false;
static bool               ivGdipTried = false;
static PFN_GDIP_STARTUP    ivGdipStartup = NULL;
static PFN_GDIP_SHUTDOWN   ivGdipShutdown = NULL;
static PFN_GDIP_LOAD_FILE  ivGdipLoadFile = NULL;
static PFN_GDIP_TO_HBITMAP ivGdipToHBITMAP = NULL;
static PFN_GDIP_DISPOSE    ivGdipDispose = NULL;

static bool ivGdipInit(void) {
    if (ivGdipReady) return true;
    if (ivGdipTried) return false;
    ivGdipTried = true;

    ivGdipDll = LoadLibraryW(L"gdiplus.dll");
    if (!ivGdipDll) return false;

    ivGdipStartup   = (PFN_GDIP_STARTUP)GetProcAddress(ivGdipDll, "GdiplusStartup");
    ivGdipShutdown  = (PFN_GDIP_SHUTDOWN)GetProcAddress(ivGdipDll, "GdiplusShutdown");
    ivGdipLoadFile  = (PFN_GDIP_LOAD_FILE)GetProcAddress(ivGdipDll, "GdipCreateBitmapFromFile");
    ivGdipToHBITMAP = (PFN_GDIP_TO_HBITMAP)GetProcAddress(ivGdipDll, "GdipCreateHBITMAPFromBitmap");
    ivGdipDispose   = (PFN_GDIP_DISPOSE)GetProcAddress(ivGdipDll, "GdipDisposeImage");

    if (!ivGdipStartup || !ivGdipLoadFile || !ivGdipToHBITMAP || !ivGdipDispose) {
        FreeLibrary(ivGdipDll);
        ivGdipDll = NULL;
        return false;
    }

    IV_GDIP_STARTUP_INPUT input;
    input.GdiplusVersion = 1;
    input.DebugEventCallback = NULL;
    input.SuppressBackgroundThread = FALSE;
    input.SuppressExternalCodecs = FALSE;

    if (ivGdipStartup(&ivGdipToken, &input, NULL) != 0) {
        ivGdipStartup = NULL;
        FreeLibrary(ivGdipDll);
        ivGdipDll = NULL;
        return false;
    }

    ivGdipReady = true;
    return true;
}

static DWORD ivArgb(COLORREF c) {
    return 0xFF000000u
         | ((DWORD)(c & 0xFF) << 16)
         | ((DWORD)(c & 0xFF00))
         | ((DWORD)((c >> 16) & 0xFF));
}

// ---------------------------------------------------------------------------
// 状态
// ---------------------------------------------------------------------------

static wchar_t ivFiles[IV_MAX_FILES][MAX_PATH];
static int     ivFileCount = 0;

static struct {
    HWND    hwnd;
    HWND    owner;
    HBITMAP bmp;          // 已解码的整图
    int     bw;           // 原始宽度（像素）
    int     bh;           // 原始高度（像素）
    double  zoom;         // 相对原始尺寸的缩放系数
    int     panX;         // 相对居中的偏移
    int     panY;
    bool    fit;          // true = 跟随窗口自适应
    wchar_t path[MAX_PATH];
    wchar_t dir[MAX_PATH];
    int     fileIndex;
    bool    dragging;
    POINT   dragStart;
    POINT   panStart;
    bool    usingGdip;     // 当前这张图是否走 GDI+ 解码
} iv = {0};

static COLORREF ivBackdrop(void) {
    return isDarkMode() ? RGB(28, 28, 30) : RGB(244, 244, 246);
}

// ---------------------------------------------------------------------------
// 解码
// ---------------------------------------------------------------------------

static HBITMAP ivDecodeGdiplus(const wchar_t* path) {
    if (!ivGdipInit()) return NULL;

    void* image = NULL;
    if (ivGdipLoadFile(path, &image) != 0 || !image) return NULL;

    HBITMAP hbm = NULL;
    if (ivGdipToHBITMAP(image, &hbm, ivArgb(ivBackdrop())) != 0) hbm = NULL;
    ivGdipDispose(image);
    return hbm;
}

static HBITMAP ivDecodeOle(const wchar_t* path) {
    OleInitialize(NULL);

    IPicture* pic = NULL;
    if (FAILED(OleLoadPicturePath((LPOLESTR)path, NULL, 0, 0,
                                  &IID_IPicture, (void**)&pic)) || !pic) {
        return NULL;
    }

    long himW = 0, himH = 0;
    pic->lpVtbl->get_Width(pic, &himW);
    pic->lpVtbl->get_Height(pic, &himH);

    HDC screen = GetDC(NULL);
    int w = himW > 0 ? MulDiv(himW, GetDeviceCaps(screen, LOGPIXELSY), 2540) : 0;
    int h = himH > 0 ? MulDiv(himH, GetDeviceCaps(screen, LOGPIXELSY), 2540) : 0;

    HBITMAP hbm = NULL;
    if (w > 0 && h > 0) {
        HDC mem = CreateCompatibleDC(screen);
        hbm = CreateCompatibleBitmap(screen, w, h);
        if (hbm) {
            HGDIOBJ old = SelectObject(mem, hbm);
            RECT rc = {0, 0, w, h};
            HBRUSH bg = CreateSolidBrush(ivBackdrop());
            FillRect(mem, &rc, bg);
            DeleteObject(bg);
            pic->lpVtbl->Render(pic, mem, 0, 0, w, h, 0, himH, himW, -himH, NULL);
            SelectObject(mem, old);
        }
        DeleteDC(mem);
    }

    ReleaseDC(NULL, screen);
    pic->lpVtbl->Release(pic);
    return hbm;
}

static bool ivLoadImage(const wchar_t* path) {
    HBITMAP hbm = ivDecodeGdiplus(path);
    bool gdip = (hbm != NULL);
    if (!hbm) hbm = ivDecodeOle(path);
    if (!hbm) return false;

    BITMAP bm = {0};
    if (GetObjectW(hbm, sizeof(bm), &bm) != sizeof(bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
        DeleteObject(hbm);
        return false;
    }

    if (iv.bmp) DeleteObject(iv.bmp);
    iv.bmp = hbm;
    iv.bw = bm.bmWidth;
    iv.bh = bm.bmHeight;
    iv.usingGdip = gdip;
    iv.zoom = 1.0;
    iv.panX = 0;
    iv.panY = 0;
    iv.fit = true;

    wcscpy_s(iv.path, MAX_PATH, path);
    return true;
}

// ---------------------------------------------------------------------------
// 同目录图片列表
// ---------------------------------------------------------------------------

static bool ivIsImageExt(const wchar_t* name) {
    const wchar_t* dot = wcsrchr(name, L'.');
    if (!dot) return false;
    const wchar_t* ext = dot + 1;
    return !_wcsicmp(ext, L"jpg")  || !_wcsicmp(ext, L"jpeg") ||
           !_wcsicmp(ext, L"png")  || !_wcsicmp(ext, L"gif")  ||
           !_wcsicmp(ext, L"bmp")  || !_wcsicmp(ext, L"ico")  ||
           !_wcsicmp(ext, L"tif")  || !_wcsicmp(ext, L"tiff") ||
           !_wcsicmp(ext, L"webp") || !_wcsicmp(ext, L"jfif");
}

static int ivCmpName(const void* a, const void* b) {
    return _wcsicmp((const wchar_t*)a, (const wchar_t*)b);
}

static void ivBuildList(void) {
    ivFileCount = 0;

    wchar_t pattern[MAX_PATH];
    swprintf_s(pattern, MAX_PATH, L"%ls\\*", iv.dir);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (!ivIsImageExt(fd.cFileName)) continue;
        if (ivFileCount >= IV_MAX_FILES) break;
        wcscpy_s(ivFiles[ivFileCount], MAX_PATH, fd.cFileName);
        ivFileCount++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (ivFileCount > 1) {
        qsort(ivFiles, (size_t)ivFileCount, sizeof(ivFiles[0]), ivCmpName);
    }

    const wchar_t* name = wcsrchr(iv.path, L'\\');
    name = name ? name + 1 : iv.path;
    iv.fileIndex = 0;
    for (int i = 0; i < ivFileCount; i++) {
        if (_wcsicmp(ivFiles[i], name) == 0) { iv.fileIndex = i; break; }
    }
}

static void ivSplitDir(const wchar_t* path) {
    wcscpy_s(iv.dir, MAX_PATH, path);
    wchar_t* slash = wcsrchr(iv.dir, L'\\');
    if (slash) *slash = L'\0';
}

static bool ivGoto(int index) {
    if (index < 0 || index >= ivFileCount) return false;

    wchar_t full[MAX_PATH];
    swprintf_s(full, MAX_PATH, L"%ls\\%ls", iv.dir, ivFiles[index]);
    if (!ivLoadImage(full)) return false;

    iv.fileIndex = index;
    if (iv.hwnd) {
        SetWindowTextW(iv.hwnd, ivFiles[index]);
        InvalidateRect(iv.hwnd, NULL, FALSE);
    }
    return true;
}

static void ivStep(int delta) {
    int next = iv.fileIndex + delta;
    if (next < 0 || next >= ivFileCount) return;
    ivGoto(next);
}

// ---------------------------------------------------------------------------
// 几何
// ---------------------------------------------------------------------------

static void ivCanvas(HWND h, int* cw, int* ch) {
    RECT rc;
    GetClientRect(h, &rc);
    *cw = rc.right;
    *ch = rc.bottom - IV_STATUS_H;
    if (*ch < 1) *ch = 1;
}

static void ivApplyFit(void) {
    if (!iv.bw || !iv.bh || !iv.hwnd) return;

    int cw, ch;
    ivCanvas(iv.hwnd, &cw, &ch);

    double s = (double)cw / (double)iv.bw;
    double sy = (double)ch / (double)iv.bh;
    if (sy < s) s = sy;
    if (s > 1.0) s = 1.0;          // 小图不放大，避免糊
    if (s < IV_MIN_ZOOM) s = IV_MIN_ZOOM;

    iv.zoom = s;
    iv.panX = 0;
    iv.panY = 0;
}

static void ivOrigin(int* ox, int* oy) {
    int cw, ch;
    ivCanvas(iv.hwnd, &cw, &ch);

    int dw = (int)(iv.bw * iv.zoom);
    int dh = (int)(iv.bh * iv.zoom);

    *ox = (cw - dw) / 2 + iv.panX;
    *oy = (ch - dh) / 2 + iv.panY;
}

// 以窗口内某点为锚点缩放，锚点下的图像内容保持不动

static void ivZoomAt(int mx, int my, double factor) {
    if (!iv.bw || !iv.bh) return;

    int cw, ch;
    ivCanvas(iv.hwnd, &cw, &ch);

    int ox, oy;
    ivOrigin(&ox, &oy);

    double imgX = (double)(mx - ox) / iv.zoom;
    double imgY = (double)(my - oy) / iv.zoom;

    double target = iv.zoom * factor;
    if (target < IV_MIN_ZOOM) target = IV_MIN_ZOOM;
    if (target > IV_MAX_ZOOM) target = IV_MAX_ZOOM;
    if (target == iv.zoom) return;

    iv.zoom = target;
    iv.fit = false;

    int dw = (int)(iv.bw * iv.zoom);
    int dh = (int)(iv.bh * iv.zoom);
    int newOx = (int)(mx - imgX * iv.zoom);
    int newOy = (int)(my - imgY * iv.zoom);

    iv.panX = newOx - (cw - dw) / 2;
    iv.panY = newOy - (ch - dh) / 2;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

static void ivDrawStatus(HDC dc, int cw, int chFull) {
    int ch = chFull - IV_STATUS_H;

    RECT sb = {0, ch, cw, chFull};
    HBRUSH bg = CreateSolidBrush(themeFaceBg());
    FillRect(dc, &sb, bg);
    DeleteObject(bg);

    HPEN pen = CreatePen(PS_SOLID, 1, themeFaceLine());
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, 0, ch, NULL);
    LineTo(dc, cw, ch);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    wchar_t left[320];
    const wchar_t* name = wcsrchr(iv.path, L'\\');
    name = name ? name + 1 : iv.path;

    int percent = (int)(iv.zoom * 100.0 + 0.5);
    if (ivFileCount > 1) {
        swprintf_s(left, 320, L"%ls    %dx%d    %d%%    %d/%d",
                   name, iv.bw, iv.bh, percent, iv.fileIndex + 1, ivFileCount);
    } else {
        swprintf_s(left, 320, L"%ls    %dx%d    %d%%", name, iv.bw, iv.bh, percent);
    }

    HGDIOBJ oldFont = SelectObject(dc, getUIFont());
    SetBkMode(dc, TRANSPARENT);

    RECT lr = {10, ch, cw - 10, chFull};
    SetTextColor(dc, themeFaceText());
    DrawTextW(dc, left, -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    static const wchar_t hint[] = L"滚轮缩放 · 左键拖动 · ←/→ 翻页 · 双击切换 · Esc 关闭";
    RECT rr = {10, ch, cw - 10, chFull};
    SetTextColor(dc, themePlaceholder());
    DrawTextW(dc, hint, -1, &rr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    SelectObject(dc, oldFont);
}

static void ivPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int cw = rc.right;
    int chFull = rc.bottom;
    int ch = chFull - IV_STATUS_H;
    if (cw < 1 || chFull < 1) { EndPaint(hwnd, &ps); return; }

    if (iv.fit) ivApplyFit();

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP buf = CreateCompatibleBitmap(hdc, cw, chFull);
    HGDIOBJ oldBuf = SelectObject(mem, buf);

    RECT canvas = {0, 0, cw, ch};
    HBRUSH bg = CreateSolidBrush(ivBackdrop());
    FillRect(mem, &canvas, bg);
    DeleteObject(bg);

    if (iv.bmp && iv.bw > 0 && iv.bh > 0) {
        int ox, oy;
        ivOrigin(&ox, &oy);

        int dw = (int)(iv.bw * iv.zoom);
        int dh = (int)(iv.bh * iv.zoom);

        RECT dr = {ox, oy, ox + dw, oy + dh};
        RECT vis;
        if (IntersectRect(&vis, &dr, &canvas) && !IsRectEmpty(&vis)) {
            // 只画可见那块，放大到 8 倍也不至于每次重绘整张图
            int sx = vis.left - ox, sy = vis.top - oy;
            int sw = vis.right - vis.left, sh = vis.bottom - vis.top;

            int srcX = (int)((double)sx / iv.zoom);
            int srcY = (int)((double)sy / iv.zoom);
            int srcW = (int)((double)sw / iv.zoom + 1.0);
            int srcH = (int)((double)sh / iv.zoom + 1.0);

            if (srcX < 0) srcX = 0;
            if (srcY < 0) srcY = 0;
            if (srcX >= iv.bw) srcX = iv.bw - 1;
            if (srcY >= iv.bh) srcY = iv.bh - 1;
            if (srcX + srcW > iv.bw) srcW = iv.bw - srcX;
            if (srcY + srcH > iv.bh) srcH = iv.bh - srcY;
            if (srcW < 1) srcW = 1;
            if (srcH < 1) srcH = 1;

            HDC img = CreateCompatibleDC(mem);
            HGDIOBJ oldImg = SelectObject(img, iv.bmp);

            // 放大两倍以上按最近邻，像素图不会糊成一团
            SetStretchBltMode(mem, iv.zoom >= 2.0 ? COLORONCOLOR : HALFTONE);
            SetBrushOrgEx(mem, 0, 0, NULL);
            StretchBlt(mem, vis.left, vis.top, vis.right - vis.left, vis.bottom - vis.top,
                       img, srcX, srcY, srcW, srcH, SRCCOPY);

            SelectObject(img, oldImg);
            DeleteDC(img);
        }
    }

    ivDrawStatus(mem, cw, chFull);

    BitBlt(hdc, 0, 0, cw, chFull, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBuf);
    DeleteObject(buf);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// 窗口位置记忆
// ---------------------------------------------------------------------------

static void ivSavePlacement(void) {
    if (!iv.hwnd) return;
    RECT r;
    GetWindowRect(iv.hwnd, &r);
    cfgSetInt(L"", L"ViewerX", (int)r.left);
    cfgSetInt(L"", L"ViewerY", (int)r.top);
    cfgSetInt(L"", L"ViewerW", (int)(r.right - r.left));
    cfgSetInt(L"", L"ViewerH", (int)(r.bottom - r.top));
}

static bool ivRestorePlacement(RECT* out) {
    int x = cfgGetInt(L"", L"ViewerX", 0x7FFFFFFF);
    int y = cfgGetInt(L"", L"ViewerY", 0x7FFFFFFF);
    int w = cfgGetInt(L"", L"ViewerW", 0);
    int h = cfgGetInt(L"", L"ViewerH", 0);
    if (x == 0x7FFFFFFF || w < 200 || h < 150) return false;

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    // 记忆的位置若已不在可视工作区内（换过分辨率或显示器），丢弃
    RECT r = {x, y, x + w, y + h};
    RECT inter;
    if (!IntersectRect(&inter, &r, &work)) return false;
    if (inter.right - inter.left < 120 || inter.bottom - inter.top < 80) return false;

    *out = r;
    return true;
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------

static LRESULT CALLBACK ivProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            ivPaint(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            if (iv.fit) ivApplyFit();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        case WM_MOUSEWHEEL: {
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            ScreenToClient(hwnd, &pt);

            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0) ivZoomAt(pt.x, pt.y, IV_ZOOM_STEP);
            else if (delta < 0) ivZoomAt(pt.x, pt.y, 1.0 / IV_ZOOM_STEP);

            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int cw, ch;
            ivCanvas(hwnd, &cw, &ch);
            if (pt.y >= ch) return 0;   // 状态栏不参与拖动

            iv.dragging = true;
            iv.dragStart = pt;
            iv.panStart.x = iv.panX;
            iv.panStart.y = iv.panY;
            SetCapture(hwnd);
            SetCursor(LoadCursor(NULL, IDC_SIZEALL));
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!iv.dragging) {
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                int cw, ch;
                ivCanvas(hwnd, &cw, &ch);
                SetCursor(LoadCursor(NULL, pt.y < ch ? IDC_SIZEALL : IDC_ARROW));
                return 0;
            }
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            iv.panX = iv.panStart.x + (pt.x - iv.dragStart.x);
            iv.panY = iv.panStart.y + (pt.y - iv.dragStart.y);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP:
            if (iv.dragging) {
                iv.dragging = false;
                ReleaseCapture();
            }
            return 0;

        case WM_LBUTTONDBLCLK: {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            int cw, ch;
            ivCanvas(hwnd, &cw, &ch);
            if (pt.y >= ch) return 0;

            if (iv.fit) {
                // 切到 1:1，并以双击点为锚
                iv.fit = false;
                ivZoomAt(pt.x, pt.y, 1.0 / (iv.zoom > 0 ? iv.zoom : 1.0));
            } else {
                iv.fit = true;
                ivApplyFit();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_KEYDOWN:
            switch (wParam) {
                case VK_ESCAPE:   PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0;
                case VK_LEFT:
                case VK_PRIOR:    ivStep(-1); return 0;
                case VK_RIGHT:
                case VK_NEXT:     ivStep(1); return 0;
                case VK_ADD:
                case VK_OEM_PLUS: {
                    int cw, ch;
                    ivCanvas(hwnd, &cw, &ch);
                    ivZoomAt(cw / 2, ch / 2, IV_ZOOM_STEP);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                case VK_SUBTRACT:
                case VK_OEM_MINUS: {
                    int cw, ch;
                    ivCanvas(hwnd, &cw, &ch);
                    ivZoomAt(cw / 2, ch / 2, 1.0 / IV_ZOOM_STEP);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                case 'F': {
                    iv.fit = true;
                    ivApplyFit();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                case '0': {
                    iv.fit = false;
                    iv.zoom = 1.0;
                    iv.panX = 0;
                    iv.panY = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
            return 0;

        case WM_CLOSE:
            ivSavePlacement();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (iv.dragging) { iv.dragging = false; ReleaseCapture(); }
            if (iv.bmp) { DeleteObject(iv.bmp); iv.bmp = NULL; }
            iv.hwnd = NULL;
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// 对外接口
// ---------------------------------------------------------------------------

bool ivIsOpen(void) {
    return iv.hwnd != NULL && IsWindow(iv.hwnd);
}

void ivClose(void) {
    if (ivIsOpen()) SendMessageW(iv.hwnd, WM_CLOSE, 0, 0);
}

static void ivEnsureClass(HINSTANCE inst) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ivProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;      // 自己画背景，避免闪烁
    wc.lpszClassName = IV_CLASS;
    RegisterClassExW(&wc);
    registered = true;
}

bool ivOpen(HWND owner, const wchar_t* path) {
    if (!path || !path[0]) return false;

    wchar_t full[MAX_PATH];
    if (GetFullPathNameW(path, MAX_PATH, full, NULL) == 0) {
        wcscpy_s(full, MAX_PATH, path);
    }

    if (ivIsOpen()) {
        // 已经开着：换一张图，不重建窗口
        if (!ivLoadImage(full)) return false;
        ivSplitDir(full);
        ivBuildList();
        SetWindowTextW(iv.hwnd, wcsrchr(full, L'\\') ? wcsrchr(full, L'\\') + 1 : full);
        InvalidateRect(iv.hwnd, NULL, FALSE);
        SetForegroundWindow(iv.hwnd);
        return true;
    }

    if (!ivLoadImage(full)) return false;
    if (!iv.bmp) return false;

    ivSplitDir(full);
    ivBuildList();

    ivEnsureClass(globalHInstance);

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    RECT placed;
    int w, h, x, y;
    if (ivRestorePlacement(&placed)) {
        x = placed.left; y = placed.top;
        w = placed.right - placed.left;
        h = placed.bottom - placed.top;
    } else {
        // 默认占工作区 85%，保证大图也能整张看到
        int maxW = (work.right - work.left) * 85 / 100;
        int maxH = (work.bottom - work.top) * 85 / 100;
        w = maxW; h = maxH;
        x = work.left + ((work.right - work.left) - w) / 2;
        y = work.top + ((work.bottom - work.top) - h) / 2;
    }

    const wchar_t* name = wcsrchr(full, L'\\');
    name = name ? name + 1 : full;

    iv.owner = owner;
    iv.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, IV_CLASS, name,
                              WS_OVERLAPPEDWINDOW,
                              x, y, w, h, owner, NULL, globalHInstance, NULL);
    if (!iv.hwnd) return false;

    ivApplyFit();
    ShowWindow(iv.hwnd, SW_SHOW);
    UpdateWindow(iv.hwnd);
    SetForegroundWindow(iv.hwnd);
    SetFocus(iv.hwnd);

    return true;
}
