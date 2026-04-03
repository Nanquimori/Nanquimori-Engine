#ifndef DRAG_FLOAT_INPUT_H
#define DRAG_FLOAT_INPUT_H

#include "raylib.h"
#include "editor/ui/text_input.h"

typedef struct
{
    TextInputState text;
    bool pressed;
    bool dragging;
    bool pressAllowDrag;
    bool cursorHidden;
    bool dragValueActive;
    float dragValue;
    float dragStartValue;
    Vector2 pressPos;
    Vector2 anchorPos;
} DragFloatInputState;

typedef struct
{
    int fontSize;
    int padding;
    Color textColor;
    Color bgColor;
    Color borderColor;
    Color selectionColor;
    Color caretColor;
    float dragSpeed;
    float fineDragSpeed;
    float minValue;
    float maxValue;
    bool clamp;
    bool allowInput;
} DragFloatInputConfig;

enum
{
    DRAG_FLOAT_INPUT_CHANGED = 1 << 0,
    DRAG_FLOAT_INPUT_SUBMITTED = 1 << 1,
    DRAG_FLOAT_INPUT_ACTIVATED = 1 << 2,
    DRAG_FLOAT_INPUT_DEACTIVATED = 1 << 3,
    DRAG_FLOAT_INPUT_DRAGGING = 1 << 4
};

void DragFloatInputInit(DragFloatInputState *state);
bool DragFloatInputIsActive(const DragFloatInputState *state);
void DragFloatInputFormat(char *dst, int dstSize, float value);
bool DragFloatInputParse(const char *text, float *outValue);
int DragFloatInputDraw(Rectangle box, char *buffer, int bufferSize, DragFloatInputState *state, float *value, const DragFloatInputConfig *config);

#endif // DRAG_FLOAT_INPUT_H
