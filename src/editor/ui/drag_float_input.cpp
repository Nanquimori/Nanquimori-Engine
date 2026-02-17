#include "editor/ui/drag_float_input.h"
#include "raymath.h"
#include "editor/ui/ui_style.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const float DRAG_THRESHOLD = 4.0f;

void DragFloatInputInit(DragFloatInputState *state)
{
    if (!state)
        return;
    TextInputInit(&state->text);
    state->pressed = false;
    state->dragging = false;
    state->pressAllowDrag = false;
    state->cursorHidden = false;
    state->dragValueActive = false;
    state->dragValue = 0.0f;
    state->pressPos = (Vector2){0, 0};
    state->anchorPos = (Vector2){0, 0};
}

bool DragFloatInputIsActive(const DragFloatInputState *state)
{
    if (!state)
        return false;
    return state->pressed || state->dragging || state->text.active || state->text.mouseSelecting;
}

void DragFloatInputFormat(char *dst, int dstSize, float value)
{
    if (!dst || dstSize <= 0)
        return;
    snprintf(dst, (size_t)dstSize, "%.3f", value);
    dst[dstSize - 1] = '\0';

    size_t len = strlen(dst);
    while (len > 1 && dst[len - 1] == '0')
    {
        dst[len - 1] = '\0';
        len--;
    }
    if (len > 1 && dst[len - 1] == '.')
        dst[len - 1] = '\0';

    if (strcmp(dst, "-0") == 0)
    {
        strncpy(dst, "0", (size_t)dstSize - 1);
        dst[dstSize - 1] = '\0';
    }
}

bool DragFloatInputParse(const char *text, float *outValue)
{
    if (!text || !outValue)
        return false;
    char *end = nullptr;
    float v = strtof(text, &end);
    if (text == end)
        return false;
    while (*end == ' ' || *end == '\t')
        end++;
    if (*end != '\0')
        return false;
    *outValue = v;
    return true;
}

int DragFloatInputDraw(Rectangle box, char *buffer, int bufferSize, DragFloatInputState *state, float *value, const DragFloatInputConfig *config)
{
    if (!buffer || bufferSize <= 0 || !state || !value)
        return 0;

    DragFloatInputConfig cfg = {0};
    if (config)
        cfg = *config;
    else
        cfg.allowInput = true;
    if (cfg.fontSize <= 0)
        cfg.fontSize = 12;
    if (cfg.padding < 0)
        cfg.padding = 4;
    const UIStyle *style = GetUIStyle();
    if (cfg.textColor.a == 0)
        cfg.textColor = style->inputText;
    if (cfg.bgColor.a == 0)
        cfg.bgColor = style->inputBg;
    if (cfg.borderColor.a == 0)
        cfg.borderColor = style->inputBorder;
    if (cfg.selectionColor.a == 0)
        cfg.selectionColor = style->inputSelection;
    if (cfg.caretColor.a == 0)
        cfg.caretColor = style->caret;
    if (cfg.dragSpeed == 0.0f)
        cfg.dragSpeed = 0.01f;

    int flags = 0;
    Vector2 mouse = GetMousePosition();
    bool over = CheckCollisionPointRec(mouse, box);

    if (cfg.allowInput && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && over)
    {
        state->pressed = true;
        state->dragging = false;
        state->pressAllowDrag = !state->text.active;
        state->dragValueActive = false;
        state->pressPos = mouse;
        state->anchorPos = mouse;
    }

    if (state->pressed)
    {
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            if (!state->dragging && state->pressAllowDrag)
            {
                float dx = mouse.x - state->pressPos.x;
                float dy = mouse.y - state->pressPos.y;
                if (fabsf(dx) + fabsf(dy) >= DRAG_THRESHOLD)
                {
                    state->dragging = true;
                    state->text.active = false;
                    state->text.mouseSelecting = false;
                    state->text.selStart = state->text.caret;
                    state->text.selEnd = state->text.caret;
                    state->dragValue = *value;
                    state->dragValueActive = true;
                    if (!state->cursorHidden)
                    {
                        HideCursor();
                        state->cursorHidden = true;
                    }
                }
            }

            if (state->dragging)
            {
                float delta = mouse.x - state->anchorPos.x;
                if (delta != 0.0f)
                {
                    float speed = cfg.dragSpeed;
                    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                    {
                        speed = (cfg.fineDragSpeed > 0.0f) ? cfg.fineDragSpeed : (cfg.dragSpeed * 0.1f);
                    }
                    if (!state->dragValueActive)
                    {
                        state->dragValue = *value;
                        state->dragValueActive = true;
                    }

                    state->dragValue += delta * speed;
                    if (cfg.clamp)
                        state->dragValue = Clamp(state->dragValue, cfg.minValue, cfg.maxValue);

                    float outValue = state->dragValue;
                    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
                        outValue = roundf(outValue);
                    if (cfg.clamp)
                        outValue = Clamp(outValue, cfg.minValue, cfg.maxValue);

                    if (outValue != *value)
                    {
                        *value = outValue;
                        DragFloatInputFormat(buffer, bufferSize, *value);
                    }
                    flags |= DRAG_FLOAT_INPUT_CHANGED;
                }

                SetMousePosition((int)state->anchorPos.x, (int)state->anchorPos.y);
                flags |= DRAG_FLOAT_INPUT_DRAGGING;
            }
        }
        else
        {
            state->pressed = false;
            if (state->dragging)
                state->dragging = false;
            state->pressAllowDrag = false;
            state->dragValueActive = false;
            if (state->cursorHidden)
            {
                ShowCursor();
                state->cursorHidden = false;
            }
        }
    }

    TextInputConfig tcfg = {0};
    tcfg.fontSize = cfg.fontSize;
    tcfg.padding = cfg.padding;
    tcfg.textColor = cfg.textColor;
    tcfg.bgColor = cfg.bgColor;
    tcfg.borderColor = cfg.borderColor;
    tcfg.selectionColor = cfg.selectionColor;
    tcfg.caretColor = cfg.caretColor;
    tcfg.filter = TEXT_INPUT_FILTER_FLOAT;
    tcfg.allowInput = cfg.allowInput && !state->dragging;

    int textFlags = TextInputDraw(box, buffer, bufferSize, &state->text, &tcfg);
    if (textFlags & TEXT_INPUT_SUBMITTED)
        flags |= DRAG_FLOAT_INPUT_SUBMITTED;
    if (textFlags & TEXT_INPUT_ACTIVATED)
        flags |= DRAG_FLOAT_INPUT_ACTIVATED;
    if (textFlags & TEXT_INPUT_DEACTIVATED)
        flags |= DRAG_FLOAT_INPUT_DEACTIVATED;

    if (textFlags & (TEXT_INPUT_SUBMITTED | TEXT_INPUT_DEACTIVATED))
    {
        float parsed = 0.0f;
        if (DragFloatInputParse(buffer, &parsed))
        {
            if (cfg.clamp)
                parsed = Clamp(parsed, cfg.minValue, cfg.maxValue);
            *value = parsed;
            DragFloatInputFormat(buffer, bufferSize, *value);
            flags |= DRAG_FLOAT_INPUT_CHANGED;
        }
        else
        {
            DragFloatInputFormat(buffer, bufferSize, *value);
        }
    }

    return flags;
}
