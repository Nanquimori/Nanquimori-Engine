#ifndef COLOR_PICKER_H
#define COLOR_PICKER_H

#include "raylib.h"
#include "text_input.h"

typedef struct
{
    bool open;
    bool justOpened;
    char hexBuffer[8];
    Vector3 hsv;
    bool dragWheel;
    bool dragValue;
    TextInputState hexInput;
    unsigned int triangles;
} ColorPickerState;

void ColorPickerOpen(ColorPickerState *state, Color color);
void ColorPickerClose(ColorPickerState *state);
bool ColorPickerIsOpen(const ColorPickerState *state);
Color ColorPickerDraw(ColorPickerState *state, Rectangle panel, const char *title);

#endif // COLOR_PICKER_H
