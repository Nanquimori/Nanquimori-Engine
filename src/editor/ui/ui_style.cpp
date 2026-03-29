#include "ui_style.h"
#include <math.h>

static Color LerpColor(Color a, Color b, float t)
{
    Color out = {0};
    out.r = (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t + 0.5f);
    out.g = (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t + 0.5f);
    out.b = (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t + 0.5f);
    out.a = (unsigned char)((float)a.a + ((float)b.a - (float)a.a) * t + 0.5f);
    return out;
}

static void DrawBlender266HeaderBase(Rectangle bounds, Color top, Color bottom, Color border, Color topHighlight, Color innerShade)
{
    DrawRectangleGradientV((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height, top, bottom);
    DrawRectangleLinesEx(bounds, 1.0f, border);
    DrawLine((int)bounds.x + 1, (int)bounds.y + 1, (int)(bounds.x + bounds.width) - 2, (int)bounds.y + 1, topHighlight);
    DrawLine((int)bounds.x + 1, (int)(bounds.y + bounds.height) - 2, (int)(bounds.x + bounds.width) - 2, (int)(bounds.y + bounds.height) - 2, innerShade);
}

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

void DrawBlender266Header(Rectangle bounds, const char *title, int textSize)
{
    Color base = ColorAlpha((Color){0xCF, 0xE6, 0xE1, 255}, 0.75f);
    Color top = LerpColor(base, WHITE, 0.16f);
    Color bottom = LerpColor(base, (Color){74, 83, 81, 255}, 0.28f);
    Color border = LerpColor(base, (Color){54, 60, 58, 255}, 0.55f);
    Color topHighlight = ColorAlpha(WHITE, 0.30f);
    Color innerShade = ColorAlpha((Color){74, 83, 81, 255}, 0.42f);
    Color textColor = BLACK;
    Color textEmboss = ColorAlpha(WHITE, 0.35f);

    DrawBlender266HeaderBase(bounds, top, bottom, border, topHighlight, innerShade);

    int textX = (int)bounds.x + 10;
    int textY = (int)(bounds.y + (bounds.height - (float)textSize) * 0.5f) - 1;
    DrawText(title, textX, textY - 1, textSize, textEmboss);
    DrawText(title, textX, textY, textSize, textColor);
}

void DrawBlender266CollapsibleHeader(Rectangle bounds, const char *title, int textSize, bool expanded, bool hovered)
{
    Color base = ColorAlpha((Color){0xCF, 0xE6, 0xE1, 255}, hovered ? 0.84f : 0.75f);
    Color top = LerpColor(base, WHITE, hovered ? 0.22f : 0.16f);
    Color bottom = LerpColor(base, (Color){74, 83, 81, 255}, hovered ? 0.22f : 0.28f);
    Color border = LerpColor(base, (Color){54, 60, 58, 255}, 0.55f);
    Color topHighlight = ColorAlpha(WHITE, hovered ? 0.38f : 0.30f);
    Color innerShade = ColorAlpha((Color){74, 83, 81, 255}, 0.42f);
    Color textColor = BLACK;
    Color textEmboss = ColorAlpha(WHITE, 0.35f);
    Color glyphColor = ColorAlpha(BLACK, 0.90f);
    Color glyphShadow = ColorAlpha(WHITE, 0.28f);

    DrawBlender266HeaderBase(bounds, top, bottom, border, topHighlight, innerShade);

    float cx = floorf(bounds.x + 10.0f) + 0.5f;
    float cy = floorf(bounds.y + bounds.height * 0.5f) + 0.5f;
    Vector2 p1 = {0};
    Vector2 p2 = {0};
    Vector2 p3 = {0};
    if (expanded)
    {
        p1 = (Vector2){cx - 4.0f, cy - 2.0f};
        p2 = (Vector2){cx, cy + 2.5f};
        p3 = (Vector2){cx + 4.0f, cy - 2.0f};
    }
    else
    {
        p1 = (Vector2){cx - 2.5f, cy - 4.0f};
        p2 = (Vector2){cx + 2.0f, cy};
        p3 = (Vector2){cx - 2.5f, cy + 4.0f};
    }
    DrawLineEx((Vector2){p1.x - 1.0f, p1.y - 1.0f}, (Vector2){p2.x - 1.0f, p2.y - 1.0f}, 2.0f, glyphShadow);
    DrawLineEx((Vector2){p2.x - 1.0f, p2.y - 1.0f}, (Vector2){p3.x - 1.0f, p3.y - 1.0f}, 2.0f, glyphShadow);
    DrawLineEx(p1, p2, 2.0f, glyphColor);
    DrawLineEx(p2, p3, 2.0f, glyphColor);

    int textX = (int)bounds.x + 20;
    int textY = (int)(bounds.y + (bounds.height - (float)textSize) * 0.5f) - 1;
    DrawText(title, textX, textY - 1, textSize, textEmboss);
    DrawText(title, textX, textY, textSize, textColor);
}
