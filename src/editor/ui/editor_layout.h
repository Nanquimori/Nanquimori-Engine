#ifndef EDITOR_LAYOUT_H
#define EDITOR_LAYOUT_H

#include "raylib.h"

#define EDITOR_LEFT_PANEL_DEFAULT_WIDTH 280
#define EDITOR_RIGHT_PANEL_DEFAULT_WIDTH 280
#define EDITOR_PANEL_MIN_WIDTH 200
#define EDITOR_VIEWPORT_MIN_WIDTH 260
#define EDITOR_TOP_BAR_HEIGHT 24

void InitEditorLayout(void);
void UpdateEditorLayout(bool allowInput);
void DrawEditorLayoutAffordances(void);

int GetEditorLeftPanelWidth(void);
int GetEditorRightPanelWidth(void);
void SetEditorPanelWidths(int leftWidth, int rightWidth);
bool ConsumeEditorLayoutPersistRequested(void);
bool IsEditorLayoutDragging(void);
bool IsEditorLayoutHandleActive(void);
bool IsMouseOverEditorLayoutHandle(Vector2 mouse);

Rectangle GetEditorLeftSplitterBounds(void);
Rectangle GetEditorRightSplitterBounds(void);

#endif // EDITOR_LAYOUT_H
