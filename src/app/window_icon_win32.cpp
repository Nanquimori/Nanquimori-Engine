#include "window_icon_win32.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static HICON gWindowIconSmall = NULL;
static HICON gWindowIconBig = NULL;
static bool gConsoleStreamsReady = false;

void ApplyWin32WindowIcon(void *windowHandle, const char *iconPath)
{
    if (!windowHandle || !iconPath || iconPath[0] == '\0')
        return;

    HWND hwnd = (HWND)windowHandle;

    if (gWindowIconSmall)
    {
        DestroyIcon(gWindowIconSmall);
        gWindowIconSmall = NULL;
    }
    if (gWindowIconBig)
    {
        DestroyIcon(gWindowIconBig);
        gWindowIconBig = NULL;
    }

    gWindowIconSmall = (HICON)LoadImageA(NULL, iconPath, IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON),
                                          GetSystemMetrics(SM_CYSMICON),
                                          LR_LOADFROMFILE);
    gWindowIconBig = (HICON)LoadImageA(NULL, iconPath, IMAGE_ICON,
                                        GetSystemMetrics(SM_CXICON),
                                        GetSystemMetrics(SM_CYICON),
                                        LR_LOADFROMFILE);

    if (gWindowIconSmall)
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)gWindowIconSmall);
    if (gWindowIconBig)
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)gWindowIconBig);
}

void ReleaseWin32WindowIcon(void)
{
    if (gWindowIconSmall)
    {
        DestroyIcon(gWindowIconSmall);
        gWindowIconSmall = NULL;
    }
    if (gWindowIconBig)
    {
        DestroyIcon(gWindowIconBig);
        gWindowIconBig = NULL;
    }
}

void SetWin32ConsoleVisible(bool visible)
{
    HWND consoleWindow = GetConsoleWindow();
    if (visible)
    {
        if (!consoleWindow)
        {
            if (!AttachConsole(ATTACH_PARENT_PROCESS))
                AllocConsole();

            consoleWindow = GetConsoleWindow();
            if (consoleWindow && !gConsoleStreamsReady)
            {
                freopen("CONOUT$", "w", stdout);
                freopen("CONOUT$", "w", stderr);
                freopen("CONIN$", "r", stdin);
                gConsoleStreamsReady = true;
            }
        }

        if (consoleWindow)
            ShowWindow(consoleWindow, SW_SHOW);
    }
    else if (consoleWindow)
    {
        ShowWindow(consoleWindow, SW_HIDE);
    }
}

void SetWin32WindowBounds(void *windowHandle, int x, int y, int width, int height, bool topmost)
{
    if (!windowHandle || width <= 0 || height <= 0)
        return;

    HWND hwnd = (HWND)windowHandle;
    SetWindowPos(hwnd,
                 topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 x,
                 y,
                 width,
                 height,
                 SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    SetForegroundWindow(hwnd);
}

#else

void ApplyWin32WindowIcon(void *windowHandle, const char *iconPath)
{
    (void)windowHandle;
    (void)iconPath;
}

void ReleaseWin32WindowIcon(void)
{
}

void SetWin32ConsoleVisible(bool visible)
{
    (void)visible;
}

void SetWin32WindowBounds(void *windowHandle, int x, int y, int width, int height, bool topmost)
{
    (void)windowHandle;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)topmost;
}

#endif
