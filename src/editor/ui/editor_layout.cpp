#include "editor_layout.h"
#include "ui_style.h"
#include "ui_tooltip.h"
#include <math.h>

enum EditorLayoutSplitter
{
    EDITOR_LAYOUT_SPLITTER_NONE = 0,
    EDITOR_LAYOUT_SPLITTER_LEFT,
    EDITOR_LAYOUT_SPLITTER_RIGHT
};

typedef struct
{
    int leftWidth;
    int rightWidth;
    EditorLayoutSplitter hoveredSplitter;
    EditorLayoutSplitter draggingSplitter;
    float dragOffset;
    bool changedWhileDragging;
    bool persistRequested;
} EditorLayoutState;

static EditorLayoutState gEditorLayout = {
    EDITOR_LEFT_PANEL_DEFAULT_WIDTH,
    EDITOR_RIGHT_PANEL_DEFAULT_WIDTH,
    EDITOR_LAYOUT_SPLITTER_NONE,
    EDITOR_LAYOUT_SPLITTER_NONE,
    0.0f,
    false,
    false};

static int ClampIntLocal(int value, int minValue, int maxValue)
{
    if (maxValue < minValue)
        maxValue = minValue;
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static void ClampEditorLayoutWidths(void)
{
    int screenW = GetScreenWidth();
    int minPanel = EDITOR_PANEL_MIN_WIDTH;
    int minViewport = EDITOR_VIEWPORT_MIN_WIDTH;
    int maxCombinedPanels = screenW - minViewport;
    int minCombinedPanels = minPanel * 2;

    if (maxCombinedPanels < minCombinedPanels)
        maxCombinedPanels = minCombinedPanels;

    int individualMaxWidth = screenW - minPanel - minViewport;
    gEditorLayout.leftWidth = ClampIntLocal(gEditorLayout.leftWidth, minPanel, individualMaxWidth);
    gEditorLayout.rightWidth = ClampIntLocal(gEditorLayout.rightWidth, minPanel, individualMaxWidth);

    int combinedPanels = gEditorLayout.leftWidth + gEditorLayout.rightWidth;
    if (combinedPanels > maxCombinedPanels)
    {
        int overflow = combinedPanels - maxCombinedPanels;
        if (gEditorLayout.draggingSplitter == EDITOR_LAYOUT_SPLITTER_LEFT)
        {
            gEditorLayout.leftWidth -= overflow;
        }
        else if (gEditorLayout.draggingSplitter == EDITOR_LAYOUT_SPLITTER_RIGHT)
        {
            gEditorLayout.rightWidth -= overflow;
        }
        else if (gEditorLayout.leftWidth >= gEditorLayout.rightWidth)
        {
            gEditorLayout.leftWidth -= overflow;
        }
        else
        {
            gEditorLayout.rightWidth -= overflow;
        }
    }

    gEditorLayout.leftWidth = ClampIntLocal(gEditorLayout.leftWidth, minPanel, screenW - minPanel - minViewport);
    gEditorLayout.rightWidth = ClampIntLocal(gEditorLayout.rightWidth, minPanel, screenW - gEditorLayout.leftWidth - minViewport);
    gEditorLayout.leftWidth = ClampIntLocal(gEditorLayout.leftWidth, minPanel, screenW - gEditorLayout.rightWidth - minViewport);
}

static EditorLayoutSplitter GetHoveredSplitter(Vector2 mouse)
{
    if (CheckCollisionPointRec(mouse, GetEditorLeftSplitterBounds()))
        return EDITOR_LAYOUT_SPLITTER_LEFT;
    if (CheckCollisionPointRec(mouse, GetEditorRightSplitterBounds()))
        return EDITOR_LAYOUT_SPLITTER_RIGHT;
    return EDITOR_LAYOUT_SPLITTER_NONE;
}

static void DrawSplitterAffordance(Rectangle bounds, bool active)
{
    const UIStyle *style = GetUIStyle();
    float centerX = floorf(bounds.x + bounds.width * 0.5f);
    float visualWidth = active ? 4.0f : 2.0f;
    float railX = centerX - visualWidth * 0.5f;
    Color railColor = active ? style->accent : Fade(style->panelBorderSoft, 0.9f);
    DrawRectangle((int)railX, 0, (int)visualWidth, GetScreenHeight(), railColor);
}

void InitEditorLayout(void)
{
    gEditorLayout.leftWidth = EDITOR_LEFT_PANEL_DEFAULT_WIDTH;
    gEditorLayout.rightWidth = EDITOR_RIGHT_PANEL_DEFAULT_WIDTH;
    gEditorLayout.hoveredSplitter = EDITOR_LAYOUT_SPLITTER_NONE;
    gEditorLayout.draggingSplitter = EDITOR_LAYOUT_SPLITTER_NONE;
    gEditorLayout.dragOffset = 0.0f;
    gEditorLayout.changedWhileDragging = false;
    gEditorLayout.persistRequested = false;
    ClampEditorLayoutWidths();
}

void UpdateEditorLayout(bool allowInput)
{
    ClampEditorLayoutWidths();

    Vector2 mouse = GetMousePosition();
    if (gEditorLayout.draggingSplitter != EDITOR_LAYOUT_SPLITTER_NONE)
    {
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            int previousLeftWidth = gEditorLayout.leftWidth;
            int previousRightWidth = gEditorLayout.rightWidth;
            if (gEditorLayout.draggingSplitter == EDITOR_LAYOUT_SPLITTER_LEFT)
                gEditorLayout.leftWidth = (int)(mouse.x - gEditorLayout.dragOffset + 0.5f);
            else if (gEditorLayout.draggingSplitter == EDITOR_LAYOUT_SPLITTER_RIGHT)
                gEditorLayout.rightWidth = (int)((float)GetScreenWidth() - mouse.x + gEditorLayout.dragOffset + 0.5f);

            ClampEditorLayoutWidths();
            if (gEditorLayout.leftWidth != previousLeftWidth || gEditorLayout.rightWidth != previousRightWidth)
                gEditorLayout.changedWhileDragging = true;
        }
        else
        {
            if (gEditorLayout.changedWhileDragging)
                gEditorLayout.persistRequested = true;
            gEditorLayout.draggingSplitter = EDITOR_LAYOUT_SPLITTER_NONE;
            gEditorLayout.changedWhileDragging = false;
        }
    }

    gEditorLayout.hoveredSplitter = allowInput ? GetHoveredSplitter(mouse) : EDITOR_LAYOUT_SPLITTER_NONE;

    if (allowInput)
    {
        if (gEditorLayout.hoveredSplitter == EDITOR_LAYOUT_SPLITTER_LEFT)
        {
            SetUITooltip(GetEditorLeftSplitterBounds(),
                         "layout.left_splitter",
                         "Redimensionar Outliner",
                         "Arraste para ajustar a largura do painel esquerdo.");
        }
        else if (gEditorLayout.hoveredSplitter == EDITOR_LAYOUT_SPLITTER_RIGHT)
        {
            SetUITooltip(GetEditorRightSplitterBounds(),
                         "layout.right_splitter",
                         "Redimensionar Properties",
                         "Arraste para ajustar a largura do painel direito.");
        }
    }

    if (allowInput && gEditorLayout.draggingSplitter == EDITOR_LAYOUT_SPLITTER_NONE && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        if (gEditorLayout.hoveredSplitter == EDITOR_LAYOUT_SPLITTER_LEFT)
        {
            gEditorLayout.draggingSplitter = EDITOR_LAYOUT_SPLITTER_LEFT;
            gEditorLayout.dragOffset = mouse.x - (float)gEditorLayout.leftWidth;
            gEditorLayout.changedWhileDragging = false;
        }
        else if (gEditorLayout.hoveredSplitter == EDITOR_LAYOUT_SPLITTER_RIGHT)
        {
            float rightDividerX = (float)(GetScreenWidth() - gEditorLayout.rightWidth);
            gEditorLayout.draggingSplitter = EDITOR_LAYOUT_SPLITTER_RIGHT;
            gEditorLayout.dragOffset = mouse.x - rightDividerX;
            gEditorLayout.changedWhileDragging = false;
        }
    }

}

