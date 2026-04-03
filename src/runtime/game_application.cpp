#include "game_application.h"

#include "app/window_icon_win32.h"
#include "assets/model_manager.h"
#include "editor/ui/properties_panel.h"
#include "editor/viewport/camera_controller.h"
#include "physics/nanquimori_physics.h"
#include "raylib.h"
#include "scene/scene_manager.h"
#include <stdio.h>
#include <string.h>

static Camera runtimeCamera = {0};
static bool runtimeProjectLoaded = false;
static char runtimeProjectPath[512] = {0};
static char runtimeError[256] = {0};
static char runtimeStatusText[128] = "Carregando jogo...";
static ProjectExportSettings runtimeBootSettings = {0};
static bool runtimeProjectLoadPending = false;
static int runtimeProjectLoadDelayFrames = 0;

static const int RUNTIME_BORDERLESS_SAFE_MARGIN = 1;

static void ApplyRuntimeWindowSettingsNow(const ProjectExportSettings *settings);

static int GetRuntimeFullscreenMode(const ProjectExportSettings *settings)
{
    if (!settings)
        return EXPORT_FULLSCREEN_DISABLED;

    int mode = settings->fullscreenMode;
    if (mode < EXPORT_FULLSCREEN_DISABLED || mode > EXPORT_FULLSCREEN_BORDERLESS)
        mode = settings->startFullscreen ? EXPORT_FULLSCREEN_EXCLUSIVE : EXPORT_FULLSCREEN_DISABLED;
    return mode;
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

static int GetRuntimeWindowWidth(int width)
{
    return (width < 320) ? 1280 : width;
}

static int GetRuntimeWindowHeight(int height)
{
    return (height < 240) ? 720 : height;
}

static void GetRuntimeBorderlessWindowBounds(int *outX, int *outY, int *outWidth, int *outHeight)
{
    int monitor = GetCurrentMonitor();
    if (monitor < 0)
        monitor = 0;

    Vector2 monitorPosition = GetMonitorPosition(monitor);
    int width = GetMonitorWidth(monitor);
    int height = GetMonitorHeight(monitor);

#ifdef _WIN32
    if (width <= 0 || height <= 0)
        GetWin32CurrentDisplaySize(&width, &height);
#endif

    if (width <= 0)
        width = 1280;
    if (height <= 0)
        height = 720;

#ifdef _WIN32
    if (outX)
        *outX = (int)monitorPosition.x - RUNTIME_BORDERLESS_SAFE_MARGIN;
    if (outY)
        *outY = (int)monitorPosition.y - RUNTIME_BORDERLESS_SAFE_MARGIN;
    if (outWidth)
        *outWidth = width + RUNTIME_BORDERLESS_SAFE_MARGIN * 2;
    if (outHeight)
        *outHeight = height + RUNTIME_BORDERLESS_SAFE_MARGIN * 2;
#else
    if (outX)
        *outX = (int)monitorPosition.x;
    if (outY)
        *outY = (int)monitorPosition.y;
    if (outWidth)
        *outWidth = width;
    if (outHeight)
        *outHeight = height;
#endif
}

static void NormalizeRuntimeWindowSettings(ProjectExportSettings *settings)
{
    if (!settings)
        return;

    settings->fullscreenMode = GetRuntimeFullscreenMode(settings);
    settings->startFullscreen = (settings->fullscreenMode != EXPORT_FULLSCREEN_DISABLED);
    settings->windowWidth = GetRuntimeWindowWidth(settings->windowWidth);
    settings->windowHeight = GetRuntimeWindowHeight(settings->windowHeight);

    if (settings->fullscreenMode != EXPORT_FULLSCREEN_DISABLED)
    {
        settings->startMaximized = false;
        settings->resizableWindow = false;
    }
}

static void GetRuntimeLaunchWindowSize(const ProjectExportSettings *settings, int *outWidth, int *outHeight)
{
    if (settings && GetRuntimeFullscreenMode(settings) == EXPORT_FULLSCREEN_BORDERLESS)
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        GetRuntimeBorderlessWindowBounds(&x, &y, &width, &height);
        if (outWidth)
            *outWidth = width;
        if (outHeight)
            *outHeight = height;
        return;
    }

    int width = GetRuntimeWindowWidth(settings ? settings->windowWidth : 1280);
    int height = GetRuntimeWindowHeight(settings ? settings->windowHeight : 720);

    if (settings && GetRuntimeFullscreenMode(settings) != EXPORT_FULLSCREEN_DISABLED)
    {
#ifdef _WIN32
        if (!GetWin32CurrentDisplaySize(&width, &height))
#endif
        {
            width = GetRuntimeWindowWidth(width);
            height = GetRuntimeWindowHeight(height);
        }
    }

    if (outWidth)
        *outWidth = width;
    if (outHeight)
        *outHeight = height;
}

