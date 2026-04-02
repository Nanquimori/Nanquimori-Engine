#include "application.h"
#include "editor/viewport/gizmo.h"
#include "editor/viewport/camera_controller.h"
#include "scene/outliner.h"
#include "assets/model_manager.h"
#include "editor/ui/file_explorer.h"
#include "editor/ui/help_panel.h"
#include "editor/ui/splash_screen.h"
#include "editor/ui/info_panel.h"
#include "editor/ui/top_bar.h"
#include "editor/ui/properties_panel.h"
#include "physics/nanquimori_physics.h"
#include "scene/scene_manager.h"
#include "window_icon_win32.h"
#include "raymath.h"
#include <cmath>
#include <stdio.h>
#include <cstring>

static Camera appCamera = {0};
static Vector3 appRayEndPos = {0};
static bool appRayHit = false;
static float profLogicMs = 0.0f;
static float profPhysicsMs = 0.0f;
static float profRenderMs = 0.0f;

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

static void ApplyRuntimeWindowIcon(void)
{
#ifdef _WIN32
    char iconPath[512] = {0};
    if (!ResolveWindowIconPath(iconPath, sizeof(iconPath)))
        return;
    ApplyWin32WindowIcon(GetWindowHandle(), iconPath);
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
    if (PropertiesIsBlockingViewport())
        return true;
    if (mouse.x < (float)PAINEL_LARGURA)
        return true;
    if (mouse.x > (float)(screenW - PROPERTIES_PAINEL_LARGURA))
        return true;
    if (mouse.y < 24.0f)
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
    Vector3 hitPos = {0};
    float hitDist = 0.0f;
    int hitObjectId = -1;
    if (RaycastModelsEx(ray, &hitPos, &hitDist, &hitObjectId) && hitObjectId > 0)
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

void InitializeApplication()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED);
    InitWindow(1280, 720, "Nanquimori Engine");
    ApplyRuntimeWindowIcon();
    MaximizeWindow();
    SetExitKey(0);
    SetTargetFPS(0);

    appCamera = InitCamera();
    EnableCursor();

    InitTopBar();
    InitOutliner();
    InitInfoPanel();
    InitHelpPanel();
    InitSplashScreen();
    InitPropertiesPanel();

    InitModelManager();
    InitSceneManager();
    InitFileExplorer();
    InitNanquimoriPhysics();
}

void ShutdownApplication()
{
    UnloadTopBar();
    UnloadSplashScreen();

    ShutdownNanquimoriPhysics();
    UnloadAllModels();
    UnloadFileExplorer();

    ReleaseWin32WindowIcon();

    CloseWindow();
}

void UpdateApplication()
{
    double updateStart = GetTime();
    static bool playSessionPrev = false;

    Vector3 loadedCamPos = {0};
    Vector3 loadedCamTarget = {0};
    if (ConsumeLoadedProjectCameraState(&loadedCamPos, &loadedCamTarget))
    {
        appCamera.position = loadedCamPos;
        appCamera.target = loadedCamTarget;
        appCamera.up = (Vector3){0.0f, 1.0f, 0.0f};
        SyncCameraControllerToCamera(&appCamera);
    }

    bool handledTransformShortcut = PropertiesHandleTransformShortcuts();
    if (!handledTransformShortcut && IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Z))
        Undo();

    if (IsMouseOverUIRoot())
        EnableMouseForUI();
    else
        DisableMouseForUI();

    UpdateCameraBlender(&appCamera);
    SetProjectCameraState(appCamera.position, appCamera.target);

    bool ctrlPressed = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrlPressed && IsKeyPressed(KEY_S))
    {
        if (!SaveProject())
            OpenProjectSaveAs();
    }

    UpdateTopBar();
    bool playSession = IsPlayModeActive();
    bool playPaused = IsPlayPaused();
    bool playMode = playSession && !playPaused;
    bool stopRequested = ConsumePlayStopRequested();
    bool restartRequested = ConsumePlayRestartRequested();

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
    }
    else if (playSessionPrev && !playSession)
    {
        ResetNanquimoriPhysicsWorld();
    }
    playSessionPrev = playSession;

    if (!playSession)
    {
        ProcessarOutliner();

        UpdateFileExplorer();

        char arquivoSelecionado[256] = {0};
        if (FileExplorerArquivoSelecionado(arquivoSelecionado))
            LoadModelFromFile(arquivoSelecionado);
    }

    if (IsViewportEditorInputAllowed(playSession))
    {
        HandleViewportRightClickSelection();
        HandleViewportDuplicateShortcut();
    }

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

    const float MAX_DIST = 1000.0f;
    Vector3 dir = {
        appCamera.target.x - appCamera.position.x,
        appCamera.target.y - appCamera.position.y,
        appCamera.target.z - appCamera.position.z};
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
        Ray ray = {appCamera.position, dir};
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
                appCamera.position.x + dir.x * MAX_DIST,
                appCamera.position.y + dir.y * MAX_DIST,
                appCamera.position.z + dir.z * MAX_DIST};
        }
    }
    else
    {
        appRayEndPos = (Vector3){
            appCamera.position.x + dir.x * MAX_DIST,
            appCamera.position.y + dir.y * MAX_DIST,
            appCamera.position.z + dir.z * MAX_DIST};
    }

    appRayHit = hit;

    UpdateInfoPanel(appCamera.position, appCamera.target, hitDist, hit);
    if (!playMode)
    {
        UpdateFileMenu();
        UpdateSplashScreen();
    }

    profLogicMs = (float)((GetTime() - updateStart) * 1000.0);
}

void RenderApplication()
{
    double renderStart = GetTime();
    BeginDrawing();
    ClearBackground((Color){24, 26, 32, 255});

    BeginMode3D(appCamera);

    RenderModels();
    if (PropertiesShowCollisions())
        DrawNanquimoriPhysicsDebug();

    DrawGrid(10, 1.0f);
    DrawMoveGizmo(appCamera);

    if (IsRaycastLineVisible())
    {
        Color rayColor = appRayHit ? GREEN : RED;
        DrawLine3D(appCamera.position, appRayEndPos, rayColor);
        if (IsRaycast3DVisible())
            DrawSphere(appRayEndPos, 0.08f, rayColor);
    }

    EndMode3D();

    DrawSelectedObjectOrigins(appCamera);

    char pendingIconPath[512] = {0};
    if (ConsumePendingProjectIconPath(pendingIconPath, (int)sizeof(pendingIconPath)))
        SaveProjectIconFromCurrentFrame(pendingIconPath);

    if (IsRaycast2DVisible())
    {
        Color rayColor = appRayHit ? GREEN : RED;
        Vector2 screenPos = GetWorldToScreen(appRayEndPos, appCamera);
        DrawCircleV(screenPos, 5.0f, rayColor);
        DrawCircleLines((int)screenPos.x, (int)screenPos.y, 6.0f, BLACK);
    }

    DrawGizmo(appCamera, GetScreenWidth());
    DrawInfoPanel();
    DrawOutliner();
    DrawPropertiesPanel();
    DrawTopBar();
    DrawFileMenu();
    DrawFileExplorer();
    DrawHelpPanel();
    DrawSplashScreen();

    EndDrawing();

    profRenderMs = (float)((GetTime() - renderStart) * 1000.0);
    UpdateInfoPanelProfile(profLogicMs, profPhysicsMs, profRenderMs);
}
