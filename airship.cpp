// (c) 2013 joric^proxium

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <time.h>

#pragma comment(lib, "gdiplus")
#pragma comment(lib, "shlwapi")
#pragma comment( linker, "/subsystem:windows" )

struct Conf_t {
	int frames;
	int speed;
	int autoclose;
	int blocked;
	int top;
	int bottom;
	int randomize;
	int delay;
	int pause;
	int repeat;
	const char *image;
	const char *link;
};

struct View_t {
	Gdiplus::Image * pImg;
	int x, y;
	int w, h;
	int step;
	int wait;
	int mx, my;
	int mx0, my0;
	int xmax, ymax;
	bool captured;
	bool pressed;
	RECT rc;
	POINT pt, pt0;
	HDC hdcScreen;
	HDC hDC;
	HBITMAP hOldDC;
	HBITMAP hBmp[16];
};

Conf_t conf;
View_t view;

char g_szConfigFile[MAX_PATH];
char g_szImage[MAX_PATH];
char g_szLink[MAX_PATH];

#ifndef UpdateLayeredWindow
typedef BOOL(WINAPI * PFN_UpdateLayeredWindow) (HWND, HDC, POINT *, SIZE *, HDC, POINT *, COLORREF, BLENDFUNCTION *, DWORD);
PFN_UpdateLayeredWindow UpdateLayeredWindow;
#endif

#ifndef WS_EX_LAYERED
#define WS_EX_LAYERED 0x00080000
#endif

#ifndef ULW_ALPHA
#define ULW_ALPHA 0x00000002
#endif

#ifndef WS_EX_NOACTIVATE
#define WS_EX_NOACTIVATE 0x08000000L
#endif

#ifndef USER_TIMER_MINIMUM
#define USER_TIMER_MINIMUM 0x0000000A
#endif

ULONG_PTR g_gdiplusToken;

