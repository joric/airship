#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <shellapi.h>
#include <time.h>

#ifndef WS_EX_NOACTIVATE
#define WS_EX_NOACTIVATE 0x08000000L
#endif

#ifndef WS_EX_LAYERED
#define WS_EX_LAYERED 0x00080000
#endif

#define APPCLASS "airship"
#define SECTION "config"

int *__security_cookie;
void __fastcall __security_check_cookie(int *_StackCookie)
{
};

unsigned int seed;

void _srand(unsigned int x)
{
    seed = x;
}

unsigned int _rand()
{
    seed = 0x343FD * seed + 0x269EC3;
    return (seed >> 0x10) & 0x7FFF;
}

char *_strrchr(char *s, char c)
{
    char *p;
    for (p = s + strlen(s); *p != c && p >= s; p--);
    return p;
}

typedef BOOL(WINAPI * PFN_UpdateLayeredWindow) (HWND, HDC, POINT *, SIZE *, HDC, POINT *, COLORREF, BLENDFUNCTION *, DWORD);
PFN_UpdateLayeredWindow _UpdateLayeredWindow;

typedef struct {
    int unused;
} gImage;

typedef struct {
    int unused;
} gGraphics;

typedef void (__stdcall * DebugEventProc) (int, char *);

typedef struct tagGdiplusStartupInput {
    UINT32 GdiplusVersion;
    DebugEventProc DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} _GdiplusStartupInput;

typedef int (__stdcall * _GdiplusStartup) (ULONG_PTR *, _GdiplusStartupInput *, void *);
typedef int (__stdcall * _GdipLoadImageFromFile) (wchar_t *, gImage **);
typedef int (__stdcall * _GdipGetImageWidth) (gImage *, int *);
typedef int (__stdcall * _GdipGetImageHeight) (gImage *, int *);
typedef int (__stdcall * _GdipDrawImageRectI) (gGraphics *, gImage *, int, int, int, int);
typedef int (__stdcall * _GdipCreateFromHDC) (HDC, gGraphics **);

#define NOTIFY_FOR_THIS_SESSION 0
#define WM_WTSSESSION_CHANGE 0x02B1
#define WTS_SESSION_LOCK    0x7
#define WTS_SESSION_UNLOCK  0x8
typedef DWORD(WINAPI * _WTS) (HWND, DWORD);

HBITMAP g_hBmp[16];

char g_szConfigFile[MAX_PATH];
char g_szImage[MAX_PATH];
char g_szLink[MAX_PATH];
char g_szLaunch[MAX_PATH];
char g_szExpire[MAX_PATH];
char g_szBuf[MAX_PATH];

typedef struct {
    int frames;
    int speed;
    int autoclose;
    int autohide;
    int top;
    int bottom;
    int randomize;
    int blocked;
    int delay;
    int pause;
    int awake;
    int draggable;
    int animframes;
    int animtime;
    const char *image;
    const char *link;
    const char *launch;
    const char *expire;
} conf_t;

typedef struct {
    int x, y;
    int w, h;
    int step;
    int sw, sh;
    int frame;
    BOOL captured;
    BOOL screensaver;
    BOOL lockscreen;
    BOOL sleeping;
    BOOL paused;
    DWORD ms;
    DWORD starttime;
    DWORD frametime;
    RECT dc;
    POINT ps;
    HDC hDC;
    HBITMAP hOldDC;
    HWND hWnd;
} view_t;

conf_t conf;
view_t view;

void UpdateFrame()
{
    BLENDFUNCTION bf;
    static POINT pt;            //zeroing
    SIZE sizeWnd;

    int side = view.step > 0 ? 0 : 1;
    int frame = side * conf.animframes + view.frame;

    SelectObject(view.hDC, view.hOldDC);
    view.hOldDC = (HBITMAP) SelectObject(view.hDC, g_hBmp[frame % conf.frames]);

    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;
    sizeWnd.cx = view.w;
    sizeWnd.cy = view.h;

    _UpdateLayeredWindow(view.hWnd, view.hDC, &pt, &sizeWnd, view.hDC, &pt, 0, &bf, 2);
}

