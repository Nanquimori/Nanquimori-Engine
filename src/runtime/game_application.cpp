#include "game_application.h"

#include "assets/model_manager.h"
#include "editor/viewport/camera_controller.h"
#include "editor/ui/properties_panel.h"
#include "physics/nanquimori_physics.h"
#include "scene/scene_manager.h"
#include "app/window_icon_win32.h"
#include "raylib.h"
#include <stdio.h>
#include <string.h>

static Camera runtimeCamera = {0};
static bool runtimeProjectLoaded = false;
static char runtimeProjectPath[512] = {0};
static char runtimeError[256] = {0};
static ProjectExportSettings runtimeBootSettings = {0};
static ProjectExportSettings runtimeAppliedWindowSettings = {0};
static ProjectExportSettings runtimePendingWindowSettings = {0};
static bool runtimeWindowSettingsPending = false;

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

static bool ResolveRuntimeFile(const char *relativePath, char *out, size_t outSize)
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

static void ApplyRuntimeWindowIcon(void)
{
#ifdef _WIN32
    char iconPath[512] = {0};
    if (ResolveRuntimeFile("game.ico", iconPath, sizeof(iconPath)))
        ApplyWin32WindowIcon(GetWindowHandle(), iconPath);
#endif
}

static void LoadRuntimeBootSettings(void)
{
    runtimeBootSettings = (ProjectExportSettings){0};
    runtimeBootSettings.windowWidth = 1280;
    runtimeBootSettings.windowHeight = 720;
    runtimeBootSettings.resizableWindow = true;
    runtimeBootSettings.showStartupHud = false;

    char projectJsonPath[512] = {0};
    if (!ResolveRuntimeFile("project.json", projectJsonPath, sizeof(projectJsonPath)))
        return;

    LoadProjectExportSettingsFromFile(projectJsonPath, &runtimeBootSettings);
}

static int GetRuntimeWindowWidth(int width)
{
    return (width < 320) ? 1280 : width;
}

static int GetRuntimeWindowHeight(int height)
{
    return (height < 240) ? 720 : height;
}

static void CenterRuntimeWindowOnCurrentMonitor(int width, int height)
{
    int monitor = GetCurrentMonitor();
    if (monitor < 0)
        monitor = 0;

    Vector2 monitorPosition = GetMonitorPosition(monitor);
    int monitorWidth = GetMonitorWidth(monitor);
    int monitorHeight = GetMonitorHeight(monitor);
    if (monitorWidth <= 0 || monitorHeight <= 0)
        return;

    int x = (int)monitorPosition.x + (monitorWidth - width) / 2;
    int y = (int)monitorPosition.y + (monitorHeight - height) / 2;
    SetWindowPosition(x, y);
}

static void GetCenteredRuntimeWindowPosition(int width, int height, int *outX, int *outY)
{
    int x = 0;
    int y = 0;
    int monitor = GetCurrentMonitor();
    if (monitor < 0)
        monitor = 0;

    Vector2 monitorPosition = GetMonitorPosition(monitor);
    int monitorWidth = GetMonitorWidth(monitor);
    int monitorHeight = GetMonitorHeight(monitor);
    if (monitorWidth > 0 && monitorHeight > 0)
    {
        x = (int)monitorPosition.x + (monitorWidth - width) / 2;
        y = (int)monitorPosition.y + (monitorHeight - height) / 2;
    }

    if (outX)
        *outX = x;
    if (outY)
        *outY = y;
}

static void QueueRuntimeWindowSettings(const ProjectExportSettings *settings)
{
    if (!settings)
        return;

    runtimePendingWindowSettings = *settings;
    runtimePendingWindowSettings.windowWidth = GetRuntimeWindowWidth(runtimePendingWindowSettings.windowWidth);
    runtimePendingWindowSettings.windowHeight = GetRuntimeWindowHeight(runtimePendingWindowSettings.windowHeight);
    runtimeWindowSettingsPending = true;
}

static void ApplyRuntimeWindowSettingsNow(const ProjectExportSettings *settings)
{
    if (!settings)
        return;

    ProjectExportSettings applied = *settings;
    applied.windowWidth = GetRuntimeWindowWidth(applied.windowWidth);
    applied.windowHeight = GetRuntimeWindowHeight(applied.windowHeight);

    if (applied.startFullscreen)
    {
        int monitor = GetCurrentMonitor();
        if (monitor < 0)
            monitor = 0;

        Vector2 monitorPosition = GetMonitorPosition(monitor);
        int monitorWidth = GetMonitorWidth(monitor);
        int monitorHeight = GetMonitorHeight(monitor);
        if (monitorWidth <= 0)
            monitorWidth = applied.windowWidth;
        if (monitorHeight <= 0)
            monitorHeight = applied.windowHeight;
        if (monitorWidth > 64)
            monitorWidth -= 1;
        if (monitorHeight > 64)
            monitorHeight -= 1;

        ClearWindowState(FLAG_WINDOW_RESIZABLE);
        SetWindowState(FLAG_WINDOW_UNDECORATED);
        SetWindowState(FLAG_WINDOW_TOPMOST);
#ifdef _WIN32
        SetWin32WindowBounds(GetWindowHandle(), (int)monitorPosition.x, (int)monitorPosition.y, monitorWidth, monitorHeight, true);
#else
        SetWindowPosition((int)monitorPosition.x, (int)monitorPosition.y);
        SetWindowSize(monitorWidth, monitorHeight);
#endif
        SetWindowFocused();
    }
    else
    {
        ClearWindowState(FLAG_WINDOW_UNDECORATED);
        ClearWindowState(FLAG_WINDOW_TOPMOST);
        if (applied.resizableWindow)
            SetWindowState(FLAG_WINDOW_RESIZABLE);
        else
            ClearWindowState(FLAG_WINDOW_RESIZABLE);

        int windowX = 0;
        int windowY = 0;
        GetCenteredRuntimeWindowPosition(applied.windowWidth, applied.windowHeight, &windowX, &windowY);
#ifdef _WIN32
        SetWin32WindowBounds(GetWindowHandle(), windowX, windowY, applied.windowWidth, applied.windowHeight, false);
#else
        SetWindowSize(applied.windowWidth, applied.windowHeight);
        CenterRuntimeWindowOnCurrentMonitor(applied.windowWidth, applied.windowHeight);
#endif
        SetWindowFocused();
    }

    runtimeAppliedWindowSettings = applied;
    runtimeWindowSettingsPending = false;
}

