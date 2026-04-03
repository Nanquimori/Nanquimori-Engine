#ifndef WINDOW_ICON_WIN32_H
#define WINDOW_ICON_WIN32_H

void ApplyWin32WindowIcon(void *windowHandle, const char *iconPath);
void ReleaseWin32WindowIcon(void);
void SetWin32ConsoleVisible(bool visible);
bool GetWin32CurrentDisplaySize(int *width, int *height);
void SetWin32WindowBounds(void *windowHandle, int x, int y, int width, int height, bool topmost);
void SetWin32WindowMaximized(void *windowHandle, bool maximized);

#endif // WINDOW_ICON_WIN32_H
