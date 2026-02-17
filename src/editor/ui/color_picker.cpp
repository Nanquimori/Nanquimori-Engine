#include "color_picker.h"
#include "rlgl.h"
#include "raymath.h"
#include "ui_style.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PICKER_BG (GetUIStyle()->inputBg)
#define PICKER_BORDER (GetUIStyle()->panelBorder)
#define PICKER_TEXT (GetUIStyle()->textPrimary)
#define PICKER_TEXT_SECONDARY (GetUIStyle()->textSecondary)
#define PICKER_ACCENT (GetUIStyle()->accent)
#define PICKER_SELECTION (GetUIStyle()->inputSelection)

static bool ParseHexColor(const char *hex, Color *out)
{
    if (!hex || !out) return false;
    int r = 0, g = 0, b = 0;
    if (sscanf(hex, "#%02x%02x%02x", &r, &g, &b) == 3 ||
        sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3)
    {
        out->r = (unsigned char)r;
        out->g = (unsigned char)g;
        out->b = (unsigned char)b;
        out->a = 255;
        return true;
    }
    return false;
}

static void FormatHexColor(Color c, char *out)
{
    if (!out) return;
    snprintf(out, 8, "#%02X%02X%02X", c.r, c.g, c.b);
}

void ColorPickerOpen(ColorPickerState *state, Color color)
{
    if (!state)
        return;

    state->open = true;
    state->justOpened = true;
    state->hsv = ColorToHSV(color);
    state->hsv.y = Clamp(state->hsv.y, 0.0f, 1.0f);
    state->hsv.z = Clamp(state->hsv.z, 0.0f, 1.0f);
    state->dragWheel = false;
    state->dragValue = false;
    TextInputInit(&state->hexInput);
    FormatHexColor(ColorFromHSV(state->hsv.x, state->hsv.y, state->hsv.z), state->hexBuffer);
    state->hexInput.caret = (int)strlen(state->hexBuffer);
    state->hexInput.selStart = state->hexInput.caret;
    state->hexInput.selEnd = state->hexInput.caret;
    if (state->triangles == 0)
        state->triangles = 96;
}

void ColorPickerClose(ColorPickerState *state)
{
    if (state)
    {
        state->open = false;
        state->justOpened = false;
        TextInputInit(&state->hexInput);
    }
}

bool ColorPickerIsOpen(const ColorPickerState *state)
{
    return state && state->open;
}

