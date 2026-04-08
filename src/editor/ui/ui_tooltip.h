#ifndef UI_TOOLTIP_H
#define UI_TOOLTIP_H

#include "raylib.h"

void BeginUITooltipFrame(void);
void SetUITooltip(Rectangle anchor, const char *id, const char *title, const char *description);
void DrawUITooltip(void);

#endif // UI_TOOLTIP_H
