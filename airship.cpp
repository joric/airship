#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <time.h>
#include <wtsapi32.h>

#define WS_EX_NOACTIVATE 0x08000000L
#define WS_EX_LAYERED 0x00080000
#define ULW_ALPHA 0x00000002

typedef BOOL(WINAPI * PFN_UpdateLayeredWindow) (HWND, HDC, POINT *, SIZE *, HDC, POINT *, COLORREF, BLENDFUNCTION *, DWORD);
PFN_UpdateLayeredWindow UpdateLayeredWindow;

#define NOTIFY_FOR_THIS_SESSION 0
#define WM_WTSSESSION_CHANGE 0x02B1
#define WTS_SESSION_LOCK	0x7
#define WTS_SESSION_UNLOCK	0x8

ULONG_PTR g_gdiplusToken;
Gdiplus::Image * g_pImg = NULL;
HBITMAP g_hBmp[16];

char g_szConfigFile[MAX_PATH];
char g_szImage[MAX_PATH];
char g_szLink[MAX_PATH];
char g_szBuf[MAX_PATH];

struct conf_t {
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
	time_t launch;
	time_t expire;
};

struct view_t {
	int x, y;
	int w, h;
	int step;
	int mx, my;
	int mx0, my0;
	int sw, sh;
	int frame;
	bool captured;
	bool pressed;
	bool screensaver;
	bool lockscreen;
	bool sleeping;
	bool paused;
	DWORD ms;
	DWORD starttime;
	DWORD frametime;
	RECT rc;
	POINT pt, pt0;
	HDC hdcScreen;
	HDC hDC;
	HBITMAP hOldDC;
	HWND hWnd;
};

conf_t conf;
view_t view;

Gdiplus::Image * LoadImg(const char *szFile)
{
	Gdiplus::Image * pImage;
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);

	wchar_t mb[MAX_PATH];
	mbstowcs(mb, szFile, MAX_PATH);

	pImage = new Gdiplus::Image(mb);

	if (Gdiplus::Ok == pImage->GetLastStatus())
		return pImage;

	return NULL;
}

HRESULT DrawImg(Gdiplus::Image * pImage, HDC hdc, int x, int y)
{
	if (pImage)
	{
		Gdiplus::Graphics graphics(hdc);
		Gdiplus::Rect rc(x, y, pImage->GetWidth(), pImage->GetHeight());
		return Gdiplus::Ok == graphics.DrawImage(pImage, rc) ? S_OK : E_FAIL;
	}
	return E_UNEXPECTED;
}

void UpdateFrame()
{
	int side = view.step > 0 ? 0 : 1;
	int frame = side * conf.animframes + view.frame;

	if (view.hOldDC)
		SelectObject(view.hDC, view.hOldDC);

	view.hOldDC = (HBITMAP) SelectObject(view.hDC, g_hBmp[frame % conf.frames]);

	BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	POINT ptPos = { 0, 0 };
	POINT ptSrc = { 0, 0 };
	SIZE sizeWnd = { view.w, view.h };
	UpdateLayeredWindow(view.hWnd, view.hdcScreen, &ptPos, &sizeWnd, view.hDC, &ptSrc, 0, &blend, ULW_ALPHA);
}

int ClampValue(int x, int a, int b)
{
	return x < a ? a : x > b ? b : x;
}

int RandomValue(int a, int b)
{
	return (int)(a + ((b - a) * rand() / RAND_MAX));
}

