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
#include "editor/ui/export_dialog.h"
#include "editor/ui/properties_panel.h"
#include "physics/nanquimori_physics.h"
#include "scene/scene_camera.h"
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
static Camera editorCameraBeforePlay = {0};
static bool editorCameraBeforePlaySaved = false;

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
    if (IsExportDialogOpen())
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
    InitExportDialog();

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

    if (IsMouseOverUIRoot())
        EnableMouseForUI();
    else
        DisableMouseForUI();

    if (!playSession)
    {
        UpdateEditorOrbitCamera(&appCamera);
        SetProjectCameraState(appCamera.position, appCamera.target);
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
    }
    playSessionPrev = playSession;

    if (!playSession)
    {
        ProcessarOutliner();

        UpdateFileExplorer();
        UpdateExportDialog();

        char arquivoSelecionado[256] = {0};
        if (FileExplorerArquivoSelecionado(arquivoSelecionado))
            LoadModelFromFile(arquivoSelecionado);
    }

    if (IsViewportEditorInputAllowed(playSession))
    {
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
    if (playMode)
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
        UpdateSplashScreen();
    }

    profLogicMs = (float)((GetTime() - updateStart) * 1000.0);
}

void RenderApplication()
{
    double renderStart = GetTime();
    bool playMode = IsPlayModeActive() && !IsPlayPaused();
    Camera renderCamera = appCamera;
    if (playMode)
    {
        Camera sceneCamera = {0};
        if (GetSceneRenderCamera(&sceneCamera, nullptr))
            renderCamera = sceneCamera;
    }

    BeginDrawing();
    ClearBackground((Color){24, 26, 32, 255});

    BeginMode3D(renderCamera);

    RenderModels();
    if (PropertiesShowCollisions())
        DrawNanquimoriPhysicsDebug();

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

    EndMode3D();

    DrawSelectedObjectOrigins(renderCamera);

    char pendingIconPath[512] = {0};
    if (ConsumePendingProjectIconPath(pendingIconPath, (int)sizeof(pendingIconPath)))
        SaveProjectIconFromCurrentFrame(pendingIconPath);

    if (IsRaycast2DVisible())
    {
        Color rayColor = appRayHit ? GREEN : RED;
        Vector2 screenPos = GetWorldToScreen(appRayEndPos, renderCamera);
        DrawCircleV(screenPos, 5.0f, rayColor);
        DrawCircleLines((int)screenPos.x, (int)screenPos.y, 6.0f, BLACK);
    }

    DrawGizmo(renderCamera, GetScreenWidth());
    DrawInfoPanel();
    DrawOutliner();
    DrawPropertiesPanel();
    DrawTopBar();
    DrawFileMenu();
    DrawExportDialog();
    DrawFileExplorer();
    DrawHelpPanel();
    DrawSplashScreen();

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
    if (!camera)
        return;

    appCamera = *camera;
    SyncCameraControllerToCamera(&appCamera);
}
