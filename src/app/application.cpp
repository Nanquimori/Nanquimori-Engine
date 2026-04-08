#include "application.h"
#include "editor/viewport/gizmo.h"
#include "editor/viewport/camera_controller.h"
#include "scene/outliner.h"
#include "assets/model_manager.h"
#include "editor/ui/file_explorer.h"
#include "editor/ui/help_panel.h"
#include "editor/ui/splash_screen.h"
#include "editor/ui/info_panel.h"
#include "editor/ui/editor_layout.h"
#include "editor/ui/top_bar.h"
#include "editor/ui/export_dialog.h"
#include "editor/ui/properties_panel.h"
#include "editor/ui/ui_style.h"
#include "editor/ui/ui_tooltip.h"
#include "tools/svg_asset_loader.h"
#include "physics/nanquimori_physics.h"
#include "scene/scene_camera.h"
#include "scene/scene_manager.h"
#include "window_icon_win32.h"
#include "raymath.h"
#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

static Camera appCamera = {0};
static Vector3 appRayEndPos = {0};
static bool appRayHit = false;
static float profLogicMs = 0.0f;
static float profPhysicsMs = 0.0f;
static float profRenderMs = 0.0f;
static Camera editorCameraBeforePlay = {0};
static bool editorCameraBeforePlaySaved = false;
static Camera editorCameraBeforeActiveLook = {0};
static bool editorCameraBeforeActiveLookSaved = false;
static int editorViewportSceneCameraLookObjectId = -1;
static float editorViewportSceneCameraFrameZoom = 1.0f;
static RenderTexture2D editorViewportSceneCameraTexture = {0};
static int editorViewportSceneCameraTextureWidth = 0;
static int editorViewportSceneCameraTextureHeight = 0;

static bool CamerasApproximatelyEqual(const Camera *a, const Camera *b)
{
    if (!a || !b)
        return false;

    const float eps = 0.0005f;
    if (fabsf(a->position.x - b->position.x) > eps || fabsf(a->position.y - b->position.y) > eps || fabsf(a->position.z - b->position.z) > eps)
        return false;
    if (fabsf(a->target.x - b->target.x) > eps || fabsf(a->target.y - b->target.y) > eps || fabsf(a->target.z - b->target.z) > eps)
        return false;
    if (fabsf(a->up.x - b->up.x) > eps || fabsf(a->up.y - b->up.y) > eps || fabsf(a->up.z - b->up.z) > eps)
        return false;
    if (fabsf(a->fovy - b->fovy) > eps)
        return false;
    return a->projection == b->projection;
}

static Rectangle GetEditorViewportBounds(void)
{
    Rectangle bounds = {
        (float)PAINEL_LARGURA,
        (float)EDITOR_TOP_BAR_HEIGHT,
        (float)(GetScreenWidth() - PAINEL_LARGURA - PROPERTIES_PAINEL_LARGURA),
        (float)(GetScreenHeight() - EDITOR_TOP_BAR_HEIGHT)};

    if (bounds.width < 1.0f)
        bounds.width = 1.0f;
    if (bounds.height < 1.0f)
        bounds.height = 1.0f;
    return bounds;
}

static void DrawEditorLayoutSeams(void)
{
    const UIStyle *style = GetUIStyle();
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    int leftDividerX = PAINEL_LARGURA;
    int rightDividerX = screenW - PROPERTIES_PAINEL_LARGURA;
    const int topDividerY = EDITOR_TOP_BAR_HEIGHT;

    // Keep the main editor seams continuous without slicing through side panel headers.
    DrawLine(leftDividerX, topDividerY, rightDividerX, topDividerY, style->panelBorder);
    DrawLine(leftDividerX, 0, leftDividerX, screenH - 1, style->panelBorder);
    DrawLine(rightDividerX, 0, rightDividerX, screenH - 1, style->panelBorder);
}

static void GetProjectViewportFrameSettings(float *aspectOut, int *widthOut, int *heightOut)
{
    ProjectExportSettings settings = {0};
    GetProjectExportSettings(&settings);

    int width = settings.windowWidth;
    int height = settings.windowHeight;
    if (width < 320)
        width = 1280;
    if (height < 240)
        height = 720;

    if (widthOut)
        *widthOut = width;
    if (heightOut)
        *heightOut = height;
    if (aspectOut)
        *aspectOut = (float)width / (float)height;
}

static Rectangle FitRectToAspect(Rectangle bounds, float aspect)
{
    Rectangle frame = bounds;
    if (aspect <= 0.01f)
        return frame;

    float boundsAspect = bounds.width / bounds.height;
    if (boundsAspect > aspect)
    {
        frame.width = bounds.height * aspect;
        frame.x = bounds.x + (bounds.width - frame.width) * 0.5f;
    }
    else
    {
        frame.height = bounds.width / aspect;
        frame.y = bounds.y + (bounds.height - frame.height) * 0.5f;
    }

    return frame;
}

static Rectangle ScaleRectAroundCenter(Rectangle rect, float scale)
{
    if (scale <= 0.001f)
        scale = 0.001f;

    float centerX = rect.x + rect.width * 0.5f;
    float centerY = rect.y + rect.height * 0.5f;
    Rectangle scaled = {
        centerX - rect.width * scale * 0.5f,
        centerY - rect.height * scale * 0.5f,
        rect.width * scale,
        rect.height * scale};
    return scaled;
}