static bool ReloadRuntimeProject(void)
{
    runtimeProjectLoaded = false;
    runtimeError[0] = '\0';

    if (!ResolveRuntimeFile("project.json", runtimeProjectPath, sizeof(runtimeProjectPath)))
    {
        ClearActiveModels();
        snprintf(runtimeError, sizeof(runtimeError), "project.json nao encontrado ao lado do executavel exportado.");
        return false;
    }

    if (!LoadProject(runtimeProjectPath))
    {
        ClearActiveModels();
        snprintf(runtimeError, sizeof(runtimeError), "Falha ao carregar %s", runtimeProjectPath);
        return false;
    }

    Vector3 loadedCamPos = {0};
    Vector3 loadedCamTarget = {0};
    if (ConsumeLoadedProjectCameraState(&loadedCamPos, &loadedCamTarget))
    {
        runtimeCamera.position = loadedCamPos;
        runtimeCamera.target = loadedCamTarget;
        runtimeCamera.up = (Vector3){0.0f, 1.0f, 0.0f};
        SyncCameraControllerToCamera(&runtimeCamera);
    }

    ProjectExportSettings exportSettings = {0};
    GetProjectExportSettings(&exportSettings);
    if (exportSettings.gameName[0] != '\0')
        SetWindowTitle(exportSettings.gameName);

    QueueRuntimeWindowSettings(&exportSettings);
    SetWin32ConsoleVisible(exportSettings.showConsole);

    ResetNanquimoriPhysicsWorld();
    runtimeProjectLoaded = true;
    return true;
}

void InitializeGameApplication(void)
{
    LoadRuntimeBootSettings();
    runtimeAppliedWindowSettings = (ProjectExportSettings){0};
    runtimePendingWindowSettings = (ProjectExportSettings){0};
    runtimeWindowSettingsPending = false;

    unsigned int flags = 0;
    if (runtimeBootSettings.resizableWindow && !runtimeBootSettings.startFullscreen)
        flags |= FLAG_WINDOW_RESIZABLE;
    SetConfigFlags(flags);
    InitWindow(GetRuntimeWindowWidth(runtimeBootSettings.windowWidth),
               GetRuntimeWindowHeight(runtimeBootSettings.windowHeight),
               runtimeBootSettings.gameName[0] != '\0' ? runtimeBootSettings.gameName : "Nanquimori Game");
    ApplyRuntimeWindowIcon();
    SetWin32ConsoleVisible(runtimeBootSettings.showConsole);
    SetTargetFPS(60);

    runtimeCamera = InitCamera();
    DisableMouseForUI();

    InitPropertiesPanel();
    InitModelManager();
    SetRecentProjectsEnabled(false);
    SetSceneManagerBootstrapEnabled(false);
    InitSceneManager();
    InitNanquimoriPhysics();

    ReloadRuntimeProject();
    if (runtimeWindowSettingsPending)
        ApplyRuntimeWindowSettingsNow(&runtimePendingWindowSettings);
}

void UpdateGameApplication(void)
{
    if (IsKeyPressed(KEY_R))
        ReloadRuntimeProject();

    if (runtimeWindowSettingsPending)
        ApplyRuntimeWindowSettingsNow(&runtimePendingWindowSettings);

    if (!runtimeProjectLoaded)
        return;

    DisableMouseForUI();
    UpdateCameraBlender(&runtimeCamera);
    StepNanquimoriPhysics(GetFrameTime());
}

void RenderGameApplication(void)
{
    BeginDrawing();
    ClearBackground((Color){24, 26, 32, 255});

    if (runtimeProjectLoaded)
    {
        BeginMode3D(runtimeCamera);
        RenderModels();
        EndMode3D();
    }

    if (runtimeError[0] != '\0')
    {
        int messageWidth = MeasureText(runtimeError, 16);
        int x = GetScreenWidth() / 2 - messageWidth / 2;
        int y = GetScreenHeight() / 2 - 8;
        DrawText(runtimeError, x, y, 16, (Color){220, 96, 88, 255});
    }

    EndDrawing();
}

void ShutdownGameApplication(void)
{
    ShutdownNanquimoriPhysics();
    UnloadAllModels();
    ReleaseWin32WindowIcon();
    CloseWindow();
}
