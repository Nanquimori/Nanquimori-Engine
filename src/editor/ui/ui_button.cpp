#include "ui_button.h"
#include "ui_style.h"

static int DefaultInt(int value, int fallback)
{
    return (value > 0) ? value : fallback;
}

static float DefaultFloat(float value, float fallback)
{
    return (value > 0.0f) ? value : fallback;
}

static Color DefaultColor(Color value, Color fallback)
{
    return (value.a == 0) ? fallback : value;
}

UIButtonState UIButtonGetState(Rectangle rect)
{
    UIButtonState state = {0};
    Vector2 mouse = GetMousePosition();
    state.hovered = CheckCollisionPointRec(mouse, rect);
    state.clicked = state.hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    return state;
}

void UIButtonDraw(Rectangle rect, const char *text, const Texture2D *icon, const UIButtonConfig *config, bool hovered)
{
    const char *label = text ? text : "";
    const UIStyle *style = GetUIStyle();

    UIButtonConfig cfg = {0};
    if (config)
        cfg = *config;

    cfg.fontSize = DefaultInt(cfg.fontSize, 12);
    cfg.padding = DefaultInt(cfg.padding, 8);
    cfg.iconSize = DefaultFloat(cfg.iconSize, 16.0f);
    cfg.borderThickness = DefaultFloat(cfg.borderThickness, 2.0f);
    cfg.textColor = DefaultColor(cfg.textColor, style->buttonText);
    cfg.textHoverColor = DefaultColor(cfg.textHoverColor, style->buttonTextHover);
    cfg.bgColor = DefaultColor(cfg.bgColor, style->buttonBg);
    cfg.bgHoverColor = DefaultColor(cfg.bgHoverColor, style->buttonBgHover);
    cfg.borderColor = DefaultColor(cfg.borderColor, style->buttonBorder);
    cfg.borderHoverColor = DefaultColor(cfg.borderHoverColor, style->buttonBorder);
    cfg.iconColor = DefaultColor(cfg.iconColor, style->buttonText);

    Color bg = hovered ? cfg.bgHoverColor : cfg.bgColor;
    Color border = hovered ? cfg.borderHoverColor : cfg.borderColor;
    Color txtColor = hovered ? cfg.textHoverColor : cfg.textColor;

    DrawRectangleRec(rect, bg);
    if (cfg.borderThickness > 0.0f)
        DrawRectangleLinesEx(rect, cfg.borderThickness, border);

    float cursorX = rect.x + (float)cfg.padding;
    float iconSize = cfg.iconSize;
    bool hasIcon = (icon && icon->id > 0);
    if (hasIcon)
    {
        float scale = iconSize / (float)icon->width;
        float iconY = rect.y + rect.height * 0.5f - iconSize * 0.5f;
        DrawTextureEx(*icon, (Vector2){cursorX, iconY}, 0.0f, scale, cfg.iconColor);
        cursorX += iconSize + (float)cfg.padding;
    }

    if (label[0] != '\0')
    {
        float textY = rect.y + rect.height * 0.5f - cfg.fontSize * 0.5f;
        float textX = cursorX;
        if (cfg.centerText && !hasIcon)
        {
            float textW = (float)MeasureText(label, cfg.fontSize);
            textX = rect.x + rect.width * 0.5f - textW * 0.5f;
        }
        DrawText(label, (int)textX, (int)textY, cfg.fontSize, txtColor);
    }
}