static Rectangle GetViewportCameraDisplayRect(Rectangle viewportBounds)
{
    return ScaleRectAroundCenter(viewportBounds, editorViewportSceneCameraFrameZoom);
}

static void UnloadEditorViewportSceneCameraTexture(void)
{
    if (editorViewportSceneCameraTexture.id != 0)
        UnloadRenderTexture(editorViewportSceneCameraTexture);
    editorViewportSceneCameraTexture = (RenderTexture2D){0};
    editorViewportSceneCameraTextureWidth = 0;
    editorViewportSceneCameraTextureHeight = 0;
}

static bool EnsureEditorViewportSceneCameraTexture(int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;

    if (editorViewportSceneCameraTexture.id != 0 &&
        editorViewportSceneCameraTextureWidth == width &&
        editorViewportSceneCameraTextureHeight == height)
        return true;

    UnloadEditorViewportSceneCameraTexture();
    editorViewportSceneCameraTexture = LoadRenderTexture(width, height);
    if (editorViewportSceneCameraTexture.id == 0)
        return false;

    editorViewportSceneCameraTextureWidth = width;
    editorViewportSceneCameraTextureHeight = height;
    return true;
}

static void ApplyEditorViewportCamera(const Camera *camera)
{
    if (!camera)
        return;

    appCamera = *camera;
    SyncCameraControllerToCamera(&appCamera);
}

static void ClearEditorViewportSceneCameraLook(bool restorePreviousView, bool resetFrameZoom = true)
{
    if (restorePreviousView && editorCameraBeforeActiveLookSaved)
        ApplyEditorViewportCamera(&editorCameraBeforeActiveLook);

    editorCameraBeforeActiveLookSaved = false;
    editorViewportSceneCameraLookObjectId = -1;
    if (resetFrameZoom)
        editorViewportSceneCameraFrameZoom = 1.0f;
    UnloadEditorViewportSceneCameraTexture();
}

static ObjetoCena *GetLookedSceneCameraObject(void)
{
    if (editorViewportSceneCameraLookObjectId <= 0)
        return nullptr;

    int idx = BuscarIndicePorId(editorViewportSceneCameraLookObjectId);
    if (idx == -1 || !objetos[idx].ativo || !ObjetoEhCamera(&objetos[idx]))
        return nullptr;

    return &objetos[idx];
}

static bool SyncEditorViewportLookedSceneCamera(void)
{
    ObjetoCena *cameraObject = GetLookedSceneCameraObject();
    if (!cameraObject)
    {
        ClearEditorViewportSceneCameraLook(true);
        return false;
    }

    Camera objectCamera = {0};
    if (!BuildSceneCameraFromObject(cameraObject, &objectCamera))
    {
        ClearEditorViewportSceneCameraLook(true);
        return false;
    }

    ApplyEditorViewportCamera(&objectCamera);
    return true;
}

static void AdjustEditorViewportSceneCameraFrameZoom(float stepAmount)
{
    if (fabsf(stepAmount) <= 0.0001f)
        return;

    float scale = powf(1.12f, stepAmount);
    editorViewportSceneCameraFrameZoom = Clamp(editorViewportSceneCameraFrameZoom * scale, 0.20f, 4.0f);
}

static void HandleViewportLookedSceneCameraZoomInput(void)
{
    bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool ctrlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool altHeld = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (editorViewportSceneCameraLookObjectId <= 0 || shiftHeld || ctrlHeld || altHeld)
        return;

    float zoomStep = GetMouseWheelMove();
    if (IsKeyPressed(KEY_KP_ADD))
        zoomStep += 1.0f;
    if (IsKeyPressed(KEY_KP_SUBTRACT))
        zoomStep -= 1.0f;

    AdjustEditorViewportSceneCameraFrameZoom(zoomStep);
}