#define CLAMP(x,a,b) ( x < a ? a : x > b ? b : x )

int RandomValue(int a, int b)
{
    return (int)(a + ((b - a) * _rand() / RAND_MAX));
}

void DelayedStart(DWORD delay)
{
    view.paused = TRUE;
    view.starttime = GetTickCount() + delay;

    view.step = 1;
    view.x = -view.w;
    if (conf.randomize)
        view.y = RandomValue(view.sh * conf.top / 100, view.sh * conf.bottom / 100);

    SetWindowPos(view.hWnd, HWND_TOPMOST, view.x, view.y, view.w, view.h, SWP_NOZORDER | SWP_NOACTIVATE);

    if (delay > 0)
        ShowWindow(view.hWnd, SW_HIDE);
}

BOOL CheckScreensaver()
{
    BOOL bRunning = FALSE;
    SystemParametersInfo(SPI_GETSCREENSAVERRUNNING, 0, &bRunning, 0);

    if (!bRunning && view.screensaver)
    {
        view.screensaver = FALSE;
        return TRUE;
    }

    if (bRunning)
        view.screensaver = TRUE;

    return FALSE;
}

BOOL CheckTime()
{
    BOOL bSleeping;
    SYSTEMTIME st;
    char szTime[20];

    GetLocalTime(&st);          // no time()/mktime() functions available, using strings
    GetDateFormat(LOCALE_USER_DEFAULT, 0, &st, "yyyy-MM-dd", szTime, sizeof(szTime));
    GetTimeFormat(LOCALE_USER_DEFAULT, 0, &st, " hh:mm:ss", szTime + 10, sizeof(szTime));

    bSleeping = (strcmp(szTime, conf.launch) < 0) || (strcmp(szTime, conf.expire) > 0);

    if (!bSleeping && view.sleeping)
    {
        view.sleeping = FALSE;
        return TRUE;
    }

    if (bSleeping)
    {
        if (!view.sleeping)
            ShowWindow(view.hWnd, SW_HIDE);
        view.sleeping = TRUE;
    }

    return FALSE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    POINT p;
    switch (message)
    {
        case WM_SYSKEYDOWN:
            if (wParam == VK_F4)
                if (!conf.blocked)
                    SendMessage(hWnd, WM_SYSCOMMAND, SC_CLOSE, 0);
            break;

        case WM_SYSCOMMAND:
            if (wParam != SC_MINIMIZE)
                return DefWindowProc(hWnd, message, wParam, lParam);
            break;

        case WM_WTSSESSION_CHANGE:
            switch (wParam)
            {
                case WTS_SESSION_LOCK:
                    view.lockscreen = TRUE;
                    break;
                case WTS_SESSION_UNLOCK:
                    if (view.lockscreen)
                    {
                        view.lockscreen = FALSE;
                        DelayedStart(conf.awake);
                    }
                    break;
            }
            break;

        case WM_TIMER:
            view.ms = GetTickCount();

            if (CheckTime())
                DelayedStart(conf.delay);

            if (CheckScreensaver())
                DelayedStart(conf.awake);

            if (view.starttime >= 0 && view.ms >= view.starttime && view.paused)
            {
                view.paused = FALSE;
                ShowWindow(view.hWnd, SW_SHOWNOACTIVATE);
            }

            if (!view.paused && !view.sleeping)
            {
                if (view.ms > view.frametime)
                {
                    view.frame++;

                    if (view.frame >= conf.animframes)
                        view.frame = 0;

                    view.frametime = view.ms + conf.animtime;
                    UpdateFrame();
                }

                if (!view.captured)
                {
                    view.x += conf.speed * view.step;

                    if (view.x < -view.w || view.x > view.sw)
                    {
                        view.step *= -1;
                        UpdateFrame();  // forceably update on corners

                        if (conf.randomize)
                            view.y = RandomValue(view.sh * conf.top / 100, view.sh * conf.bottom / 100);

                        if (view.step > 0)  // got to the left edge, the end
                            DelayedStart(conf.pause);
                    }

                    view.y = CLAMP(view.y, 0, view.sh - view.h);
                    SetWindowPos(view.hWnd, HWND_TOPMOST, view.x, view.y, view.w, view.h, SWP_NOZORDER | SWP_NOACTIVATE);
                }
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        case WM_LBUTTONDOWN:
            GetCursorPos(&view.ps);
            if (conf.draggable && !view.captured)
            {
                view.captured = TRUE;
                SetCapture(hWnd);
                GetWindowRect(hWnd, &view.dc);
            }
            break;

        case WM_MOUSEMOVE:
            if (view.captured)
            {
                GetCursorPos(&p);
                view.x = view.dc.left + p.x - view.ps.x;
                view.y = view.dc.top + p.y - view.ps.y;
                SetWindowPos(hWnd, NULL, view.x, view.y, 0, 0, SWP_NOSIZE);
            }
            break;

        case WM_LBUTTONUP:
            GetCursorPos(&p);
            if (p.x == view.ps.x && p.y == view.ps.y)
            {
                ShellExecute(NULL, "open", conf.link, NULL, NULL, SW_SHOW);

                if (conf.autoclose)
                    PostQuitMessage(0);

                if (conf.autohide)
                    DelayedStart(conf.pause);
            }

            if (view.captured)
            {
                view.captured = FALSE;
                ReleaseCapture();
            }
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
            break;
    }

    return 0;
}

int getInt(const char *szKey, int iDef)
{
    return GetPrivateProfileInt(SECTION, szKey, iDef, g_szConfigFile);
}

const char *getStr(const char *szKey, const char *szDef, char *szBuf, int iSize)
{
    return GetPrivateProfileString(SECTION, szKey, szDef, szBuf, iSize, g_szConfigFile) ? szBuf : szDef;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASS wc;
    MSG msg;
    int i;
    gGraphics *g;
    LARGE_INTEGER q;
    char szPath[MAX_PATH];
    wchar_t wszImage[MAX_PATH];

    HINSTANCE gdiplusDll = LoadLibrary("gdiplus.dll");
    ULONG_PTR gdiplusToken;
    _GdiplusStartupInput GdiplusStartupInput;
    _GdiplusStartup GdiplusStartup = (_GdiplusStartup) GetProcAddress(gdiplusDll, "GdiplusStartup");
    _GdipLoadImageFromFile GdipLoadImageFromFile = (_GdipLoadImageFromFile) GetProcAddress(gdiplusDll, "GdipLoadImageFromFile");
    _GdipGetImageWidth GdipGetImageWidth = (_GdipGetImageWidth) GetProcAddress(gdiplusDll, "GdipGetImageWidth");
    _GdipGetImageHeight GdipGetImageHeight = (_GdipGetImageHeight) GetProcAddress(gdiplusDll, "GdipGetImageHeight");
    _GdipDrawImageRectI GdipDrawImageRectI = (_GdipDrawImageRectI) GetProcAddress(gdiplusDll, "GdipDrawImageRectI");
    _GdipCreateFromHDC GdipCreateFromHDC = (_GdipCreateFromHDC) GetProcAddress(gdiplusDll, "GdipCreateFromHDC");

    _WTS WTSRegisterSessionNotification = (_WTS) GetProcAddress(LoadLibrary("wtsapi32.dll"), "WTSRegisterSessionNotification");

    gImage *pImage = NULL;

    HWND hWnd = FindWindow(APPCLASS, NULL);
    if (hWnd)
        PostMessage(hWnd, WM_CLOSE, 0, 0);

    QueryPerformanceCounter(&q);
    _srand((unsigned int)q.QuadPart);

    GetModuleFileName(NULL, szPath, MAX_PATH);

    strcpy(g_szConfigFile, szPath);
    strcpy(_strrchr(g_szConfigFile, '.'), ".ini");

    if (getInt("autorun", 0))
    {
        HKEY reg_key = 0;
        RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_ALL_ACCESS, &reg_key);
        RegSetValueEx(reg_key, APPCLASS, 0, REG_SZ, szPath, MAX_PATH);
    }

    conf.frames = getInt("frames", 2);
    conf.speed = getInt("speed", 1);
    conf.autoclose = getInt("autoclose", 0);
    conf.autohide = getInt("autohide", 0);
    conf.top = getInt("top", 25);
    conf.bottom = getInt("bottom", 75);
    conf.randomize = getInt("randomize", 0);
    conf.blocked = getInt("blocked", 0);
    conf.delay = getInt("delay", 0);
    conf.pause = getInt("pause", 0);
    conf.awake = getInt("awake", 0);
    conf.draggable = getInt("draggable", 1);
    conf.animframes = getInt("animframes", 1);
    conf.animtime = getInt("animtime", 250);

    conf.image = getStr("image", "airship.png", g_szImage, MAX_PATH);
    conf.link = getStr("link", "http://airship.com", g_szLink, MAX_PATH);
    conf.launch = getStr("launch", "", g_szLaunch, MAX_PATH);
    conf.expire = getStr("expire", "2140", g_szExpire, MAX_PATH);

    GdiplusStartupInput.GdiplusVersion = 1;
    GdiplusStartupInput.DebugEventCallback = NULL;
    GdiplusStartupInput.SuppressBackgroundThread = FALSE;
    GdiplusStartupInput.SuppressExternalCodecs = FALSE;
    GdiplusStartup(&gdiplusToken, &GdiplusStartupInput, NULL);

    _UpdateLayeredWindow = (PFN_UpdateLayeredWindow) GetProcAddress(LoadLibrary("user32.dll"), "UpdateLayeredWindow");

    *(char *)_strrchr(szPath, '\\') = 0;
    SetCurrentDirectory(szPath);

    MultiByteToWideChar(CP_ACP, 0, conf.image, MAX_PATH, wszImage, MAX_PATH);
    GdipLoadImageFromFile(wszImage, &pImage);
    if (!pImage)
        ExitProcess(0);

    GdipGetImageWidth(pImage, &view.w);
    GdipGetImageHeight(pImage, &view.h);
    view.h /= conf.frames;
    view.step = 1;
    view.hOldDC = NULL;
    view.paused = TRUE;
    view.sleeping = TRUE;
    view.screensaver = FALSE;
    view.lockscreen = FALSE;
    view.frame = 0;
    view.sw = GetSystemMetrics(SM_CXSCREEN);
    view.sh = GetSystemMetrics(SM_CYSCREEN);
    view.ms = GetTickCount();
    view.frametime = view.ms;
    view.starttime = -1;
    view.x = view.sw / 2;
    view.y = view.sh * (conf.top + (conf.bottom - conf.top) / 2) / 100;

    view.hDC = CreateCompatibleDC(0);

    for (i = 0; i < conf.frames; i++)
    {
        g_hBmp[i] = CreateCompatibleBitmap(GetDC(0), view.w, view.h);
        view.hOldDC = (HBITMAP) SelectObject(view.hDC, g_hBmp[i]);
        GdipCreateFromHDC(view.hDC, &g);
        GdipDrawImageRectI(g, pImage, 0, -view.h * i, view.w, view.h * conf.frames);
        SelectObject(view.hDC, view.hOldDC);
    }

    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = (WNDPROC) WndProc;
    wc.lpszClassName = APPCLASS;
    wc.hCursor = LoadCursor(NULL, IDC_HAND);

    RegisterClass(&wc);

    if (hWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, APPCLASS, APPCLASS, WS_POPUP, 0, 0, view.w, view.h, NULL, NULL, wc.hInstance, NULL))
    {
        view.hWnd = hWnd;

        if (WTSRegisterSessionNotification)
            WTSRegisterSessionNotification(hWnd, NOTIFY_FOR_THIS_SESSION);

        SetTimer(hWnd, 0, 0, NULL);

        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // won't exit without ExitProcess (/Zl mode)
    ExitProcess(0);
    return 0;
}
