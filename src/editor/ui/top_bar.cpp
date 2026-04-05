#include "top_bar.h"
#include "export_dialog.h"
#include "file_explorer.h"
#include "help_panel.h"
#include "splash_screen.h"
#include "app/application.h"
#include "editor/viewport/camera_controller.h"
#include "scene/outliner.h"
#include "scene/scene_camera.h"
#include "properties_panel.h"
#include "assets/model_manager.h"
#include "ui_button.h"
#include "ui_style.h"
#include <cstdio>
#include <cstring>

// -------------------------------------------------------
// STATIC TOP BAR STATE
// -------------------------------------------------------
static Texture2D iconFolder = {0};
static Texture2D iconHelp = {0};
static Texture2D iconN = {0};
static bool playModeActive = false;
static bool playPaused = false;
static bool playStopRequested = false;
static bool playRestartRequested = false;
static bool fileHover = false;
static bool helpHover = false;
static bool buildHover = false;
static bool playHover = false;
static bool stopHover = false;
static bool restartHover = false;
static bool navigateHover = false;
static bool viewportNavigateMode = false;
static bool addMenuOpen = false;
static bool addHover = false;
static int addMainHoverItem = -1;
static int addShapeHoverItem = -1;
static bool addShapesSubmenuOpen = false;
static const PrimitiveModelType addPrimitiveTypes[] = {
    PRIMITIVE_MODEL_CUBE,
    PRIMITIVE_MODEL_SPHERE,
    PRIMITIVE_MODEL_CYLINDER,
    PRIMITIVE_MODEL_PLANE};
static const char *addPrimitiveLabels[] = {
    "Cube",
    "Sphere",
    "Cylinder",
    "Plane"};
static const char *topBarBrandText = "Nanquimori Engine";

static Color TopBarMenuHoverColor(void)
{
    return (Color){58, 26, 24, 255};
}

static Color TopBarMenuBorderColor(void)
{
    return (Color){104, 56, 52, 255};
}