static void DrawViewportCameraFrameOverlay(void)
{
    Rectangle viewportBounds = GetEditorViewportBounds();
    Rectangle displayRect = GetViewportCameraDisplayRect(viewportBounds);
    float frameAspect = 16.0f / 9.0f;
    int outputWidth = 1280;
    int outputHeight = 720;
    GetProjectViewportFrameSettings(&frameAspect, &outputWidth, &outputHeight);

    Rectangle frame = FitRectToAspect(displayRect, frameAspect);
    const Color matteColor = {14, 16, 22, 138};
    const Color borderColor = {236, 221, 188, 210};
    const Color guideColor = {236, 221, 188, 72};
    const Color labelBg = {10, 12, 16, 224};
    const Color labelText = {236, 221, 188, 255};

    if (frame.y > viewportBounds.y)
        DrawRectangleRec((Rectangle){viewportBounds.x, viewportBounds.y, viewportBounds.width, frame.y - viewportBounds.y}, matteColor);
    if (frame.x > viewportBounds.x)
        DrawRectangleRec((Rectangle){viewportBounds.x, frame.y, frame.x - viewportBounds.x, frame.height}, matteColor);

    float frameRight = frame.x + frame.width;
    float viewportRight = viewportBounds.x + viewportBounds.width;
    if (frameRight < viewportRight)
        DrawRectangleRec((Rectangle){frameRight, frame.y, viewportRight - frameRight, frame.height}, matteColor);

    float frameBottom = frame.y + frame.height;
    float viewportBottom = viewportBounds.y + viewportBounds.height;
    if (frameBottom < viewportBottom)
        DrawRectangleRec((Rectangle){viewportBounds.x, frameBottom, viewportBounds.width, viewportBottom - frameBottom}, matteColor);

    DrawRectangleLinesEx(frame, 1.0f, borderColor);

    float thirdW = frame.width / 3.0f;
    float thirdH = frame.height / 3.0f;
    for (int i = 1; i <= 2; i++)
    {
        DrawLineEx((Vector2){frame.x + thirdW * i, frame.y},
                   (Vector2){frame.x + thirdW * i, frameBottom},
                   1.0f,
                   guideColor);
        DrawLineEx((Vector2){frame.x, frame.y + thirdH * i},
                   (Vector2){frameRight, frame.y + thirdH * i},
                   1.0f,
                   guideColor);
    }

    char frameLabel[64] = {0};
    snprintf(frameLabel, sizeof(frameLabel), "%d x %d", outputWidth, outputHeight);
    int labelWidth = MeasureText(frameLabel, 11) + 18;
    Rectangle labelRect = {frame.x + 10.0f, frame.y + 10.0f, (float)labelWidth, 22.0f};
    DrawRectangleRounded(labelRect, 0.35f, 8, labelBg);
    DrawRectangleLinesEx(labelRect, 1.0f, Fade(borderColor, 0.85f));
    DrawText(frameLabel, (int)labelRect.x + 9, (int)labelRect.y + 6, 11, labelText);
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

static bool ResolveRuntimePlayerExecutablePath(char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';

    const char *candidates[] = {
#ifdef _WIN32
        "NanquimoriPlayer.exe",
        ".cmake/build-debug/NanquimoriPlayer.exe",
        ".cmake/build-release/NanquimoriPlayer.exe",
        "build/NanquimoriPlayer.exe"
#else
        "NanquimoriPlayer",
        ".cmake/build-debug/NanquimoriPlayer",
        ".cmake/build-release/NanquimoriPlayer",
        "build/NanquimoriPlayer"
#endif
    };

    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++)
    {
        if (FileExists(candidates[i]))
        {
            strncpy(out, candidates[i], outSize - 1);
            out[outSize - 1] = '\0';
            return true;
        }
    }

    const char *cwd = GetWorkingDirectory();
    const char *appDir = GetApplicationDirectory();
    for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++)
    {
        if (TryResolvePathFromBaseChain(cwd, candidates[i], out, outSize))
            return true;
        if (TryResolvePathFromBaseChain(appDir, candidates[i], out, outSize))
            return true;
    }

    return false;
}