Gdiplus::Image * LoadImg(const char *szFile)
{
	Gdiplus::Image * pImage;
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
	wchar_t mb[MAX_PATH];
	MultiByteToWideChar(CP_ACP, 0, szFile, strlen(szFile), mb, MAX_PATH);
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

void SetFrame(HWND hWnd, int i)
{
	if (i > conf.frames)
		i = 0;

	if (view.hOldDC)
		SelectObject(view.hDC, view.hOldDC);

	view.hOldDC = (HBITMAP) SelectObject(view.hDC, view.hBmp[i]);

	BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	POINT ptPos = { 0, 0 };
	POINT ptSrc = { 0, 0 };
	SIZE sizeWnd = { view.w, view.h };
	UpdateLayeredWindow(hWnd, view.hdcScreen, &ptPos, &sizeWnd, view.hDC, &ptSrc, 0, &blend, ULW_ALPHA);
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

		case WM_CREATE:
		{
			SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST);

			view.hdcScreen = GetDC(NULL);
			view.hDC = CreateCompatibleDC(view.hdcScreen);
			for (int i = 0; i < conf.frames; i++)
			{
				view.hBmp[i] = CreateCompatibleBitmap(view.hdcScreen, view.w, view.h);
				view.hOldDC = (HBITMAP) SelectObject(view.hDC, view.hBmp[i]);
				DrawImg(view.pImg, view.hDC, 0, -view.h * i);
				SelectObject(view.hDC, view.hOldDC);
			}
			SetFrame(hWnd, view.step > 0 ? 0 : 1);
			break;
		}

		case WM_TIMER:
			if (wParam==1)
			{
				view.wait = 0;
				KillTimer ( hWnd, 1 );

				if ( conf.randomize )
				{
					float r = (float)rand()/(float)RAND_MAX;
					int y0 = view.ymax * conf.top / 100;
					int y1 = view.ymax * conf.bottom / 100;
					view.y = y0 + (int)((y1-y0) * r);
				}
			} 
			else if (view.wait==0 && !view.captured)
			{
				view.x += conf.speed * view.step;

				if (view.x < -view.w || view.x > view.xmax)
				{
					view.wait = (view.step<0 ? conf.repeat : conf.pause);
					SetTimer(hWnd, 1, view.wait, NULL);

					view.step *= -1;
					SetFrame(hWnd, view.step > 0 ? 0 : 1);
				}

				view.y = view.y < 0 ? 0 : view.y > view.ymax - view.h ? view.ymax - view.h : view.y;
				SetWindowPos(hWnd, NULL, view.x, view.y, view.w, view.h, SWP_NOZORDER | SWP_NOACTIVATE);
				SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
			break;

		case WM_DESTROY:
			PostQuitMessage(0);
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
			if (pt.x == view.pt0.x && pt.y == view.pt0.y)
			{
				ShellExecute(NULL, "open", conf.link, NULL, NULL, SW_SHOW);
				if (conf.autoclose)
					PostQuitMessage(0);
			}

			view.pressed = false;
			if (view.captured)
			{
				ReleaseCapture();
				view.captured = false;
			}
			break;

		case WM_LBUTTONDOWN:
			GetCursorPos(&view.pt0);
			view.pressed = true;
			view.mx0 = view.mx;
			view.my0 = view.my;

			if (!view.captured)
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	LARGE_INTEGER q;
	QueryPerformanceCounter(&q);
	srand ((unsigned int)q.QuadPart);

	GetModuleFileName(NULL, g_szConfigFile, MAX_PATH);
	PathRenameExtension(g_szConfigFile, ".ini");

	char szCurrentDir[MAX_PATH];
	GetModuleFileName(NULL, szCurrentDir, MAX_PATH);
	PathRemoveFileSpec(szCurrentDir);
	SetCurrentDirectory(szCurrentDir);

	conf.frames = getInt("frames", 2);
	conf.speed = getInt("speed", 1);
	conf.autoclose = getInt("autoclose", 0);
	conf.top = getInt("top", 25);
	conf.bottom = getInt("bottom", 75);
	conf.randomize = getInt("randomize", 75);
	conf.blocked = getInt("blocked", 0);
	conf.delay = getInt("delay", 0);
	conf.pause = getInt("pause", 0);
	conf.repeat = getInt("repeat", 0);
	conf.image = getStr("image", "airship.png", g_szImage, MAX_PATH);
	conf.link = getStr("link", "https://en.wikipedia.org/wiki/Airship", g_szLink, MAX_PATH);

	view.step = 1;
	view.hOldDC = NULL;

	view.pImg = LoadImg(conf.image);
	if (!view.pImg)
	{
		MessageBox(NULL, "Could not load image", conf.image, MB_OK);
		return 0;
	}

	view.w = view.pImg->GetWidth();
	view.h = view.pImg->GetHeight() / conf.frames;

	HMODULE hUser32Dll =::GetModuleHandle("user32.dll");
	UpdateLayeredWindow = (PFN_UpdateLayeredWindow)::GetProcAddress(hUser32Dll, "UpdateLayeredWindow");

	MSG msg;
	HWND hWnd;
	WNDCLASS wc;

	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = (WNDPROC) WndProc;
	wc.hInstance = GetModuleHandle(NULL);
	wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
	wc.lpszClassName = "main";
	wc.hCursor = LoadCursor(NULL, IDC_HAND);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.lpszMenuName = NULL;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;

	RegisterClass(&wc);

	if (!(hWnd = CreateWindow(wc.lpszClassName, wc.lpszClassName, WS_POPUP, 0, 0, view.w, view.h, NULL, NULL, wc.hInstance, NULL)))
	{
		MessageBox(NULL, "Could not create window", "Error", MB_OK);
		return 0;
	}

	view.wait = conf.delay;
	SetTimer(hWnd, 1, view.wait, NULL);
	SetTimer(hWnd, 0, USER_TIMER_MINIMUM, NULL);

	RECT rc;

	SetRect(&rc, 0, 0, view.w, view.h);

	view.w = rc.right - rc.left;
	view.h = rc.bottom - rc.top;

	view.xmax = GetSystemMetrics(SM_CXSCREEN);
	view.ymax = GetSystemMetrics(SM_CYSCREEN);
	view.x = -view.w;
	view.y = view.ymax * (conf.top + (conf.bottom-conf.top)/2 ) / 100;

	AdjustWindowRectEx(&rc, GetWindowLong(hWnd, GWL_STYLE), GetMenu(hWnd) != NULL, GetWindowLong(hWnd, GWL_EXSTYLE));

	SetWindowPos(hWnd, NULL, view.x, view.y, view.w, view.h, SWP_NOZORDER | SWP_NOACTIVATE);

	ShowWindow(hWnd, SW_SHOWNOACTIVATE);
	UpdateWindow(hWnd);

	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return (int)msg.wParam;
}