void DrawEditorLayoutAffordances(void)
{
    bool leftActive = (gEditorLayout.draggingSplitter == EDITOR_LAYOUT_SPLITTER_LEFT) || (gEditorLayout.hoveredSplitter == EDITOR_LAYOUT_SPLITTER_LEFT);
    bool rightActive = (gEditorLayout.draggingSplitter == EDITOR_LAYOUT_SPLITTER_RIGHT) || (gEditorLayout.hoveredSplitter == EDITOR_LAYOUT_SPLITTER_RIGHT);
    DrawSplitterAffordance(GetEditorLeftSplitterBounds(), leftActive);
    DrawSplitterAffordance(GetEditorRightSplitterBounds(), rightActive);
}

int GetEditorLeftPanelWidth(void)
{
    return gEditorLayout.leftWidth;
}

int GetEditorRightPanelWidth(void)
{
    return gEditorLayout.rightWidth;
}

void SetEditorPanelWidths(int leftWidth, int rightWidth)
{
    if (leftWidth > 0)
        gEditorLayout.leftWidth = leftWidth;
    if (rightWidth > 0)
        gEditorLayout.rightWidth = rightWidth;
    ClampEditorLayoutWidths();
}

bool ConsumeEditorLayoutPersistRequested(void)
{
    bool requested = gEditorLayout.persistRequested;
    gEditorLayout.persistRequested = false;
    return requested;
}

bool IsEditorLayoutDragging(void)
{
    return gEditorLayout.draggingSplitter != EDITOR_LAYOUT_SPLITTER_NONE;
}

bool IsEditorLayoutHandleActive(void)
{
    return gEditorLayout.draggingSplitter != EDITOR_LAYOUT_SPLITTER_NONE ||
           gEditorLayout.hoveredSplitter != EDITOR_LAYOUT_SPLITTER_NONE;
}

bool IsMouseOverEditorLayoutHandle(Vector2 mouse)
{
    return gEditorLayout.draggingSplitter != EDITOR_LAYOUT_SPLITTER_NONE ||
           CheckCollisionPointRec(mouse, GetEditorLeftSplitterBounds()) ||
           CheckCollisionPointRec(mouse, GetEditorRightSplitterBounds());
}

Rectangle GetEditorLeftSplitterBounds(void)
{
    return (Rectangle){(float)gEditorLayout.leftWidth - 6.0f, 0.0f, 12.0f, (float)GetScreenHeight()};
}

Rectangle GetEditorRightSplitterBounds(void)
{
    float dividerX = (float)(GetScreenWidth() - gEditorLayout.rightWidth);
    return (Rectangle){dividerX - 6.0f, 0.0f, 12.0f, (float)GetScreenHeight()};
}