static bool ResolveWindowIconPath(char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';

    const char *relativePath = "icons/N.ico";
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

static bool ResolveTemporaryRuntimeProjectPaths(char *outProjectDir, size_t outProjectDirSize, char *outProjectPath, size_t outProjectPathSize)
{
    if (!outProjectDir || outProjectDirSize == 0 || !outProjectPath || outProjectPathSize == 0)
        return false;

    outProjectDir[0] = '\0';
    outProjectPath[0] = '\0';

    const char *cwd = GetWorkingDirectory();
    if (!cwd || cwd[0] == '\0')
        return false;

    snprintf(outProjectDir, outProjectDirSize, "%s/tmp_runtime_player", cwd);
    outProjectDir[outProjectDirSize - 1] = '\0';
    snprintf(outProjectPath, outProjectPathSize, "%s/project.json", outProjectDir);
    outProjectPath[outProjectPathSize - 1] = '\0';
    return true;
}

static bool DeleteDirectoryRecursive(const char *dirPath)
{
    if (!dirPath || dirPath[0] == '\0')
        return false;
    if (!DirectoryExists(dirPath))
        return true;

    FilePathList items = LoadDirectoryFiles(dirPath);
    bool ok = true;

    for (unsigned int i = 0; i < items.count; i++)
    {
        const char *itemPath = items.paths[i];
        if (!itemPath || itemPath[0] == '\0')
            continue;

        if (DirectoryExists(itemPath))
        {
            if (!DeleteDirectoryRecursive(itemPath))
                ok = false;
        }
        else
        {
            if (remove(itemPath) != 0 && FileExists(itemPath))
                ok = false;
        }
    }

    UnloadDirectoryFiles(items);

#ifdef _WIN32
    if (_rmdir(dirPath) != 0 && DirectoryExists(dirPath))
        ok = false;
#else
    if (rmdir(dirPath) != 0 && DirectoryExists(dirPath))
        ok = false;
#endif

    return ok;
}

static void CleanupTemporaryRuntimeProjectDir(void)
{
    char temporaryProjectDir[1024] = {0};
    char temporaryProjectPath[1024] = {0};
    if (!ResolveTemporaryRuntimeProjectPaths(temporaryProjectDir, sizeof(temporaryProjectDir), temporaryProjectPath, sizeof(temporaryProjectPath)))
        return;

    DeleteDirectoryRecursive(temporaryProjectDir);
}

static void NormalizeWindowsCommandPath(char *path)
{
    if (!path)
        return;

    for (size_t i = 0; path[i] != '\0'; i++)
    {
        if (path[i] == '/')
            path[i] = '\\';
    }
}

static void ApplyRuntimeWindowIcon(void)
{
#ifdef _WIN32
    char iconPath[512] = {0};
    if (ResolveWindowIconPath(iconPath, sizeof(iconPath)))
    {
        ApplyWin32WindowIcon(GetWindowHandle(), iconPath);
        return;
    }

    Image icon = LoadSvgImageAsset("icons/n.svg", 256);
    if (icon.data && icon.width > 0 && icon.height > 0)
    {
        SetWindowIcon(icon);
        UnloadImage(icon);
    }
#endif
}

static void SaveProjectIconFromCurrentFrame(const char *iconPath)
{
    if (!iconPath || iconPath[0] == '\0')
        return;

    Image frame = LoadImageFromScreen();
    if (!frame.data || frame.width <= 0 || frame.height <= 0)
        return;

    int size = (frame.width < frame.height) ? frame.width : frame.height;
    if (size <= 0)
    {
        UnloadImage(frame);
        return;
    }

    Rectangle crop = {
        (float)((frame.width - size) / 2),
        (float)((frame.height - size) / 2),
        (float)size,
        (float)size};
    Image icon = ImageFromImage(frame, crop);
    ImageResize(&icon, 128, 128);
    ExportImage(icon, iconPath);
    UnloadImage(icon);
    UnloadImage(frame);
}

static bool IsMouseOverUIRoot(void)
{
    Vector2 mouse = GetMousePosition();
    int screenW = GetScreenWidth();

    if (HelpPanelShouldShow() || SplashScreenShouldShow() || SplashScreenIsInputBlocked())
        return true;
    if (fileExplorer.aberto || IsFileMenuOpen() || IsTopBarMenuOpen())
        return true;
    if (IsExportDialogOpen())
        return true;
    if (PropertiesIsBlockingViewport())
        return true;
    if (IsMouseOverEditorLayoutHandle(mouse))
        return true;
    if (mouse.x < (float)PAINEL_LARGURA)
        return true;
    if (mouse.x > (float)(screenW - PROPERTIES_PAINEL_LARGURA))
        return true;
    if (mouse.y < (float)EDITOR_TOP_BAR_HEIGHT)
        return true;
    if (IsMouseOverInfoPanel(mouse))
        return true;
    return false;
}

static bool ShiftHeld(void)
{
    return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

static bool CtrlHeld(void)
{
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}

static bool AltHeld(void)
{
    return IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
}

static bool IsViewportEditorInputAllowed(bool playSession)
{
    return !playSession && !IsMouseOverUIRoot();
}

static void HandleViewportRightClickSelection(void)
{
    if (!IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
        return;

    Ray ray = GetMouseRay(GetMousePosition(), appCamera);
    Vector3 modelHitPos = {0};
    float modelHitDist = 0.0f;
    int modelHitObjectId = -1;
    bool modelHit = RaycastModelsEx(ray, &modelHitPos, &modelHitDist, &modelHitObjectId) && modelHitObjectId > 0;

    Vector3 cameraHitPos = {0};
    float cameraHitDist = 0.0f;
    int cameraHitObjectId = -1;
    bool cameraHit = RaycastSceneCameraHelpers(ray, &cameraHitPos, &cameraHitDist, &cameraHitObjectId) && cameraHitObjectId > 0;

    int hitObjectId = -1;
    if (modelHit && (!cameraHit || modelHitDist <= cameraHitDist))
        hitObjectId = modelHitObjectId;
    else if (cameraHit)
        hitObjectId = cameraHitObjectId;

    if (hitObjectId > 0)
    {
        if (ShiftHeld())
            AdicionarObjetoSelecionadoPorId(hitObjectId);
        else
            SelecionarObjetoPorId(hitObjectId);
        SetSelectedModelByObjetoId(hitObjectId);
    }
    else if (!ShiftHeld())
    {
        SelecionarObjetoPorId(-1);
        SetSelectedModelByObjetoId(-1);
    }
}

static void HandleViewportDuplicateShortcut(void)
{
    if (!ShiftHeld() || CtrlHeld() || AltHeld() || !IsKeyPressed(KEY_D))
        return;

    const Vector3 duplicateOffset = ObterDeslocamentoPadraoDuplicacao();
    DuplicarObjetosSelecionados(duplicateOffset);
}

static void HandleViewportActiveCameraShortcut(void)
{
    if (ShiftHeld() || CtrlHeld() || AltHeld() || !IsKeyPressed(KEY_KP_0))
        return;

    if (editorViewportSceneCameraLookObjectId > 0)
    {
        ClearEditorViewportSceneCameraLook(true);
        return;
    }

    int activeCameraObjectId = -1;
    Camera activeCamera = {0};
    if (!GetSceneRenderCamera(&activeCamera, &activeCameraObjectId))
        return;

    LookThroughSceneCameraObject(activeCameraObjectId);
}

void InitializeApplication()
{
    CleanupTemporaryRuntimeProjectDir();

#ifdef _WIN32
    SetWin32ConsoleCloseCallback(CleanupTemporaryRuntimeProjectDir);
    EnableWin32ConsoleCloseHandler(true);
#endif

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED);
    InitWindow(1280, 720, "Nanquimori Engine");
    ApplyRuntimeWindowIcon();
    MaximizeWindow();
    SetExitKey(0);
    SetTargetFPS(0);

    appCamera = InitCamera();
    EnableCursor();

    InitEditorLayout();
    InitTopBar();
    InitOutliner();
    InitInfoPanel();
    InitHelpPanel();
    InitSplashScreen();
    InitPropertiesPanel();
    InitExportDialog();

    InitModelManager();
    InitSceneManager();
    InitFileExplorer();
    InitNanquimoriPhysics();
}

void ShutdownApplication()
{
    CleanupTemporaryRuntimeProjectDir();

#ifdef _WIN32
    EnableWin32ConsoleCloseHandler(false);
    SetWin32ConsoleCloseCallback(nullptr);
#endif

    UnloadTopBar();
    UnloadSplashScreen();

    ShutdownNanquimoriPhysics();
    UnloadAllModels();
    UnloadFileExplorer();
    UnloadEditorViewportSceneCameraTexture();

    ReleaseWin32WindowIcon();

    CloseWindow();
}

void UpdateApplication()
{
    double updateStart = GetTime();
    static bool playSessionPrev = false;
    static bool navigateModePrev = false;

    BeginUITooltipFrame();

    Vector3 loadedCamPos = {0};
    Vector3 loadedCamTarget = {0};
    if (ConsumeLoadedProjectCameraState(&loadedCamPos, &loadedCamTarget))
    {
        appCamera.position = loadedCamPos;
        appCamera.target = loadedCamTarget;
        appCamera.up = (Vector3){0.0f, 1.0f, 0.0f};
        SyncCameraControllerToCamera(&appCamera);
        ClearEditorViewportSceneCameraLook(false, false);
    }

    bool handledTransformShortcut = PropertiesHandleTransformShortcuts();
    if (!handledTransformShortcut && IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Z))
        Undo();

    bool ctrlPressed = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrlPressed && IsKeyPressed(KEY_S))
    {
        if (!SaveProject())
            OpenProjectSaveAs();
    }

    UpdateTopBar();
    if (!ShiftHeld() && !CtrlHeld() && !AltHeld() &&
        !HelpPanelShouldShow() &&
        !SplashScreenShouldShow() &&
        !SplashScreenIsInputBlocked() &&
        !fileExplorer.aberto &&
        !IsFileMenuOpen() &&
        !IsTopBarMenuOpen() &&
        !IsExportDialogOpen() &&
        IsKeyPressed(KEY_Z))
    {
        ToggleViewportWireframeMode();
    }
    bool layoutInputAllowed = !HelpPanelShouldShow() &&
                              !SplashScreenShouldShow() &&
                              !SplashScreenIsInputBlocked() &&
                              !fileExplorer.aberto &&
                              !IsFileMenuOpen() &&
                              !IsTopBarMenuOpen() &&
                              !IsExportDialogOpen() &&
                              !PropertiesIsBlockingViewport();
    UpdateEditorLayout(layoutInputAllowed);
    UpdateOutlinerLayout(layoutInputAllowed);
    int desiredCursor = MOUSE_CURSOR_DEFAULT;
    if (IsOutlinerObjectPanelHandleActive())
        desiredCursor = MOUSE_CURSOR_RESIZE_NS;
    else if (IsEditorLayoutHandleActive())
        desiredCursor = MOUSE_CURSOR_RESIZE_EW;
    SetMouseCursor(desiredCursor);
    if (ConsumeEditorLayoutPersistRequested() && GetProjectPath()[0] != '\0')
        SaveProject();
    if (ConsumeOutlinerLayoutPersistRequested() && GetProjectPath()[0] != '\0')
        SaveProject();
    bool playSession = IsPlayModeActive();
    bool playPaused = IsPlayPaused();
    bool playMode = playSession && !playPaused;
    bool navigateMode = IsViewportNavigateModeActive();
    bool stopRequested = ConsumePlayStopRequested();
    bool restartRequested = ConsumePlayRestartRequested();
    if (!CtrlHeld() && !AltHeld() && !playSession && IsKeyPressed(KEY_P))
    {
        SetPlayModeActive(true);
        SetPlayPaused(false);
        playSession = true;
        playPaused = false;
        playMode = true;
    }
    if (playSession && IsKeyPressed(KEY_ESCAPE))
    {
        stopRequested = true;
    }

    if (IsMouseOverUIRoot())
        EnableMouseForUI();
    else
        DisableMouseForUI();

    if (playSession && navigateMode && !navigateModePrev)
    {
        Camera sceneCamera = {0};
        if (GetSceneRenderCamera(&sceneCamera, nullptr))
        {
            appCamera = sceneCamera;
            SyncCameraControllerToCamera(&appCamera);
        }
    }

    if (!playSession)
    {
        if (editorViewportSceneCameraLookObjectId > 0)
        {
            SyncEditorViewportLookedSceneCamera();
        }
        else
        {
            UpdateEditorViewportCamera(&appCamera, false);
            SetProjectCameraState(appCamera.position, appCamera.target);
        }
    }
    else if (navigateMode)
    {
        Camera cameraBeforeNavigateUpdate = appCamera;
        Camera sceneCamera = {0};
        int sceneCameraObjectId = -1;
        bool hasSceneCamera = GetSceneRenderCamera(&sceneCamera, &sceneCameraObjectId);

        UpdateEditorViewportCamera(&appCamera, true);

        bool viewportCameraChanged = !CamerasApproximatelyEqual(&cameraBeforeNavigateUpdate, &appCamera);
        if (viewportCameraChanged)
        {
            int sceneCameraIndex = BuscarIndicePorId(sceneCameraObjectId);
            if (hasSceneCamera && sceneCameraIndex != -1)
                CopySceneObjectFromCameraView(&objetos[sceneCameraIndex], &appCamera);
        }
        else if (hasSceneCamera && !CamerasApproximatelyEqual(&sceneCamera, &appCamera))
        {
            ApplyEditorViewportCamera(&sceneCamera);
        }
    }

    if (stopRequested)
    {
        SetPlayModeActive(false);
        ReloadActiveScene();
        ResetNanquimoriPhysicsWorld();
        playMode = false;
        playSession = false;
    }
    else if (restartRequested)
    {
        SetPlayModeActive(true);
        SetPlayPaused(false);
        ReloadActiveScene();
        ResetNanquimoriPhysicsWorld();
        playMode = true;
        playSession = true;
    }

    if (!playSessionPrev && playSession)
    {
        SaveActiveSceneSnapshot();
        ResetNanquimoriPhysicsWorld();
        editorCameraBeforePlay = appCamera;
        editorCameraBeforePlaySaved = true;
        ClearEditorViewportSceneCameraLook(false, false);
    }
    else if (playSessionPrev && !playSession)
    {
        ResetNanquimoriPhysicsWorld();
        if (editorCameraBeforePlaySaved)
        {
            appCamera = editorCameraBeforePlay;
            SyncCameraControllerToCamera(&appCamera);
            editorCameraBeforePlaySaved = false;
        }
        ClearEditorViewportSceneCameraLook(false, false);
    }
    playSessionPrev = playSession;
    navigateModePrev = IsViewportNavigateModeActive();

    if (!playSession)
    {
        if (!IsEditorLayoutDragging() && !IsOutlinerObjectPanelDragging())
            ProcessarOutliner();

        UpdateFileExplorer();
        UpdateExportDialog();

        char arquivoSelecionado[256] = {0};
        if (FileExplorerArquivoSelecionado(arquivoSelecionado))
            LoadModelFromFile(arquivoSelecionado);
    }

    if (IsViewportEditorInputAllowed(playSession))
    {
        HandleViewportLookedSceneCameraZoomInput();
        HandleViewportActiveCameraShortcut();
        if (!IsViewportNavigateModeActive())
            HandleViewportRightClickSelection();
        HandleViewportDuplicateShortcut();
    }

    if (!playMode)
        UpdateMoveGizmo(appCamera);

    if (playMode)
    {
        StepNanquimoriPhysics(GetFrameTime());
        profPhysicsMs = GetNanquimoriPhysicsProfileMs();
    }
    else
    {
        profPhysicsMs = 0.0f;
    }

    Camera debugCamera = appCamera;
    if (playMode && !navigateMode)
    {
        Camera sceneCamera = {0};
        if (GetSceneRenderCamera(&sceneCamera, nullptr))
            debugCamera = sceneCamera;
    }

    const float MAX_DIST = 1000.0f;
    Vector3 dir = {
        debugCamera.target.x - debugCamera.position.x,
        debugCamera.target.y - debugCamera.position.y,
        debugCamera.target.z - debugCamera.position.z};
    float dirLen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dirLen < 0.0001f)
        dirLen = 1.0f;
    dir.x /= dirLen;
    dir.y /= dirLen;
    dir.z /= dirLen;

    float hitDist = MAX_DIST;
    bool hit = false;

    if (IsRaycastLineVisible())
    {
        Ray ray = {debugCamera.position, dir};
        Vector3 hitPos = {0};
        hit = RaycastModels(ray, &hitPos, &hitDist);
        if (!hit)
            hitDist = MAX_DIST;

        if (hit)
        {
            appRayEndPos = hitPos;
        }
        else
        {
            appRayEndPos = (Vector3){
                debugCamera.position.x + dir.x * MAX_DIST,
                debugCamera.position.y + dir.y * MAX_DIST,
                debugCamera.position.z + dir.z * MAX_DIST};
        }
    }
    else
    {
        appRayEndPos = (Vector3){
            debugCamera.position.x + dir.x * MAX_DIST,
            debugCamera.position.y + dir.y * MAX_DIST,
            debugCamera.position.z + dir.z * MAX_DIST};
    }

    appRayHit = hit;

    UpdateInfoPanel(debugCamera.position, debugCamera.target, hitDist, hit);
    if (!playMode)
    {
        UpdateFileMenu();
    }
    UpdateSplashScreen();

    profLogicMs = (float)((GetTime() - updateStart) * 1000.0);
}

