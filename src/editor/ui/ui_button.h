#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include "raylib.h"

typedef struct
{
    int fontSize;
    int padding;
    Color textColor;
    Color textHoverColor;
    Color bgColor;
    Color bgHoverColor;
    Color borderColor;
    Color borderHoverColor;
    Color iconColor;
    float iconSize;
    float borderThickness;
    bool centerText;
} UIButtonConfig;

typedef struct
{
    bool hovered;
    bool clicked;
} UIButtonState;

UIButtonState UIButtonGetState(Rectangle rect);
void UIButtonDraw(Rectangle rect, const char *text, const Texture2D *icon, const UIButtonConfig *config, bool hovered);

#endif // UI_BUTTON_H