static void LoadRuntimeBootSettings(void)
{
    runtimeBootSettings = (ProjectExportSettings){0};
    runtimeBootSettings.windowWidth = 1280;
    runtimeBootSettings.windowHeight = 720;
    runtimeBootSettings.resizableWindow = true;
    runtimeBootSettings.showStartupHud = false;

    char projectJsonPath[512] = {0};
    if (ResolveRuntimeFile("project.json", projectJsonPath, sizeof(projectJsonPath)))
        LoadProjectExportSettingsFromFile(projectJsonPath, &runtimeBootSettings);

    NormalizeRuntimeWindowSettings(&runtimeBootSettings);
}

static void ApplyRuntimeStartupWindowPlacement(const ProjectExportSettings *settings)
{
    if (!settings)
        return;

    ProjectExportSettings applied = *settings;
    NormalizeRuntimeWindowSettings(&applied);

    int fullscreenMode = applied.fullscreenMode;

    if (fullscreenMode == EXPORT_FULLSCREEN_BORDERLESS)
    {
#ifdef _WIN32
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        GetRuntimeBorderlessWindowBounds(&x, &y, &width, &height);
        SetWin32WindowBounds(GetWindowHandle(), x, y, width, height, false);
#endif
        return;
    }
    else
    {
        ApplyRuntimeWindowSettingsNow(&applied);
    }
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

static void ApplyRuntimeWindowSettingsNow(const ProjectExportSettings *settings)
{
    if (!settings)
        return;

    ProjectExportSettings applied = *settings;
    NormalizeRuntimeWindowSettings(&applied);

    int fullscreenMode = applied.fullscreenMode;

    if (fullscreenMode == EXPORT_FULLSCREEN_EXCLUSIVE)
        return;

    if (fullscreenMode == EXPORT_FULLSCREEN_BORDERLESS)
    {
        ClearWindowState(FLAG_WINDOW_RESIZABLE);
        if (!IsWindowState(FLAG_WINDOW_UNDECORATED))
            SetWindowState(FLAG_WINDOW_UNDECORATED);
#ifdef _WIN32
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        GetRuntimeBorderlessWindowBounds(&x, &y, &width, &height);
        SetWin32WindowBounds(GetWindowHandle(), x, y, width, height, false);
#else
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        GetRuntimeBorderlessWindowBounds(&x, &y, &width, &height);
        SetWindowPosition(x, y);
        SetWindowSize(width, height);
#endif
        return;
    }

    if (IsWindowState(FLAG_WINDOW_UNDECORATED))
        ClearWindowState(FLAG_WINDOW_UNDECORATED);

    if (applied.resizableWindow || applied.startMaximized)
        SetWindowState(FLAG_WINDOW_RESIZABLE);
    else
        ClearWindowState(FLAG_WINDOW_RESIZABLE);

    int windowX = 0;
    int windowY = 0;
    GetCenteredRuntimeWindowPosition(applied.windowWidth, applied.windowHeight, &windowX, &windowY);

#ifdef _WIN32
    SetWin32WindowBounds(GetWindowHandle(), windowX, windowY, applied.windowWidth, applied.windowHeight, false);
    SetWin32WindowMaximized(GetWindowHandle(), applied.startMaximized);
#else
    SetWindowSize(applied.windowWidth, applied.windowHeight);
    SetWindowPosition(windowX, windowY);
    if (applied.startMaximized)
        MaximizeWindow();
    else
        RestoreWindow();
#endif
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
    NormalizeRuntimeWindowSettings(&exportSettings);

    if (exportSettings.gameName[0] != '\0')
        SetWindowTitle(exportSettings.gameName);
    SetWin32ConsoleVisible(exportSettings.showConsole);

    ResetNanquimoriPhysicsWorld();
    runtimeProjectLoaded = true;
    return true;
}

void InitializeGameApplication(void)
{
    LoadRuntimeBootSettings();
    runtimeProjectLoaded = false;
    runtimeError[0] = '\0';
    strncpy(runtimeStatusText, "Carregando jogo...", sizeof(runtimeStatusText) - 1);
    runtimeStatusText[sizeof(runtimeStatusText) - 1] = '\0';
    runtimeProjectLoadPending = true;
    runtimeProjectLoadDelayFrames = 1;

    int bootWidth = 1280;
    int bootHeight = 720;
    GetRuntimeLaunchWindowSize(&runtimeBootSettings, &bootWidth, &bootHeight);
    int fullscreenMode = GetRuntimeFullscreenMode(&runtimeBootSettings);

    unsigned int flags = 0;
    if (fullscreenMode == EXPORT_FULLSCREEN_EXCLUSIVE)
        flags |= FLAG_FULLSCREEN_MODE;
    else if (fullscreenMode == EXPORT_FULLSCREEN_BORDERLESS)
        flags |= FLAG_WINDOW_UNDECORATED;
    else if (runtimeBootSettings.resizableWindow || runtimeBootSettings.startMaximized)
        flags |= FLAG_WINDOW_RESIZABLE;
    if (fullscreenMode == EXPORT_FULLSCREEN_DISABLED && runtimeBootSettings.startMaximized)
        flags |= FLAG_WINDOW_MAXIMIZED;

    SetConfigFlags(flags);
    InitWindow(bootWidth,
               bootHeight,
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

    if (fullscreenMode != EXPORT_FULLSCREEN_EXCLUSIVE)
        ApplyRuntimeStartupWindowPlacement(&runtimeBootSettings);
}

void UpdateGameApplication(void)
{
    if (runtimeProjectLoadPending)
    {
        if (runtimeProjectLoadDelayFrames > 0)
        {
            runtimeProjectLoadDelayFrames--;
            return;
        }

        runtimeProjectLoadPending = false;
        ReloadRuntimeProject();
        return;
    }

    if (IsKeyPressed(KEY_R))
    {
        runtimeProjectLoaded = false;
        runtimeError[0] = '\0';
        strncpy(runtimeStatusText, "Recarregando projeto...", sizeof(runtimeStatusText) - 1);
        runtimeStatusText[sizeof(runtimeStatusText) - 1] = '\0';
        runtimeProjectLoadPending = true;
        runtimeProjectLoadDelayFrames = 1;
        return;
    }

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
    else
    {
        const char *statusText = runtimeError[0] != '\0' ? runtimeError : runtimeStatusText;
        Color statusColor = runtimeError[0] != '\0' ? (Color){220, 96, 88, 255} : (Color){220, 224, 232, 255};
        int messageWidth = MeasureText(statusText, 18);
        int x = GetScreenWidth() / 2 - messageWidth / 2;
        int y = GetScreenHeight() / 2 - 10;
        DrawText(statusText, x, y, 18, statusColor);
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