void Reset()
{
	view.step = 1;
	view.x = -view.w;
	if (conf.randomize)
		view.y = RandomValue(view.sh * conf.top / 100, view.sh * conf.bottom / 100);
	SetWindowPos(view.hWnd, HWND_TOPMOST, view.x, view.y, view.w, view.h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void DelayedStart(DWORD delay)
{
	view.paused = true;
	view.starttime = GetTickCount() + delay;
	Reset();
	if (delay > 0)
		ShowWindow(view.hWnd, SW_HIDE);
}

bool CheckScreensaver()
{
	BOOL bRunning = false;
	int bResult = SystemParametersInfo(SPI_GETSCREENSAVERRUNNING, 0, &bRunning, 0);

	if (!bRunning && view.screensaver)
	{
		view.screensaver = false;
		return true;
	}

	if (bRunning)
		view.screensaver = true;

	return false;
}

bool CheckTime()
{
	time_t t;
	time(&t);
	bool bSleeping = (conf.launch > 0 && t < conf.launch) || (conf.expire > 0 && t > conf.expire);

	if (!bSleeping && view.sleeping)
	{
		view.sleeping = false;
		return true;
	}

	if (bSleeping)
	{
		if (!view.sleeping)
			ShowWindow(view.hWnd, SW_HIDE);
		view.sleeping = true;
	}

	return false;
}

void Update()
{
	view.ms = GetTickCount();

	if (CheckTime())
		DelayedStart(conf.delay);

	if (CheckScreensaver())
		DelayedStart(conf.awake);

	if (view.starttime >= 0 && view.ms >= view.starttime && view.paused)
	{
		view.paused = false;
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
				UpdateFrame();	// forceably update on corners

				if (conf.randomize)
					view.y = RandomValue(view.sh * conf.top / 100, view.sh * conf.bottom / 100);

				if (view.step > 0)	// got to the left edge, the end
				{
					DelayedStart(conf.pause);
					return;
				}
			}

			view.y = ClampValue(view.y, 0, view.sh - view.h);
			SetWindowPos(view.hWnd, HWND_TOPMOST, view.x, view.y, view.w, view.h, SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	POINT pt;
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
					view.lockscreen = true;
					break;
				case WTS_SESSION_UNLOCK:
					if (view.lockscreen)
					{
						view.lockscreen = false;
						DelayedStart(conf.awake);
					}
					break;
			}
			break;

		case WM_CREATE:
		{
			view.hdcScreen = GetDC(NULL);
			view.hDC = CreateCompatibleDC(view.hdcScreen);

			int w = g_pImg->GetWidth();
			int h = g_pImg->GetHeight() / conf.frames;

			for (int i = 0; i < conf.frames; i++)
			{
				g_hBmp[i] = CreateCompatibleBitmap(view.hdcScreen, view.w, view.h);
				view.hOldDC = (HBITMAP) SelectObject(view.hDC, g_hBmp[i]);
				DrawImg(g_pImg, view.hDC, 0, -h * i);
				SelectObject(view.hDC, view.hOldDC);
			}
			view.hOldDC = NULL;
			break;
		}

		case WM_TIMER:
			Update();
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
			break;

		case WM_LBUTTONDOWN:
			GetCursorPos(&view.pt0);
			view.pressed = true;
			view.mx0 = view.mx;
			view.my0 = view.my;

			if (conf.draggable && !view.captured)
			{
				WINDOWPLACEMENT wp;
				GetWindowPlacement(hWnd, &wp);
				if (wp.showCmd != SW_MAXIMIZE)
				{
					GetWindowRect(hWnd, &view.rc);
					SetCapture(hWnd);
					view.captured = true;
					view.pt.x = view.mx;
					view.pt.y = view.my;
					ClientToScreen(hWnd, &view.pt);
				}
			}
			break;

		case WM_MOUSEMOVE:
			view.mx = (int)(short)LOWORD(lParam);
			view.my = (int)(short)HIWORD(lParam);

			if (view.captured)
			{
				POINT pt;
				pt.x = view.mx;
				pt.y = view.my;
				ClientToScreen(hWnd, &pt);
				view.x = view.rc.left + pt.x - view.pt.x;
				view.y = view.rc.top + pt.y - view.pt.y;
				SetWindowPos(hWnd, NULL, view.x, view.y, view.w, view.h, SWP_SHOWWINDOW);
			}
			break;

		case WM_LBUTTONUP:
			GetCursorPos(&pt);
			view.pressed = false;

			if (pt.x == view.pt0.x && pt.y == view.pt0.y)
			{
				ShellExecute(NULL, "open", conf.link, NULL, NULL, SW_SHOW);
				if (conf.autoclose)
					PostQuitMessage(0);
				if (conf.autohide)
					DelayedStart(conf.pause);
			}

			if (view.captured)
			{
				ReleaseCapture();
				view.captured = false;
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
	char szBuf[MAX_PATH];
	return GetPrivateProfileString("config", szKey, NULL, szBuf, MAX_PATH, g_szConfigFile) ? atoi(szBuf) : iDef;
}

const char *getStr(const char *szKey, const char *szDef, char *szBuf, int iSize)
{
	return GetPrivateProfileString("config", szKey, szDef, szBuf, iSize, g_szConfigFile) ? szBuf : szDef;
}

time_t getTime(const char *szKey, const char *szDef, char *szBuf, int iSize)
{
	const char *szTime = GetPrivateProfileString("config", szKey, szDef, szBuf, iSize, g_szConfigFile) ? szBuf : szDef;
	int Y, M, D, h, m, s;
	int res = sscanf(szTime, "%04d-%02d-%02d %02d:%02d:%02d", &Y, &M, &D, &h, &m, &s);
	struct tm tmv = { s, m, h, D, M - 1, Y - 1900, -1, -1, -1 };
	return mktime(&tmv);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	HWND hWnd = FindWindow("airshipclass", NULL);
	if (hWnd)
		PostMessage(hWnd, WM_CLOSE, 0, 0);

	HMODULE hUser32Dll =::GetModuleHandle("user32.dll");
	UpdateLayeredWindow = (PFN_UpdateLayeredWindow)::GetProcAddress(hUser32Dll, "UpdateLayeredWindow");

	LARGE_INTEGER q;
	QueryPerformanceCounter(&q);
	srand((unsigned int)q.QuadPart);

	GetModuleFileName(NULL, g_szConfigFile, MAX_PATH);
	PathRenameExtension(g_szConfigFile, ".ini");

	char szCurrentDir[MAX_PATH];
	GetModuleFileName(NULL, szCurrentDir, MAX_PATH);
	PathRemoveFileSpec(szCurrentDir);
	SetCurrentDirectory(szCurrentDir);

	conf.frames = getInt("frames", 2);
	conf.speed = getInt("speed", 1);
	conf.autoclose = getInt("autoclose", 0);
	conf.autohide = getInt("autohide", 0);
	conf.top = getInt("top", 25);
	conf.bottom = getInt("bottom", 75);
	conf.randomize = getInt("randomize", 75);
	conf.blocked = getInt("blocked", 0);
	conf.delay = getInt("delay", 0);
	conf.pause = getInt("pause", 0);
	conf.awake = getInt("awake", 1000);
	conf.draggable = getInt("draggable", 1);
	conf.animframes = getInt("animframes", 1);
	conf.animtime = getInt("animtime", 250);

	conf.image = getStr("image", "airship.png", g_szImage, MAX_PATH);
	conf.link = getStr("link", "https://en.wikipedia.org/wiki/Airship", g_szLink, MAX_PATH);
	conf.launch = getTime("launch", "2001-01-01 00:00:00", g_szBuf, MAX_PATH);
	conf.expire = getTime("expire", "2024-01-01 00:00:00", g_szBuf, MAX_PATH);

	g_pImg = LoadImg(conf.image);
	if (!g_pImg)
	{
		MessageBox(NULL, "Could not load image", conf.image, MB_OK);
		return 0;
	}

	view.w = g_pImg->GetWidth();
	view.h = g_pImg->GetHeight() / conf.frames;
	view.step = 1;
	view.hOldDC = NULL;
	view.paused = true;
	view.sleeping = true;
	view.screensaver = false;
	view.lockscreen = false;
	view.frame = 0;
	view.sw = GetSystemMetrics(SM_CXSCREEN);
	view.sh = GetSystemMetrics(SM_CYSCREEN);
	view.ms = GetTickCount();
	view.frametime = view.ms;
	view.starttime = -1;
	view.x = view.sw / 2;
	view.y = view.sh * (conf.top + (conf.bottom - conf.top) / 2) / 100;

	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC) WndProc;
	wc.hInstance = hInstance;
	wc.hbrBackground = NULL;
	wc.lpszClassName = "airshipclass";
	wc.hCursor = LoadCursor(NULL, IDC_HAND);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.lpszMenuName = NULL;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;

	RegisterClass(&wc);

	if (!(hWnd = CreateWindowEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName, wc.lpszClassName, WS_POPUP, 0, 0, view.w, view.h, NULL, NULL, wc.hInstance, NULL)))
	{
		MessageBox(NULL, "Could not create window", "Error", MB_OK);
		return 0;
	}

	view.hWnd = hWnd;

	HINSTANCE handle =::LoadLibrary("wtsapi32.dll");
	typedef DWORD(WINAPI * tWTSRegisterSessionNotification) (HWND, DWORD);
	tWTSRegisterSessionNotification pWTSRegisterSessionNotification = 0;
	pWTSRegisterSessionNotification = (tWTSRegisterSessionNotification)::GetProcAddress(handle, "WTSRegisterSessionNotification");
	if (pWTSRegisterSessionNotification)
		pWTSRegisterSessionNotification(hWnd, NOTIFY_FOR_THIS_SESSION);
	::FreeLibrary(handle);

	SetTimer(hWnd, 0, 0, NULL);

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}
