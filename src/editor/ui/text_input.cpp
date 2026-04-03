#include "text_input.h"
#include "ui_style.h"
#include <math.h>
#include <string.h>

static int TextWidthN(const char *text, int count, int fontSize)
{
    if (!text || count <= 0)
        return 0;
    char tmp[128];
    if (count > (int)sizeof(tmp) - 1)
        count = (int)sizeof(tmp) - 1;
    memcpy(tmp, text, count);
    tmp[count] = '\0';
    return MeasureText(tmp, fontSize);
}

static int ClampInt(int value, int minValue, int maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static Color LerpColorLocal(Color a, Color b, float t)
{
    Color out = {0};
    out.r = (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t + 0.5f);
    out.g = (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t + 0.5f);
    out.b = (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t + 0.5f);
    out.a = (unsigned char)((float)a.a + ((float)b.a - (float)a.a) * t + 0.5f);
    return out;
}

static int TextIndexFromX(const char *text, int fontSize, float x)
{
    int len = (int)strlen(text);
    for (int i = 0; i <= len; i++)
    {
        int w = TextWidthN(text, i, fontSize);
        if (x < (float)w)
            return i;
    }
    return len;
}

static void ResetCaretBlink(TextInputState *state)
{
    if (!state)
        return;
    state->caretBlinkTimer = 0.0f;
}

static void EraseRange(char *text, int start, int end)
{
    int len = (int)strlen(text);
    if (start < 0) start = 0;
    if (end > len) end = len;
    if (end <= start) return;
    memmove(text + start, text + end, len - end + 1);
}

static void InsertChar(char *text, int pos, char c, int maxLen)
{
    int len = (int)strlen(text);
    if (len >= maxLen)
        return;
    if (pos < 0) pos = 0;
    if (pos > len) pos = len;
    memmove(text + pos + 1, text + pos, len - pos + 1);
    text[pos] = c;
}

static bool IsAllowedChar(TextInputFilter filter, char c, const char *text, int caret, bool allowHashPrefix)
{
    if (filter == TEXT_INPUT_FILTER_NONE)
        return (c >= 32 && c <= 126);

    if (filter == TEXT_INPUT_FILTER_HEX)
    {
        if (c == '#')
            return allowHashPrefix && caret == 0 && (text[0] != '#');
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    }

    if (filter == TEXT_INPUT_FILTER_INT)
    {
        if (c == '-')
            return caret == 0 && (text[0] != '-');
        return (c >= '0' && c <= '9');
    }

    if (filter == TEXT_INPUT_FILTER_FLOAT)
    {
        if (c == '-')
            return caret == 0 && (text[0] != '-');
        if (c == '.')
            return strchr(text, '.') == nullptr;
        return (c >= '0' && c <= '9');
    }

    return false;
}

void TextInputInit(TextInputState *state)
{
    if (!state)
        return;
    state->active = false;
    state->caret = 0;
    state->selStart = 0;
    state->selEnd = 0;
    state->scrollX = 0.0f;
    state->mouseSelecting = false;
    state->repeatKey = 0;
    state->repeatTimer = 0.0f;
    state->caretBlinkTimer = 0.0f;
}

int TextInputDraw(Rectangle box, char *buffer, int bufferSize, TextInputState *state, const TextInputConfig *config)
{
    if (!buffer || bufferSize <= 0 || !state)
        return 0;

    TextInputConfig cfg = {0};
    if (config)
        cfg = *config;
    if (cfg.fontSize <= 0)
        cfg.fontSize = 12;
    if (cfg.padding < 0)
        cfg.padding = 6;
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
    if (!config)
        cfg.allowInput = true;

    int flags = 0;
    Vector2 mouse = GetMousePosition();
    bool over = CheckCollisionPointRec(mouse, box);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        if (over && cfg.allowInput)
        {
            if (!state->active)
                flags |= TEXT_INPUT_ACTIVATED;
            state->active = true;
            float localX = mouse.x - (box.x + (float)cfg.padding) + state->scrollX;
            int idx = TextIndexFromX(buffer, cfg.fontSize, localX);
            state->caret = idx;
            state->selStart = idx;
            state->selEnd = idx;
            state->mouseSelecting = true;
            ResetCaretBlink(state);
        }
        else
        {
            if (state->active)
                flags |= TEXT_INPUT_DEACTIVATED;
            state->active = false;
            state->mouseSelecting = false;
        }
    }

    if (state->mouseSelecting)
    {
        float localX = mouse.x - (box.x + (float)cfg.padding) + state->scrollX;
        int idx = TextIndexFromX(buffer, cfg.fontSize, localX);
        state->selEnd = idx;
        state->caret = idx;
        ResetCaretBlink(state);
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
            state->mouseSelecting = false;
    }

    DrawRectangleRec(box, cfg.bgColor);
    Color borderColor = cfg.borderColor;
    if (state->active)
        borderColor = style->accent;
    else if (over && cfg.allowInput)
        borderColor = LerpColorLocal(cfg.borderColor, style->textPrimary, 0.22f);
    DrawRectangleLinesEx(box, 1, borderColor);
    if (!state->active && over && cfg.allowInput)
    {
        Color hoverGlow = LerpColorLocal(cfg.bgColor, style->textPrimary, 0.08f);
        Rectangle hoverFill = {box.x + 1.0f, box.y + 1.0f, box.width - 2.0f, box.height - 2.0f};
        DrawRectangleRec(hoverFill, hoverGlow);
    }

    int len = (int)strlen(buffer);
    state->caret = ClampInt(state->caret, 0, len);
    state->selStart = ClampInt(state->selStart, 0, len);
    state->selEnd = ClampInt(state->selEnd, 0, len);

    int textX = (int)box.x + cfg.padding;
    int textY = (int)(box.y + box.height * 0.5f - (float)cfg.fontSize * 0.5f);
    int innerW = (int)box.width - cfg.padding * 2;
    if (innerW < 1)
        innerW = 1;

    int caretPixel = TextWidthN(buffer, state->caret, cfg.fontSize);
    if (!state->active)
    {
        if (state->scrollX < 0.0f)
            state->scrollX = 0.0f;
    }
    else
    {
        if ((float)caretPixel - state->scrollX > (float)(innerW - 2))
            state->scrollX = (float)caretPixel - (float)(innerW - 2);
        if ((float)caretPixel - state->scrollX < 0.0f)
            state->scrollX = (float)caretPixel;
        if (state->scrollX < 0.0f)
            state->scrollX = 0.0f;
    }

    Rectangle clipRect = {(float)(textX), box.y + 1.0f, (float)innerW, box.height - 2.0f};
    BeginScissorMode((int)clipRect.x, (int)clipRect.y, (int)clipRect.width, (int)clipRect.height);

    if (state->active)
    {
        int selStart = state->selStart;
        int selEnd = state->selEnd;
        if (selStart > selEnd)
        {
            int tmp = selStart;
            selStart = selEnd;
            selEnd = tmp;
        }
        if (selStart != selEnd)
        {
            int selX = textX + TextWidthN(buffer, selStart, cfg.fontSize) - (int)state->scrollX;
            int selW = TextWidthN(buffer, selEnd, cfg.fontSize) - TextWidthN(buffer, selStart, cfg.fontSize);
            if (selW < 2) selW = 2;
            Rectangle sel = {(float)(selX - 2), (float)(textY - 2), (float)(selW + 4), (float)(cfg.fontSize + 4)};
            DrawRectangleRec(sel, cfg.selectionColor);
        }
    }

    DrawText(buffer, textX - (int)state->scrollX, textY, cfg.fontSize, cfg.textColor);

    if (state->active)
    {
        int cx = textX + TextWidthN(buffer, state->caret, cfg.fontSize) - (int)state->scrollX;
        bool showCaret = state->mouseSelecting || fmodf(state->caretBlinkTimer, 1.0f) < 0.55f;
        if (showCaret)
        {
            Rectangle caret = {(float)cx, (float)(textY - 1), 2.0f, (float)(cfg.fontSize + 2)};
            DrawRectangleRec(caret, cfg.caretColor);
        }
    }

    EndScissorMode();

    if (state->active && cfg.allowInput)
    {
        int selStart = state->selStart;
        int selEnd = state->selEnd;
        if (selStart > selEnd)
        {
            int tmp = selStart;
            selStart = selEnd;
            selEnd = tmp;
        }

        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (ctrl && IsKeyPressed(KEY_A))
        {
            state->selStart = 0;
            state->selEnd = len;
            state->caret = len;
            ResetCaretBlink(state);
        }
        if (ctrl && IsKeyPressed(KEY_C))
        {
            if (selStart != selEnd)
            {
                char tmp[256];
                int count = selEnd - selStart;
                if (count > (int)sizeof(tmp) - 1) count = (int)sizeof(tmp) - 1;
                memcpy(tmp, buffer + selStart, count);
                tmp[count] = '\0';
                SetClipboardText(tmp);
            }
            else if (len > 0)
            {
                SetClipboardText(buffer);
            }
        }
        if (ctrl && IsKeyPressed(KEY_V))
        {
            const char *clip = GetClipboardText();
            if (clip && clip[0] != '\0')
            {
                if (selStart != selEnd)
                {
                    EraseRange(buffer, selStart, selEnd);
                    state->caret = selStart;
                }
                int maxLen = bufferSize - 1;
                for (int i = 0; clip[i] != '\0'; i++)
                {
                    char c = clip[i];
                    if (!IsAllowedChar(cfg.filter, c, buffer, state->caret, cfg.allowHashPrefix))
                        continue;
                    if (cfg.uppercase && c >= 'a' && c <= 'z')
                        c = (char)(c - 32);
                    if ((int)strlen(buffer) >= maxLen)
                        break;
                    InsertChar(buffer, state->caret, c, maxLen);
                    state->caret++;
                }
                state->selStart = state->caret;
                state->selEnd = state->caret;
                flags |= TEXT_INPUT_CHANGED;
                ResetCaretBlink(state);
            }
        }

        if (IsKeyPressed(KEY_LEFT))
        {
            if (selStart != selEnd)
                state->caret = selStart;
            else if (state->caret > 0)
                state->caret--;
            state->selStart = state->caret;
            state->selEnd = state->caret;
            ResetCaretBlink(state);
        }
        if (IsKeyPressed(KEY_RIGHT))
        {
            if (selStart != selEnd)
                state->caret = selEnd;
            else if (state->caret < (int)strlen(buffer))
                state->caret++;
            state->selStart = state->caret;
            state->selEnd = state->caret;
            ResetCaretBlink(state);
        }
        if (IsKeyPressed(KEY_HOME))
        {
            state->caret = 0;
            state->selStart = 0;
            state->selEnd = 0;
            ResetCaretBlink(state);
        }
        if (IsKeyPressed(KEY_END))
        {
            int end = (int)strlen(buffer);
            state->caret = end;
            state->selStart = end;
            state->selEnd = end;
            ResetCaretBlink(state);
        }

        auto applyBackspace = [&](void)
        {
            if (selStart != selEnd)
            {
                EraseRange(buffer, selStart, selEnd);
                state->caret = selStart;
                flags |= TEXT_INPUT_CHANGED;
            }
            else if (state->caret > 0)
            {
                EraseRange(buffer, state->caret - 1, state->caret);
                state->caret--;
                flags |= TEXT_INPUT_CHANGED;
            }
            state->selStart = state->caret;
            state->selEnd = state->caret;
            ResetCaretBlink(state);
        };

        auto applyDelete = [&](void)
        {
            if (selStart != selEnd)
            {
                EraseRange(buffer, selStart, selEnd);
                state->caret = selStart;
                flags |= TEXT_INPUT_CHANGED;
            }
            else if (state->caret < (int)strlen(buffer))
            {
                EraseRange(buffer, state->caret, state->caret + 1);
                flags |= TEXT_INPUT_CHANGED;
            }
            state->selStart = state->caret;
            state->selEnd = state->caret;
            ResetCaretBlink(state);
        };

        if (IsKeyPressed(KEY_BACKSPACE))
        {
            applyBackspace();
            state->repeatKey = KEY_BACKSPACE;
            state->repeatTimer = 0.45f;
        }
        if (IsKeyPressed(KEY_DELETE))
        {
            applyDelete();
            state->repeatKey = KEY_DELETE;
            state->repeatTimer = 0.45f;
        }

        if (state->repeatKey != 0)
        {
            bool hold = (state->repeatKey == KEY_BACKSPACE) ? IsKeyDown(KEY_BACKSPACE) : IsKeyDown(KEY_DELETE);
            if (!hold)
            {
                state->repeatKey = 0;
                state->repeatTimer = 0.0f;
            }
            else
            {
                state->repeatTimer -= GetFrameTime();
                if (state->repeatTimer <= 0.0f)
                {
                    if (state->repeatKey == KEY_BACKSPACE)
                        applyBackspace();
                    else
                        applyDelete();
                    state->repeatTimer = 0.04f;
                }
            }
        }

        int ch = GetCharPressed();
        while (ch > 0)
        {
            char c = (char)ch;
            if (IsAllowedChar(cfg.filter, c, buffer, state->caret, cfg.allowHashPrefix))
            {
                if (cfg.uppercase && c >= 'a' && c <= 'z')
                    c = (char)(c - 32);
                if (selStart != selEnd)
                {
                    EraseRange(buffer, selStart, selEnd);
                    state->caret = selStart;
                }
                int maxLen = bufferSize - 1;
                if ((int)strlen(buffer) < maxLen)
                {
                    InsertChar(buffer, state->caret, c, maxLen);
                    state->caret++;
                    flags |= TEXT_INPUT_CHANGED;
                }
                state->selStart = state->caret;
                state->selEnd = state->caret;
                ResetCaretBlink(state);
            }
            ch = GetCharPressed();
        }

        if (IsKeyPressed(KEY_ENTER))
            flags |= TEXT_INPUT_SUBMITTED;
    }

    if (state->active)
        state->caretBlinkTimer += GetFrameTime();
    else
        state->caretBlinkTimer = 0.0f;

    return flags;
}
