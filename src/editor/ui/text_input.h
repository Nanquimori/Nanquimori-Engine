#ifndef TEXT_INPUT_H
#define TEXT_INPUT_H

#include "raylib.h"

typedef enum
{
    TEXT_INPUT_FILTER_NONE = 0,
    TEXT_INPUT_FILTER_HEX,
    TEXT_INPUT_FILTER_INT,
    TEXT_INPUT_FILTER_FLOAT
} TextInputFilter;

typedef struct
{
    bool active;
    int caret;
    int selStart;
    int selEnd;
    float scrollX;
    bool mouseSelecting;
    int repeatKey;
    float repeatTimer;
    float caretBlinkTimer;
} TextInputState;

typedef struct
{
    int fontSize;
    int padding;
    Color textColor;
    Color bgColor;
    Color borderColor;
    Color selectionColor;
    Color caretColor;
    TextInputFilter filter;
    bool uppercase;
    bool allowHashPrefix;
    bool allowInput;
} TextInputConfig;

enum
{
    TEXT_INPUT_CHANGED = 1 << 0,
    TEXT_INPUT_SUBMITTED = 1 << 1,
    TEXT_INPUT_ACTIVATED = 1 << 2,
    TEXT_INPUT_DEACTIVATED = 1 << 3
};

void TextInputInit(TextInputState *state);
int TextInputDraw(Rectangle box, char *buffer, int bufferSize, TextInputState *state, const TextInputConfig *config);

#endif // TEXT_INPUT_H
