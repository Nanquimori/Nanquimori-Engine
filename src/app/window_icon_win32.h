#ifndef WINDOW_ICON_WIN32_H
#define WINDOW_ICON_WIN32_H

typedef void (*Win32ConsoleCloseCallback)(void);

void ApplyWin32WindowIcon(void *windowHandle, const char *iconPath);
void ReleaseWin32WindowIcon(void);
void SetWin32ConsoleVisible(bool visible);
bool GetWin32CurrentDisplaySize(int *width, int *height);
void SetWin32WindowBounds(void *windowHandle, int x, int y, int width, int height, bool topmost);
void SetWin32WindowMaximized(void *windowHandle, bool maximized);
void SetWin32ConsoleCloseCallback(Win32ConsoleCloseCallback callback);
void EnableWin32ConsoleCloseHandler(bool enabled);

#endif // WINDOW_ICON_WIN32_H
