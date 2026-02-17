#include "window_icon_win32.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HICON gWindowIconSmall = NULL;
static HICON gWindowIconBig = NULL;

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

#else

void ApplyWin32WindowIcon(void *windowHandle, const char *iconPath)
{
    (void)windowHandle;
    (void)iconPath;
}

void ReleaseWin32WindowIcon(void)
{
}

#endif