static bool TryResolvePathFromBaseChain(const char *baseDir, const char *relativePath, char *out, size_t outSize)
{
    if (!baseDir || baseDir[0] == '\0' || !relativePath || relativePath[0] == '\0' || !out || outSize == 0)
        return false;

    char current[512] = {0};
    strncpy(current, baseDir, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';

    for (int i = 0; i < 6; i++)
    {
        snprintf(out, outSize, "%s/%s", current, relativePath);
        out[outSize - 1] = '\0';
        if (FileExists(out))
            return true;

        char next[512] = {0};
        snprintf(next, sizeof(next), "%s/..", current);
        next[sizeof(next) - 1] = '\0';
        if (strcmp(next, current) == 0)
            break;
        strncpy(current, next, sizeof(current) - 1);
        current[sizeof(current) - 1] = '\0';
    }

    return false;
}

static bool ResolveAssetPath(const char *relativePath, char *out, size_t outSize)
{
    if (!relativePath || relativePath[0] == '\0' || !out || outSize == 0)
        return false;
    out[0] = '\0';

    if (FileExists(relativePath))
    {
        strncpy(out, relativePath, outSize - 1);
        out[outSize - 1] = '\0';
        return true;
    }

    const char *cwd = GetWorkingDirectory();
    const char *appDir = GetApplicationDirectory();
    if (TryResolvePathFromBaseChain(cwd, relativePath, out, outSize))
        return true;
    if (TryResolvePathFromBaseChain(appDir, relativePath, out, outSize))
        return true;

    return false;
}

static void DrawTopBarIcon(Texture2D icon, Vector2 pos, float size, Color tint)
{
    if (icon.id <= 0 || icon.width <= 0 || icon.height <= 0)
        return;
    DrawTextureEx(icon, pos, 0.0f, size / (float)icon.width, tint);
}

static void UnloadTopBarIcon(Texture2D *icon)
{
    if (!icon || icon->id <= 0)
        return;
    UnloadTexture(*icon);
    *icon = (Texture2D){0};
}

static void BuildUniqueObjectName(const char *baseName, char *outName, size_t outSize)
{
    if (!outName || outSize == 0)
        return;
    outName[0] = '\0';

    const char *base = (baseName && baseName[0] != '\0') ? baseName : "Object";
    if (!ObjetoExisteNoOutliner(base))
    {
        strncpy(outName, base, outSize - 1);
        outName[outSize - 1] = '\0';
        return;
    }

    for (int i = 1; i < 1000; i++)
    {
        snprintf(outName, outSize, "%s %d", base, i);
        outName[outSize - 1] = '\0';
        if (!ObjetoExisteNoOutliner(outName))
            return;
    }

    strncpy(outName, base, outSize - 1);
    outName[outSize - 1] = '\0';
}

static void AddEmptyObject(void)
{
    char objectName[32] = {0};
    BuildUniqueObjectName("Empty", objectName, sizeof(objectName));
    int id = RegistrarObjeto(objectName, (Vector3){0, 0, 0}, -1);
    if (id <= 0)
        return;

    int idx = BuscarIndicePorId(id);
    if (idx != -1)
        objetos[idx].caminhoModelo[0] = '\0';

    SelecionarObjetoPorId(id);
    SetSelectedModelByObjetoId(id);
}

// -------------------------------------------------------
// INIT / UNLOAD
// -------------------------------------------------------
void InitTopBar()
{
    char path[512] = {0};

    if (ResolveAssetPath("icons/window.png", path, sizeof(path)))
        iconFolder = LoadTexture(path);
    if (ResolveAssetPath("icons/help.png", path, sizeof(path)))
        iconHelp = LoadTexture(path);

    if (ResolveAssetPath("icons/N.ico", path, sizeof(path)))
        iconN = LoadTexture(path);
    if (iconN.id <= 0 && ResolveAssetPath("icons/n.png", path, sizeof(path)))
        iconN = LoadTexture(path);
}

void UnloadTopBar()
{
    UnloadTopBarIcon(&iconFolder);
    UnloadTopBarIcon(&iconHelp);
    UnloadTopBarIcon(&iconN);
}

// -------------------------------------------------------
// UPDATE
// -------------------------------------------------------
void UpdateTopBar()
{
    float W = (float)GetScreenWidth();
    float left = (float)PAINEL_LARGURA;
    float right = (float)PROPERTIES_PAINEL_LARGURA;
    float availableW = W - left - right;
    float barY = 4.0f;
    float iconSize = 16.0f;
    float brandW = iconSize + 6.0f + (float)MeasureText(topBarBrandText, 12);
    float brandX = left + (availableW - brandW) * 0.5f;
    Rectangle areaBrand = {brandX, barY, brandW, 16.0f};
    UIButtonState brandState = UIButtonGetState(areaBrand);
    if (brandState.clicked)
        ShowSplashScreen();

    float barX = 8.0f + left;

    // File
    Rectangle areaFile = {barX - 4.0f, 2.0f, 64.0f, 20.0f};
    UIButtonState fileState = UIButtonGetState(areaFile);
    fileHover = fileState.hovered;
    if (fileState.clicked)
    {
        ToggleFileMenu();
        addMenuOpen = false;
        addShapesSubmenuOpen = false;
    }

    // Add
    barX += 70.0f;
    Rectangle areaAdd = {barX - 6.0f, 2.0f, 56.0f, 20.0f};
    UIButtonState addState = UIButtonGetState(areaAdd);
    addHover = addState.hovered;
    bool addToggledThisFrame = false;
    if (addState.clicked)
    {
        addMenuOpen = !addMenuOpen;
        addToggledThisFrame = true;
    }

    const float addItemH = 24.0f;
    Rectangle addMenuRect = {areaAdd.x, 24.0f, 220.0f, addItemH * 3.0f};
    Rectangle itemEmpty = {addMenuRect.x, addMenuRect.y, addMenuRect.width, addItemH};
    Rectangle itemCamera = {addMenuRect.x, addMenuRect.y + addItemH, addMenuRect.width, addItemH};
    Rectangle itemGeometry = {addMenuRect.x, addMenuRect.y + addItemH * 2.0f, addMenuRect.width, addItemH};
    const int addShapeCount = (int)(sizeof(addPrimitiveTypes) / sizeof(addPrimitiveTypes[0]));
    Rectangle addShapesRect = {addMenuRect.x + addMenuRect.width + 2.0f, itemGeometry.y, 180.0f, addShapeCount * addItemH};

    addMainHoverItem = -1;
    addShapeHoverItem = -1;
    if (addMenuOpen)
    {
        Vector2 mouse = GetMousePosition();
        bool clicked = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
        if (CheckCollisionPointRec(mouse, itemEmpty))
            addMainHoverItem = 0;
        else if (CheckCollisionPointRec(mouse, itemCamera))
            addMainHoverItem = 1;
        else if (CheckCollisionPointRec(mouse, itemGeometry))
            addMainHoverItem = 2;

        bool mouseInSubmenu = CheckCollisionPointRec(mouse, addShapesRect);
        addShapesSubmenuOpen = (addMainHoverItem == 2) || mouseInSubmenu;

        if (addShapesSubmenuOpen)
        {
            for (int i = 0; i < addShapeCount; i++)
            {
                Rectangle item = {addShapesRect.x, addShapesRect.y + i * addItemH, addShapesRect.width, addItemH};
                if (CheckCollisionPointRec(mouse, item))
                {
                    addShapeHoverItem = i;
                    break;
                }
            }
        }

        if (clicked && !addToggledThisFrame)
        {
            if (addMainHoverItem == 0)
            {
                AddEmptyObject();
                addMenuOpen = false;
                addShapesSubmenuOpen = false;
            }
            else if (addMainHoverItem == 1)
            {
                Camera viewCamera = GetEditorViewportCamera();
                AddCameraObjectFromView(&viewCamera);
                addMenuOpen = false;
                addShapesSubmenuOpen = false;
            }
            else if (addShapesSubmenuOpen && addShapeHoverItem >= 0)
            {
                AddPrimitiveObject(addPrimitiveTypes[addShapeHoverItem]);
                addMenuOpen = false;
                addShapesSubmenuOpen = false;
            }
            else if (!CheckCollisionPointRec(mouse, addMenuRect) && !CheckCollisionPointRec(mouse, addShapesRect))
            {
                addMenuOpen = false;
                addShapesSubmenuOpen = false;
            }
        }
    }

    // Build
    barX += 70.0f;
    Rectangle areaBuild = {barX - 6.0f, 2.0f, 58.0f, 20.0f};
    UIButtonState buildState = UIButtonGetState(areaBuild);
    buildHover = buildState.hovered;
    if (buildState.clicked)
    {
        OpenExportDialog();
        addMenuOpen = false;
        addShapesSubmenuOpen = false;
    }

    // Help
    barX += 62.0f;
    Rectangle areaHelp = {barX - 4.0f, 2.0f, 68.0f, 20.0f};
    UIButtonState helpState = UIButtonGetState(areaHelp);
    helpHover = helpState.hovered;
    if (helpState.clicked)
    {
        SetHelpPanelShow(!HelpPanelShouldShow());
        addMenuOpen = false;
        addShapesSubmenuOpen = false;
    }

    if (!playModeActive && viewportNavigateMode)
        viewportNavigateMode = false;

    // Navegacao / Play / Stop / Restart (depois do Help)
    float btnY = barY;
    float btnX = barX + 80.0f;
    Rectangle areaPlay = {btnX, btnY, 60.0f, 16.0f};
    if (playModeActive)
        areaPlay.x = btnX + 94.0f;

    navigateHover = false;
    if (playModeActive)
    {
        Rectangle areaNavigate = {btnX, btnY, 84.0f, 16.0f};
        UIButtonState navigateState = UIButtonGetState(areaNavigate);
        navigateHover = navigateState.hovered;
        if (navigateState.clicked)
            viewportNavigateMode = !viewportNavigateMode;
    }

    UIButtonState playState = UIButtonGetState(areaPlay);
    playHover = playState.hovered;
    if (playState.clicked)
    {
        if (!playModeActive)
        {
            viewportNavigateMode = false;
            playModeActive = true;
            playPaused = false;
        }
        else if (playPaused)
        {
            playPaused = false;
        }
        else
        {
            playPaused = true;
        }
    }
    else if (playModeActive)
    {
        Rectangle areaStop = {btnX + 164.0f, btnY, 60.0f, 16.0f};
        Rectangle areaRestart = {btnX + 234.0f, btnY, 70.0f, 16.0f};
        UIButtonState stopState = UIButtonGetState(areaStop);
        UIButtonState restartState = UIButtonGetState(areaRestart);
        stopHover = stopState.hovered;
        restartHover = restartState.hovered;
        if (stopState.clicked)
        {
            playStopRequested = true;
        }
        else if (restartState.clicked)
        {
            playRestartRequested = true;
        }
    }
    else
    {
        stopHover = false;
        restartHover = false;
    }

}

// -------------------------------------------------------
// DRAW
// -------------------------------------------------------
void DrawTopBar()
{
    float W = (float)GetScreenWidth();
    float left = (float)PAINEL_LARGURA;
    float right = (float)PROPERTIES_PAINEL_LARGURA;
    float availableW = W - left - right;

    const UIStyle *style = GetUIStyle();
    DrawRectangle((int)left, 0, (int)availableW, 24, style->topBarBg);
    DrawLine((int)left, 24, (int)(left + availableW), 24, style->topBarBorder);

    float barY = 4.0f;
    float iconSize = 16.0f;
    float brandW = iconSize + 6.0f + (float)MeasureText(topBarBrandText, 12);
    float brandX = left + (availableW - brandW) * 0.5f;
    DrawTopBarIcon(iconN, (Vector2){brandX, barY}, iconSize, style->textPrimary);
    DrawText(topBarBrandText, (int)(brandX + iconSize + 6.0f), 5, 12, style->textSecondary);

    float barX = 8.0f + left;

    // File
    Rectangle areaFile = {barX - 4.0f, 2.0f, 64.0f, 20.0f};
    bool fileActive = IsFileMenuOpen() || fileHover;
    Color fileText = fileActive ? style->accent : style->textPrimary;
    DrawTopBarIcon(iconFolder, (Vector2){barX, barY}, iconSize, style->textPrimary);
    DrawText("File", barX + 20.0f, 5, 12, fileText);

    // Add
    barX += 70.0f;
    float addMenuX = barX;
    Rectangle areaAdd = {barX - 6.0f, 2.0f, 56.0f, 20.0f};
    bool addActive = addMenuOpen || addHover;
    Color addColor = addActive ? style->accent : style->textPrimary;
    DrawText("Add", (int)barX, 5, 12, addColor);

    // Build
    barX += 70.0f;
    Rectangle areaBuild = {barX - 6.0f, 2.0f, 58.0f, 20.0f};
    bool buildActive = buildHover || IsExportDialogOpen();
    Color buildText = buildActive ? style->accent : style->textPrimary;
    DrawText("Build", (int)barX, 5, 12, buildText);

    // Help
    barX += 62.0f;
    Rectangle areaHelp = {barX - 4.0f, 2.0f, 68.0f, 20.0f};
    bool helpActive = helpHover || HelpPanelShouldShow();
    Color helpText = helpActive ? style->accent : style->textPrimary;
    DrawTopBarIcon(iconHelp, (Vector2){barX, barY}, iconSize, style->textPrimary);
    DrawText("Help", barX + 20.0f, 5, 12, helpText);

    if (addMenuOpen)
    {
        const float addItemH = 24.0f;
        Rectangle addMenuRect = {addMenuX, 24.0f, 220.0f, addItemH * 3.0f};
        Rectangle itemEmpty = {addMenuRect.x, addMenuRect.y, addMenuRect.width, addItemH};
        Rectangle itemCamera = {addMenuRect.x, addMenuRect.y + addItemH, addMenuRect.width, addItemH};
        Rectangle itemGeometry = {addMenuRect.x, addMenuRect.y + addItemH * 2.0f, addMenuRect.width, addItemH};
        const int addShapeCount = (int)(sizeof(addPrimitiveLabels) / sizeof(addPrimitiveLabels[0]));
        Rectangle addShapesRect = {addMenuRect.x + addMenuRect.width + 2.0f, itemGeometry.y, 180.0f, addShapeCount * addItemH};

        DrawRectangleRec(addMenuRect, style->buttonBg);
        DrawRectangleLinesEx(addMenuRect, 1.0f, style->panelBorder);
        DrawRectangleRec(itemEmpty, (addMainHoverItem == 0) ? TopBarMenuHoverColor() : style->buttonBg);
        DrawRectangleRec(itemCamera, (addMainHoverItem == 1) ? TopBarMenuHoverColor() : style->buttonBg);
        DrawRectangleRec(itemGeometry, (addMainHoverItem == 2 || addShapesSubmenuOpen) ? TopBarMenuHoverColor() : style->buttonBg);
        if (addMainHoverItem == 0)
            DrawRectangleLinesEx(itemEmpty, 1.0f, TopBarMenuBorderColor());
        if (addMainHoverItem == 1)
            DrawRectangleLinesEx(itemCamera, 1.0f, TopBarMenuBorderColor());
        if (addMainHoverItem == 2 || addShapesSubmenuOpen)
            DrawRectangleLinesEx(itemGeometry, 1.0f, TopBarMenuBorderColor());
        DrawText("Empty Object", (int)(itemEmpty.x + 10.0f), (int)(itemEmpty.y + 6.0f), 12,
                 (addMainHoverItem == 0) ? style->buttonTextHover : style->buttonText);
        DrawText("Camera", (int)(itemCamera.x + 10.0f), (int)(itemCamera.y + 6.0f), 12,
                 (addMainHoverItem == 1) ? style->buttonTextHover : style->buttonText);
        DrawText("Geometric Shapes", (int)(itemGeometry.x + 10.0f), (int)(itemGeometry.y + 6.0f), 12,
                 (addMainHoverItem == 2 || addShapesSubmenuOpen) ? style->buttonTextHover : style->buttonText);
        DrawText(">", (int)(itemGeometry.x + itemGeometry.width - 18.0f), (int)(itemGeometry.y + 6.0f), 12,
                 (addMainHoverItem == 2 || addShapesSubmenuOpen) ? style->buttonTextHover : style->buttonText);

        if (addShapesSubmenuOpen)
        {
            DrawRectangleRec(addShapesRect, style->buttonBg);
            DrawRectangleLinesEx(addShapesRect, 1.0f, style->panelBorder);
            for (int i = 0; i < addShapeCount; i++)
            {
                Rectangle item = {addShapesRect.x, addShapesRect.y + i * addItemH, addShapesRect.width, addItemH};
                bool hover = (i == addShapeHoverItem);
                DrawRectangleRec(item, hover ? TopBarMenuHoverColor() : style->buttonBg);
                if (hover)
                    DrawRectangleLinesEx(item, 1.0f, TopBarMenuBorderColor());
                DrawText(addPrimitiveLabels[i], (int)(item.x + 10.0f), (int)(item.y + 6.0f), 12,
                         hover ? style->buttonTextHover : style->buttonText);
            }
        }
    }

    // Navegacao / Play / Stop / Restart after Help
    barX += 80.0f;
    UIButtonConfig baseCfg = {0};
    const UIStyle *uiStyle = GetUIStyle();
    baseCfg.fontSize = 12;
    baseCfg.padding = 6;
    baseCfg.centerText = true;
    baseCfg.textColor = WHITE;
    baseCfg.textHoverColor = WHITE;
    baseCfg.bgColor = uiStyle->buttonBg;
    baseCfg.bgHoverColor = uiStyle->itemHover;
    baseCfg.borderColor = uiStyle->buttonBorder;
    baseCfg.borderHoverColor = uiStyle->accent;
    baseCfg.borderThickness = 1.0f;

    if (playModeActive)
    {
        Rectangle areaNavigate = {barX, 4.0f, 84.0f, 16.0f};
        UIButtonConfig navigateCfg = baseCfg;
        Color navigateColor = viewportNavigateMode ? (Color){88, 192, 224, 255} : style->textSecondary;
        navigateCfg.textColor = navigateColor;
        navigateCfg.textHoverColor = navigateColor;
        navigateCfg.borderColor = navigateColor;
        navigateCfg.borderHoverColor = navigateColor;
        UIButtonDraw(areaNavigate, "Navegar", nullptr, &navigateCfg, navigateHover);
        barX += 94.0f;
    }

    const char *playLabel = playModeActive ? (playPaused ? "Resume" : "Pause") : "Play";
    Color playColor = playPaused ? (Color){80, 160, 220, 255} : (playModeActive ? (Color){220, 110, 80, 255} : (Color){80, 200, 120, 255});

    Rectangle areaPlay = {barX, 4.0f, 60.0f, 16.0f};
    UIButtonConfig playCfg = baseCfg;
    playCfg.textColor = playColor;
    playCfg.textHoverColor = playColor;
    playCfg.borderColor = playColor;
    playCfg.borderHoverColor = playColor;
    UIButtonDraw(areaPlay, playLabel, nullptr, &playCfg, playHover);

    if (playModeActive)
    {
        Color stopColor = (Color){210, 90, 80, 255};
        Color restartColor = (Color){90, 170, 220, 255};

        Rectangle areaStop = {barX + 70.0f, 4.0f, 60.0f, 16.0f};
        Rectangle areaRestart = {barX + 140.0f, 4.0f, 70.0f, 16.0f};

        UIButtonConfig stopCfg = baseCfg;
        stopCfg.textColor = stopColor;
        stopCfg.textHoverColor = stopColor;
        stopCfg.borderColor = stopColor;
        stopCfg.borderHoverColor = stopColor;
        UIButtonDraw(areaStop, "Stop", nullptr, &stopCfg, stopHover);

        UIButtonConfig restartCfg = baseCfg;
        restartCfg.textColor = restartColor;
        restartCfg.textHoverColor = restartColor;
        restartCfg.borderColor = restartColor;
        restartCfg.borderHoverColor = restartColor;
        UIButtonDraw(areaRestart, "Restart", nullptr, &restartCfg, restartHover);
    }

}

bool IsPlayModeActive(void)
{
    return playModeActive;
}

void SetPlayModeActive(bool active)
{
    playModeActive = active;
    if (!playModeActive)
    {
        playPaused = false;
        viewportNavigateMode = false;
    }
}

bool IsPlayPaused(void)
{
    return playPaused;
}

void SetPlayPaused(bool paused)
{
    playPaused = paused;
    if (playPaused && !playModeActive)
        playPaused = false;
}

bool ConsumePlayStopRequested(void)
{
    bool v = playStopRequested;
    playStopRequested = false;
    return v;
}

bool ConsumePlayRestartRequested(void)
{
    bool v = playRestartRequested;
    playRestartRequested = false;
    return v;
}

bool IsTopBarMenuOpen(void)
{
    return addMenuOpen;
}

bool IsViewportNavigateModeActive(void)
{
    return viewportNavigateMode && playModeActive;
}