void RenderApplication()
{
    double renderStart = GetTime();
    bool playSession = IsPlayModeActive();
    bool playMode = playSession && !IsPlayPaused();
    bool navigateMode = IsViewportNavigateModeActive();
    bool wireframeMode = IsViewportWireframeModeActive();
    bool showCameraFrameOverlay = false;
    Camera renderCamera = appCamera;
    ObjetoCena *renderCameraObject = nullptr;
    if (playSession)
    {
        if (navigateMode)
        {
            renderCamera = appCamera;
            Camera sceneCamera = {0};
            int renderCameraObjectId = -1;
            if (GetSceneRenderCamera(&sceneCamera, &renderCameraObjectId))
            {
                int idx = BuscarIndicePorId(renderCameraObjectId);
                renderCameraObject = (idx != -1) ? &objetos[idx] : nullptr;
                showCameraFrameOverlay = (renderCameraObject != nullptr);
            }
            else
            {
                renderCameraObject = nullptr;
                showCameraFrameOverlay = false;
            }
        }
        else
        {
            Camera sceneCamera = {0};
            int renderCameraObjectId = -1;
            if (GetSceneRenderCamera(&sceneCamera, &renderCameraObjectId))
            {
                renderCamera = sceneCamera;
                int idx = BuscarIndicePorId(renderCameraObjectId);
                renderCameraObject = (idx != -1) ? &objetos[idx] : nullptr;
                showCameraFrameOverlay = (renderCameraObject != nullptr);
            }
            else
            {
                renderCameraObject = nullptr;
                showCameraFrameOverlay = false;
            }
        }
    }
    else
    {
        renderCameraObject = GetLookedSceneCameraObject();
        showCameraFrameOverlay = (renderCameraObject != nullptr);
    }

    BeginDrawing();
    ClearBackground((Color){24, 26, 32, 255});

    if (showCameraFrameOverlay)
    {
        Rectangle viewportBounds = GetEditorViewportBounds();
        int textureWidth = (int)viewportBounds.width;
        int textureHeight = (int)viewportBounds.height;
        if (EnsureEditorViewportSceneCameraTexture(textureWidth, textureHeight))
        {
            BeginTextureMode(editorViewportSceneCameraTexture);
            ClearBackground((Color){24, 26, 32, 0});

            BeginManagedMode3D(renderCamera, renderCameraObject);
            RenderModels(wireframeMode);
            if (PropertiesShowCollisions())
                DrawNanquimoriPhysicsDebug();
            if (!playSession)
                DrawGrid(10, 1.0f);
            if (!playMode)
            {
                DrawMoveGizmo(renderCamera);
                DrawSceneCameraHelpers(renderCamera);
            }
            if (IsRaycastLineVisible())
            {
                Color rayColor = appRayHit ? GREEN : RED;
                DrawLine3D(renderCamera.position, appRayEndPos, rayColor);
                if (IsRaycast3DVisible())
                    DrawSphere(appRayEndPos, 0.08f, rayColor);
            }
            EndManagedMode3D();

            EndTextureMode();

            Rectangle displayRect = GetViewportCameraDisplayRect(viewportBounds);
            Rectangle source = {0.0f, 0.0f, (float)textureWidth, (float)-textureHeight};
            DrawTexturePro(editorViewportSceneCameraTexture.texture,
                           source,
                           displayRect,
                           (Vector2){0.0f, 0.0f},
                           0.0f,
                           WHITE);
        }
    }
    else
    {
        BeginManagedMode3D(renderCamera, renderCameraObject);

        RenderModels(wireframeMode);
        if (PropertiesShowCollisions())
            DrawNanquimoriPhysicsDebug();

        if (!playSession)
            DrawGrid(10, 1.0f);
        if (!playMode)
        {
            DrawMoveGizmo(renderCamera);
            DrawSceneCameraHelpers(renderCamera);
        }

        if (IsRaycastLineVisible())
        {
            Color rayColor = appRayHit ? GREEN : RED;
            DrawLine3D(renderCamera.position, appRayEndPos, rayColor);
            if (IsRaycast3DVisible())
                DrawSphere(appRayEndPos, 0.08f, rayColor);
        }

        EndManagedMode3D();

        DrawSelectedObjectOrigins(renderCamera);
    }

    char pendingIconPath[512] = {0};
    if (ConsumePendingProjectIconPath(pendingIconPath, (int)sizeof(pendingIconPath)))
        SaveProjectIconFromCurrentFrame(pendingIconPath);

    if (!showCameraFrameOverlay && IsRaycast2DVisible())
    {
        Color rayColor = appRayHit ? GREEN : RED;
        Vector2 screenPos = GetWorldToScreen(appRayEndPos, renderCamera);
        DrawCircleV(screenPos, 5.0f, rayColor);
        DrawCircleLines((int)screenPos.x, (int)screenPos.y, 6.0f, BLACK);
    }

    if (showCameraFrameOverlay)
        DrawViewportCameraFrameOverlay();

    DrawGizmo(renderCamera, GetScreenWidth());
    DrawInfoPanel();
    DrawOutliner();
    DrawPropertiesPanel();
    DrawTopBar();
    DrawEditorLayoutSeams();
    DrawEditorLayoutAffordances();
    DrawFileMenu();
    DrawExportDialog();
    DrawFileExplorer();
    DrawHelpPanel();
    DrawSplashScreen();
    DrawUITooltip();

    EndDrawing();

    profRenderMs = (float)((GetTime() - renderStart) * 1000.0);
    UpdateInfoPanelProfile(profLogicMs, profPhysicsMs, profRenderMs);
}