Color ColorPickerDraw(ColorPickerState *state, Rectangle panel, const char *title)
{
    if (!state)
        return WHITE;

    Vector2 mouse = GetMousePosition();

    if (state->justOpened)
    {
        if (!IsMouseButtonDown(MOUSE_LEFT_BUTTON))
            state->justOpened = false;
    }
    else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !CheckCollisionPointRec(mouse, panel))
    {
        state->open = false;
    }

    DrawRectangleRec(panel, PICKER_BG);
    DrawRectangleLinesEx(panel, 1, PICKER_BORDER);

    const char *t = (title && title[0]) ? title : "Color";
    DrawText(t, (int)panel.x + 8, (int)panel.y + 6, 12, PICKER_TEXT);

    Rectangle closeBtn = {panel.x + panel.width - 18, panel.y + 4, 14, 14};
    DrawRectangleLinesEx(closeBtn, 1, PICKER_BORDER);
    DrawText("X", (int)closeBtn.x + 4, (int)closeBtn.y + 1, 12, PICKER_TEXT);
    if (CheckCollisionPointRec(mouse, closeBtn) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        state->open = false;

    float pad = 10.0f;
    float headerSpace = 22.0f;
    float valueGap = 12.0f;
    float valueHeight = 12.0f;
    float hexGap = 10.0f;
    float hexHeight = 20.0f;

    float availW = panel.width - pad * 2.0f;
    float availH = panel.height - headerSpace - valueGap - valueHeight - hexGap - hexHeight - pad;
    if (availH < 80.0f)
        availH = 80.0f;

    float wheelSize = (availW < availH) ? availW : availH;
    if (wheelSize > 150.0f)
        wheelSize = 150.0f;
    if (wheelSize < 130.0f)
        wheelSize = 130.0f;

    float wheelX = panel.x + (panel.width - wheelSize) * 0.5f;
    float wheelY = panel.y + headerSpace;
    Rectangle wheel = {wheelX, wheelY, wheelSize, wheelSize};
    float radius = wheel.width * 0.5f;
    Vector2 center = {wheel.x + radius, wheel.y + radius};

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && Vector2Distance(mouse, center) <= radius + 6.0f)
        state->dragWheel = true;
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
        state->dragWheel = false;

    if (state->dragWheel)
    {
        Vector2 delta = Vector2Subtract(mouse, center);
        float dist = Vector2Length(delta);
        if (dist > radius)
            delta = Vector2Scale(delta, radius / dist);

        float angle = atan2f(delta.x, -delta.y);
        if (angle < 0.0f)
            angle += PI * 2.0f;

        state->hsv.x = angle * 180.0f / PI;
        state->hsv.y = Clamp(Vector2Length(delta) / radius, 0.0f, 1.0f);
    }

    rlBegin(RL_TRIANGLES);
    for (unsigned int i = 0; i < state->triangles; i++)
    {
        float a0 = (PI * 2.0f) * ((float)i / (float)state->triangles);
        float a1 = (PI * 2.0f) * ((float)(i + 1) / (float)state->triangles);

        Vector2 p0 = {center.x + sinf(a0) * radius, center.y - cosf(a0) * radius};
        Vector2 p1 = {center.x + sinf(a1) * radius, center.y - cosf(a1) * radius};

        float hue0 = (a0 / (PI * 2.0f)) * 360.0f;
        float hue1 = (a1 / (PI * 2.0f)) * 360.0f;

        Color c0 = ColorFromHSV(hue0, 1.0f, 1.0f);
        Color c1 = ColorFromHSV(hue1, 1.0f, 1.0f);

        rlColor4ub(c0.r, c0.g, c0.b, 255);
        rlVertex2f(p0.x, p0.y);

        rlColor4f(state->hsv.z, state->hsv.z, state->hsv.z, 1.0f);
        rlVertex2f(center.x, center.y);

        rlColor4ub(c1.r, c1.g, c1.b, 255);
        rlVertex2f(p1.x, p1.y);
    }
    rlEnd();

    float handleAngle = (state->hsv.x / 360.0f) * (PI * 2.0f);
    float handleDist = state->hsv.y * radius;
    Vector2 handle = {center.x + sinf(handleAngle) * handleDist, center.y - cosf(handleAngle) * handleDist};
    Color handleColor = (state->hsv.y <= 0.5f && state->hsv.z <= 0.5f) ? DARKGRAY : BLACK;
    DrawCircleLinesV(handle, 4.0f, handleColor);

    Rectangle valueRect = {wheel.x, wheel.y + wheel.height + valueGap, wheel.width, valueHeight};

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mouse, valueRect))
        state->dragValue = true;
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
        state->dragValue = false;

    if (state->dragValue)
    {
        float v = (mouse.x - valueRect.x) / valueRect.width;
        state->hsv.z = Clamp(v, 0.0f, 1.0f);
    }

    Color hueColor = ColorFromHSV(state->hsv.x, state->hsv.y, 1.0f);
    DrawRectangleGradientH((int)valueRect.x, (int)valueRect.y, (int)valueRect.width, (int)valueRect.height, BLACK, hueColor);
    DrawRectangleLinesEx(valueRect, 1, PICKER_BORDER);

    float knobX = valueRect.x + state->hsv.z * valueRect.width;
    DrawRectangle((int)(knobX - 2), (int)(valueRect.y - 2), 4, (int)(valueRect.height + 4), PICKER_ACCENT);

    Color result = ColorFromHSV(state->hsv.x, state->hsv.y, state->hsv.z);
    if (!state->hexInput.active)
        FormatHexColor(result, state->hexBuffer);

    float hexY = valueRect.y + valueRect.height + hexGap;

    const char *hexLabel = "HEX";
    float hexLabelX = panel.x + pad;
    float hexLabelY = hexY + (hexHeight - 12.0f) * 0.5f + 1.0f;
    DrawText(hexLabel, (int)hexLabelX, (int)hexLabelY, 12, PICKER_TEXT_SECONDARY);

    float hexBoxW = 70.0f;
    float hexBoxX = hexLabelX + 32.0f;
    float maxHexX = panel.x + panel.width - hexBoxW - pad;
    if (hexBoxX > maxHexX)
        hexBoxX = maxHexX;
    Rectangle hexBox = {hexBoxX, hexY, hexBoxW, hexHeight};
    TextInputConfig cfg = {0};
    cfg.fontSize = 12;
    cfg.padding = 6;
    cfg.textColor = PICKER_TEXT;
    cfg.bgColor = GetUIStyle()->inputBg;
    cfg.borderColor = PICKER_BORDER;
    cfg.selectionColor = PICKER_SELECTION;
    cfg.caretColor = GetUIStyle()->caret;
    cfg.filter = TEXT_INPUT_FILTER_HEX;
    cfg.uppercase = true;
    cfg.allowHashPrefix = true;
    cfg.allowInput = true;

    int flags = TextInputDraw(hexBox, state->hexBuffer, (int)sizeof(state->hexBuffer), &state->hexInput, &cfg);
    if (flags & TEXT_INPUT_CHANGED)
    {
        Color parsed;
        if (ParseHexColor(state->hexBuffer, &parsed))
        {
            state->hsv = ColorToHSV(parsed);
            state->hsv.y = Clamp(state->hsv.y, 0.0f, 1.0f);
            state->hsv.z = Clamp(state->hsv.z, 0.0f, 1.0f);
        }
    }
    if (flags & TEXT_INPUT_SUBMITTED)
    {
        Color parsed;
        if (ParseHexColor(state->hexBuffer, &parsed))
        {
            state->hsv = ColorToHSV(parsed);
            state->hsv.y = Clamp(state->hsv.y, 0.0f, 1.0f);
            state->hsv.z = Clamp(state->hsv.z, 0.0f, 1.0f);
        }
        state->hexInput.active = false;
    }

    return result;
}
