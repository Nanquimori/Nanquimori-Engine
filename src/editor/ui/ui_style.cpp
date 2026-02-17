#include "ui_style.h"

const UIStyle *GetUIStyle(void)
{
    static UIStyle style = {0};
    static bool initialized = false;
    if (!initialized)
    {
        style.panelBg = (Color){12, 12, 12, 255};
        style.panelBgAlt = (Color){0, 0, 0, 255};
        style.panelBgHover = (Color){34, 34, 34, 255};
        style.panelOverlay = (Color){0, 0, 0, 214};
        style.panelBorder = (Color){96, 96, 96, 255};
        style.panelBorderSoft = (Color){62, 62, 62, 255};
        style.textPrimary = (Color){242, 239, 231, 255};
        style.textSecondary = (Color){196, 192, 184, 255};
        style.textMuted = (Color){154, 150, 144, 255};
        style.accent = (Color){184, 38, 36, 255};
        style.accentSoft = (Color){130, 46, 42, 255};
        style.inputBg = (Color){20, 20, 20, 255};
        style.inputBorder = (Color){86, 86, 86, 255};
        style.inputText = (Color){242, 239, 231, 255};
        style.inputSelection = (Color){184, 38, 36, 92};
        style.caret = (Color){242, 239, 231, 255};
        style.itemBg = (Color){18, 18, 18, 255};
        style.itemHover = (Color){42, 42, 42, 255};
        style.itemActive = (Color){184, 38, 36, 255};
        style.buttonBg = (Color){18, 18, 18, 255};
        style.buttonBgHover = (Color){184, 38, 36, 255};
        style.buttonBorder = (Color){96, 96, 96, 255};
        style.buttonText = (Color){242, 239, 231, 255};
        style.buttonTextHover = (Color){242, 239, 231, 255};
        style.topBarBg = (Color){8, 8, 8, 255};
        style.topBarBorder = (Color){86, 86, 86, 255};
        initialized = true;
    }
    return &style;
}

Color UiTextForBackground(Color bg)
{
    const UIStyle *style = GetUIStyle();
    float l = (0.2126f * (float)bg.r + 0.7152f * (float)bg.g + 0.0722f * (float)bg.b) / 255.0f;
    return (l > 0.6f) ? style->buttonTextHover : style->textPrimary;
}
