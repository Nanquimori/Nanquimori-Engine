#include "ui_tooltip.h"
#include "ui_style.h"
#include <cstdio>
#include <cstring>

typedef struct
{
    bool requested;
    Rectangle requestedAnchor;
    char requestedId[64];
    char requestedTitle[96];
    char requestedDescription[192];
    Rectangle activeAnchor;
    char activeId[64];
    char activeTitle[96];
    char activeDescription[192];
    double hoverStartTime;
} UITooltipState;

static UITooltipState gTooltip = {0};

static void CopyTooltipString(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0)
        return;

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

void BeginUITooltipFrame(void)
{
    gTooltip.requested = false;
    gTooltip.requestedAnchor = (Rectangle){0};
    gTooltip.requestedId[0] = '\0';
    gTooltip.requestedTitle[0] = '\0';
    gTooltip.requestedDescription[0] = '\0';
}

void SetUITooltip(Rectangle anchor, const char *id, const char *title, const char *description)
{
    if (!id || id[0] == '\0' || !title || title[0] == '\0')
        return;

    gTooltip.requested = true;
    gTooltip.requestedAnchor = anchor;
    CopyTooltipString(gTooltip.requestedId, sizeof(gTooltip.requestedId), id);
    CopyTooltipString(gTooltip.requestedTitle, sizeof(gTooltip.requestedTitle), title);
    CopyTooltipString(gTooltip.requestedDescription, sizeof(gTooltip.requestedDescription), description);
}

static void SyncActiveTooltip(void)
{
    if (!gTooltip.requested)
    {
        gTooltip.activeId[0] = '\0';
        gTooltip.activeTitle[0] = '\0';
        gTooltip.activeDescription[0] = '\0';
        gTooltip.hoverStartTime = 0.0;
        return;
    }

    bool changed = strcmp(gTooltip.activeId, gTooltip.requestedId) != 0;
    gTooltip.activeAnchor = gTooltip.requestedAnchor;

    if (!changed)
        return;

    CopyTooltipString(gTooltip.activeId, sizeof(gTooltip.activeId), gTooltip.requestedId);
    CopyTooltipString(gTooltip.activeTitle, sizeof(gTooltip.activeTitle), gTooltip.requestedTitle);
    CopyTooltipString(gTooltip.activeDescription, sizeof(gTooltip.activeDescription), gTooltip.requestedDescription);
    gTooltip.hoverStartTime = GetTime();
}

void DrawUITooltip(void)
{
    SyncActiveTooltip();

    if (gTooltip.activeId[0] == '\0')
        return;
    if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) || IsMouseButtonDown(MOUSE_RIGHT_BUTTON) || IsMouseButtonDown(MOUSE_MIDDLE_BUTTON))
        return;
    if ((GetTime() - gTooltip.hoverStartTime) < 0.24)
        return;

    const UIStyle *style = GetUIStyle();
    const int titleFontSize = 12;
    const int descFontSize = 10;
    const int paddingX = 10;
    const int paddingY = 8;
    const int lineGap = 4;

    int titleWidth = MeasureText(gTooltip.activeTitle, titleFontSize);
    int descWidth = (gTooltip.activeDescription[0] != '\0') ? MeasureText(gTooltip.activeDescription, descFontSize) : 0;
    int boxWidth = titleWidth;
    if (descWidth > boxWidth)
        boxWidth = descWidth;
    boxWidth += paddingX * 2;
    if (boxWidth < 140)
        boxWidth = 140;
    if (boxWidth > 360)
        boxWidth = 360;

    int boxHeight = paddingY * 2 + titleFontSize;
    if (gTooltip.activeDescription[0] != '\0')
        boxHeight += lineGap + descFontSize;

    Vector2 mouse = GetMousePosition();
    float x = mouse.x + 18.0f;
    float y = mouse.y + 20.0f;
    if (x + (float)boxWidth > (float)GetScreenWidth() - 8.0f)
        x = mouse.x - (float)boxWidth - 18.0f;
    if (y + (float)boxHeight > (float)GetScreenHeight() - 8.0f)
        y = mouse.y - (float)boxHeight - 18.0f;
    if (x < 8.0f)
        x = 8.0f;
    if (y < 8.0f)
        y = 8.0f;

    Rectangle box = {x, y, (float)boxWidth, (float)boxHeight};
    Rectangle accentBar = {box.x, box.y, 3.0f, box.height};
    Color bg = (Color){10, 10, 10, 238};
    Color border = style->panelBorder;
    Color shadow = Fade(BLACK, 0.35f);

    DrawRectangleRec((Rectangle){box.x + 3.0f, box.y + 3.0f, box.width, box.height}, shadow);
    DrawRectangleRec(box, bg);
    DrawRectangleRec(accentBar, style->accent);
    DrawRectangleLinesEx(box, 1.0f, border);

    int textX = (int)box.x + paddingX + 3;
    int titleY = (int)box.y + paddingY;
    DrawText(gTooltip.activeTitle, textX, titleY, titleFontSize, style->textPrimary);

    if (gTooltip.activeDescription[0] != '\0')
    {
        int descY = titleY + titleFontSize + lineGap;
        char fitted[192] = {0};
        snprintf(fitted, sizeof(fitted), "%s", gTooltip.activeDescription);
        fitted[sizeof(fitted) - 1] = '\0';
        DrawText(fitted, textX, descY, descFontSize, style->textSecondary);
    }
}