Camera GetEditorViewportCamera(void)
{
    return appCamera;
}

void SetEditorViewportCamera(const Camera *camera)
{
    ClearEditorViewportSceneCameraLook(false);
    ApplyEditorViewportCamera(camera);
}

bool LookThroughSceneCameraObject(int objectId)
{
    int idx = BuscarIndicePorId(objectId);
    if (idx == -1 || !objetos[idx].ativo || !ObjetoEhCamera(&objetos[idx]))
        return false;

    Camera objectCamera = {0};
    if (!BuildSceneCameraFromObject(&objetos[idx], &objectCamera))
        return false;

    if (editorViewportSceneCameraLookObjectId <= 0)
    {
        editorCameraBeforeActiveLook = appCamera;
        editorCameraBeforeActiveLookSaved = true;
    }
    if (editorViewportSceneCameraLookObjectId != objectId)
        editorViewportSceneCameraFrameZoom = 1.0f;

    editorViewportSceneCameraLookObjectId = objectId;
    ApplyEditorViewportCamera(&objectCamera);
    return true;
}

bool LaunchProjectPlayer(char *status, int statusSize)
{
    if (status && statusSize > 0)
        status[0] = '\0';

    const char *savedProjectPath = GetProjectPath();
    const char *savedProjectDir = GetProjectDir();
    bool hasSavedProject = savedProjectPath && savedProjectPath[0] != '\0' && savedProjectDir && savedProjectDir[0] != '\0';
    bool usingTemporaryProject = false;
    char temporaryProjectDir[1024] = {0};
    char temporaryProjectPath[1024] = {0};
    const char *projectDirToLaunch = savedProjectDir;

    if (hasSavedProject)
    {
        CleanupTemporaryRuntimeProjectDir();

        if (!SaveProject())
        {
            if (status && statusSize > 0)
                snprintf(status, (size_t)statusSize, "Nao foi possivel salvar o projeto antes de iniciar o player.");
            return false;
        }
    }
    else
    {
        CleanupTemporaryRuntimeProjectDir();

        if (!ResolveTemporaryRuntimeProjectPaths(temporaryProjectDir, sizeof(temporaryProjectDir), temporaryProjectPath, sizeof(temporaryProjectPath)) ||
            !SaveProjectSnapshotToPath(temporaryProjectPath))
        {
            if (status && statusSize > 0)
                snprintf(status, (size_t)statusSize, "Nao foi possivel preparar a copia temporaria do player.");
            return false;
        }

        projectDirToLaunch = temporaryProjectDir;
        usingTemporaryProject = true;
    }

    if (!projectDirToLaunch || projectDirToLaunch[0] == '\0')
    {
        if (status && statusSize > 0)
            snprintf(status, (size_t)statusSize, "Pasta do projeto nao encontrada.");
        return false;
    }

    char playerPath[1024] = {0};
    if (!ResolveRuntimePlayerExecutablePath(playerPath, sizeof(playerPath)))
    {
        if (status && statusSize > 0)
            snprintf(status, (size_t)statusSize, "Nao encontrei o executavel do player.");
        return false;
    }

#ifdef _WIN32
    char normalizedProjectDir[1024] = {0};
    char normalizedPlayerPath[1024] = {0};
    strncpy(normalizedProjectDir, projectDirToLaunch, sizeof(normalizedProjectDir) - 1);
    normalizedProjectDir[sizeof(normalizedProjectDir) - 1] = '\0';
    strncpy(normalizedPlayerPath, playerPath, sizeof(normalizedPlayerPath) - 1);
    normalizedPlayerPath[sizeof(normalizedPlayerPath) - 1] = '\0';
    NormalizeWindowsCommandPath(normalizedProjectDir);
    NormalizeWindowsCommandPath(normalizedPlayerPath);

    char commandLine[2600] = {0};
    snprintf(commandLine,
             sizeof(commandLine),
             "cmd /c start \"\" /D \"%s\" \"%s\"",
             normalizedProjectDir,
             normalizedPlayerPath);

    int launchResult = system(commandLine);
    if (launchResult != 0)
    {
        if (status && statusSize > 0)
            snprintf(status, (size_t)statusSize, "Falha ao abrir a janela do jogo.");
        return false;
    }
#else
    (void)projectDirToLaunch;
    (void)playerPath;
    if (status && statusSize > 0)
        snprintf(status, (size_t)statusSize, "Inicializacao do player so foi configurada no Windows.");
    return false;
#endif

    if (status && statusSize > 0)
        snprintf(status,
                 (size_t)statusSize,
                 usingTemporaryProject ? "Janela do jogo iniciada com copia temporaria." : "Janela do jogo iniciada.");
    return true;
}

void PrepareForProjectOpen(void)
{
    if (editorCameraBeforePlaySaved)
    {
        appCamera = editorCameraBeforePlay;
        SyncCameraControllerToCamera(&appCamera);
        editorCameraBeforePlaySaved = false;
    }

    SetPlayModeActive(false);
    SetPlayPaused(false);
    ResetNanquimoriPhysicsWorld();
    ClearEditorViewportSceneCameraLook(false);
    EnableMouseForUI();
    SetSelectedModelByObjetoId(-1);
}
