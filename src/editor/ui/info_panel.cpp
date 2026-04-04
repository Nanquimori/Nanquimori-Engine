#include "info_panel.h"
#include "assets/model_manager.h"
#include "scene/outliner.h"
#include "rlgl.h"
#include "ui_button.h"
#include "ui_style.h"
#include <cmath>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

static InfoPanel infoPanel = {0};
static bool raycastLineVisible = false;
static bool raycast2DVisible = false;
static bool raycast3DVisible = false;

// Cores (mantidas como antes para contraste entre rótulos e valores)
#define COR_PAINEL (Color){0, 0, 0, 130}
#define COR_BORDA (GetUIStyle()->panelBorder)
#define COR_TEXTO_PRINCIPAL (Color){255, 255, 255, 255}
#define COR_TEXTO_SECUNDARIO (Color){150, 150, 150, 255}

// Constantes
#define PAINEL_LARGURA 280
#define PAINEL_X (10.0f + PAINEL_LARGURA)
#define PAINEL_Y 32.0f
#define PAINEL_W 150.0f
#define PAINEL_H 285.0f

void InitInfoPanel(void)
{
    infoPanel.cameraPos = (Vector3){0, 0, 0};
    infoPanel.cameraTarget = (Vector3){0, 0, 0};
    infoPanel.cameraDistObservada = 0.0f;
    infoPanel.distHit = false;
    infoPanel.mostrar = true;
    infoPanel.logicMs = 0.0f;
    infoPanel.physicsMs = 0.0f;
    infoPanel.renderMs = 0.0f;
    raycastLineVisible = false;
    raycast2DVisible = false;
    raycast3DVisible = false;
}

void UpdateInfoPanel(Vector3 cameraPos, Vector3 cameraTarget, float cameraDistObservada, bool distHit)
{
    infoPanel.cameraPos = cameraPos;
    infoPanel.cameraTarget = cameraTarget;
    infoPanel.cameraDistObservada = cameraDistObservada;
    infoPanel.distHit = distHit;
}

void UpdateInfoPanelProfile(float logicMs, float physicsMs, float renderMs)
{
    infoPanel.logicMs = logicMs;
    infoPanel.physicsMs = physicsMs;
    infoPanel.renderMs = renderMs;
}

