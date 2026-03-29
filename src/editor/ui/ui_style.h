#ifndef UI_STYLE_H
#define UI_STYLE_H

#include "raylib.h"

typedef struct
{
    Color panelBg;
    Color panelBgAlt;
    Color panelBgHover;
    Color panelOverlay;
    Color panelBorder;
    Color panelBorderSoft;
    Color textPrimary;
    Color textSecondary;
    Color textMuted;
    Color accent;
    Color accentSoft;
    Color inputBg;
    Color inputBorder;
    Color inputText;
    Color inputSelection;
    Color caret;
    Color itemBg;
    Color itemHover;
    Color itemActive;
    Color buttonBg;
    Color buttonBgHover;
    Color buttonBorder;
    Color buttonText;
    Color buttonTextHover;
    Color topBarBg;
    Color topBarBorder;
} UIStyle;

const UIStyle *GetUIStyle(void);
Color UiTextForBackground(Color bg);
void DrawBlender266Header(Rectangle bounds, const char *title, int textSize);
void DrawBlender266CollapsibleHeader(Rectangle bounds, const char *title, int textSize, bool expanded, bool hovered);

#endif // UI_STYLE_H