void DrawInfoPanel(void)
{
    if (!infoPanel.mostrar)
        return;

    float infoPanelX = PAINEL_X;
    float infoPanelY = PAINEL_Y;
    float infoPanelW = PAINEL_W;
    float infoPanelH = PAINEL_H;

    // Desenhar painel de fundo
    DrawRectangle((int)infoPanelX - 5, (int)infoPanelY - 5, (int)(infoPanelW + 10), (int)(infoPanelH + 10), COR_PAINEL);
    DrawRectangleLinesEx((Rectangle){infoPanelX - 5, infoPanelY - 5, infoPanelW + 10, infoPanelH + 10}, 2, COR_BORDA);

    int lineH = 16;
    int y = (int)infoPanelY;

    // Calcular angulos da camera
    Vector3 dir = {
        infoPanel.cameraTarget.x - infoPanel.cameraPos.x,
        infoPanel.cameraTarget.y - infoPanel.cameraPos.y,
        infoPanel.cameraTarget.z - infoPanel.cameraPos.z};

    float yaw = atan2f(dir.x, dir.z) * (180.0f / PI);
    if (yaw < 0.0f)
        yaw += 360.0f;
    float pitch = 0.0f;
    float dirLenXZ = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (dirLenXZ > 0.0001f)
        pitch = atan2f(dir.y, dirLenXZ) * (180.0f / PI);

    // CAMERA
    DrawText("CAMERA", (int)infoPanelX, y, 12, COR_TEXTO_PRINCIPAL);
    y += lineH;
    DrawText(TextFormat("Pos: %.2f, %.2f, %.2f", infoPanel.cameraPos.x, infoPanel.cameraPos.y, infoPanel.cameraPos.z),
             (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
    DrawText(TextFormat("Angulo H: %.1f", yaw), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
    DrawText(TextFormat("Angulo V: %.1f", pitch), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;

    // RAY
    DrawText("RAY", (int)infoPanelX, y, 12, COR_TEXTO_PRINCIPAL);
    y += lineH;

    if (infoPanel.distHit)
    {
        int distM = (int)infoPanel.cameraDistObservada;
        DrawText(TextFormat("Dist Hit: %d m", distM), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    }
    else
        DrawText("Dist Hit: ----", (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);

    const float btnW = 28.0f;
    const float btnH = 16.0f;
    const float btnGap = 4.0f;
    float btn3DX = infoPanelX + infoPanelW - btnW - 2.0f;
    float btn2DX = btn3DX - btnGap - btnW;
    Rectangle btn2D = {btn2DX, (float)(y - 3), btnW, btnH};
    Rectangle btn3D = {btn3DX, (float)(y - 3), btnW, btnH};

    UIButtonState btn2DState = UIButtonGetState(btn2D);
    UIButtonState btn3DState = UIButtonGetState(btn3D);

    const UIStyle *style = GetUIStyle();
    UIButtonConfig baseCfg = {0};
    baseCfg.centerText = true;
    baseCfg.fontSize = 12;
    baseCfg.padding = 4;
    baseCfg.textColor = style->buttonText;
    baseCfg.textHoverColor = style->buttonTextHover;
    baseCfg.bgColor = style->buttonBg;
    baseCfg.bgHoverColor = style->buttonBgHover;
    baseCfg.borderColor = style->buttonBorder;
    baseCfg.borderHoverColor = style->buttonBorder;
    baseCfg.borderThickness = 1.0f;

    bool active2D = raycast2DVisible;
    bool active3D = raycast3DVisible;

    UIButtonDraw(btn2D, "2D", nullptr, &baseCfg, active2D ? true : btn2DState.hovered);
    UIButtonDraw(btn3D, "3D", nullptr, &baseCfg, active3D ? true : btn3DState.hovered);

    if (btn2DState.clicked)
    {
        if (active2D)
        {
            raycast2DVisible = false;
        }
        else
        {
            raycast2DVisible = true;
            raycast3DVisible = false;
        }
    }
    if (btn3DState.clicked)
    {
        if (active3D)
        {
            raycast3DVisible = false;
        }
        else
        {
            raycast3DVisible = true;
            raycast2DVisible = false;
        }
    }

    raycastLineVisible = raycast2DVisible || raycast3DVisible;
    y += lineH;

    // DISPLAY
    DrawText("DISPLAY", (int)infoPanelX, y, 12, COR_TEXTO_PRINCIPAL);
    y += lineH;
    static int fpsMax = 0;
    static double fpsMaxResetAt = 0.0;
    int fps = GetFPS();
    double now = GetTime();
    if (fpsMaxResetAt <= 0.0)
        fpsMaxResetAt = now + 10.0;
    if (now >= fpsMaxResetAt)
    {
        fpsMax = 0;
        fpsMaxResetAt = now + 10.0;
    }
    if (fps > fpsMax)
        fpsMax = fps;
    float frameMs = GetFrameTime() * 1000.0f;

    DrawText(TextFormat("FPS: %d (Max: %d)", fps, fpsMax), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
    DrawText(TextFormat("Frame: %.2f ms", frameMs), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
    DrawText(TextFormat("Resolucao: %dx%d", GetScreenWidth(), GetScreenHeight()), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
    static int cachedObjetosAtivos = 0;
    static int cachedTris = 0;
    static double nextSceneStatsRefresh = 0.0;
    if (now >= nextSceneStatsRefresh)
    {
        cachedObjetosAtivos = 0;
        for (int i = 0; i < totalObjetos; i++)
            if (objetos[i].ativo)
                cachedObjetosAtivos++;

        cachedTris = 0;
        for (int i = 0; i < modelManager.modelCount; i++)
        {
            LoadedModel *lm = &modelManager.models[i];
            if (!lm->loaded)
                continue;
            if (lm->idObjeto > 0)
            {
                int idx = BuscarIndicePorId(lm->idObjeto);
                if (idx != -1 && !objetos[idx].ativo)
                    continue;
            }
            if (lm->cacheIndex < 0 || lm->cacheIndex >= modelManager.cacheCount)
                continue;
            if (!modelManager.cache[lm->cacheIndex].loaded)
                continue;

            Model model = modelManager.cache[lm->cacheIndex].model;
            for (int m = 0; m < model.meshCount; m++)
                cachedTris += model.meshes[m].triangleCount;
        }

        nextSceneStatsRefresh = now + 0.5;
    }

    DrawText(TextFormat("Triangulos: %d", cachedTris), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
    DrawText(TextFormat("Objetos Ativos: %d/%d", cachedObjetosAtivos, totalObjetos), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
#ifdef _WIN32
    static FILETIME prevIdle = {0}, prevKernel = {0}, prevUser = {0};
    static bool hasPrev = false;
    FILETIME idleTime, kernelTime, userTime;
    float cpuPercent = 0.0f;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime))
    {
        if (hasPrev)
        {
            ULONGLONG idle = (((ULONGLONG)idleTime.dwHighDateTime) << 32) | idleTime.dwLowDateTime;
            ULONGLONG kernel = (((ULONGLONG)kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
            ULONGLONG user = (((ULONGLONG)userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;
            ULONGLONG prevIdleQ = (((ULONGLONG)prevIdle.dwHighDateTime) << 32) | prevIdle.dwLowDateTime;
            ULONGLONG prevKernelQ = (((ULONGLONG)prevKernel.dwHighDateTime) << 32) | prevKernel.dwLowDateTime;
            ULONGLONG prevUserQ = (((ULONGLONG)prevUser.dwHighDateTime) << 32) | prevUser.dwLowDateTime;

            ULONGLONG idleDiff = idle - prevIdleQ;
            ULONGLONG kernelDiff = kernel - prevKernelQ;
            ULONGLONG userDiff = user - prevUserQ;
            ULONGLONG total = kernelDiff + userDiff;
            if (total > 0)
                cpuPercent = (float)(total - idleDiff) * 100.0f / (float)total;
        }
        prevIdle = idleTime;
        prevKernel = kernelTime;
        prevUser = userTime;
        hasPrev = true;
    }
    DrawText(TextFormat("CPU: %.1f%%", cpuPercent), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
#else
    DrawText("CPU: N/A", (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
#endif
    y += lineH;

    float overheadMs = frameMs - (infoPanel.logicMs + infoPanel.renderMs);
    if (overheadMs < 0.0f)
        overheadMs = 0.0f;

    DrawText(TextFormat("Logic: %.2f ms", infoPanel.logicMs), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
    DrawText(TextFormat("Physics: %.2f ms", infoPanel.physicsMs), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
    DrawText(TextFormat("Rasterizer: %.2f ms", infoPanel.renderMs), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
    DrawText(TextFormat("Overhead: %.2f ms", overheadMs), (int)infoPanelX, y, 12, COR_TEXTO_SECUNDARIO);
    y += lineH;
}

void SetInfoPanelVisible(bool visible)
{
    infoPanel.mostrar = visible;
}

bool IsInfoPanelVisible(void)
{
    return infoPanel.mostrar;
}

void SetRaycastLineVisible(bool visible)
{
    raycastLineVisible = visible;
}

bool IsRaycastLineVisible(void)
{
    return raycastLineVisible;
}

void SetRaycast2DVisible(bool visible)
{
    raycast2DVisible = visible;
    if (visible)
        raycast3DVisible = false;
    raycastLineVisible = raycast2DVisible || raycast3DVisible;
}

bool IsRaycast2DVisible(void)
{
    return raycast2DVisible;
}

void SetRaycast3DVisible(bool visible)
{
    raycast3DVisible = visible;
    if (visible)
        raycast2DVisible = false;
    raycastLineVisible = raycast2DVisible || raycast3DVisible;
}

bool IsRaycast3DVisible(void)
{
    return raycast3DVisible;
}

bool IsMouseOverInfoPanel(Vector2 mouse)
{
    if (!infoPanel.mostrar)
        return false;
    Rectangle rect = {PAINEL_X - 5.0f, PAINEL_Y - 5.0f, PAINEL_W + 10.0f, PAINEL_H + 10.0f};
    return CheckCollisionPointRec(mouse, rect);
}
