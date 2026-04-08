#include "splash_screen.h"
#include "ui_button.h"
#include "ui_style.h"
#include "scene/scene_manager.h"
#include "tools/svg_asset_loader.h"
#include <string.h>
#include <stdio.h>

static SplashScreen splashScreen = {0};
static bool splashInputBlock = false;
static const int RECENT_SLOTS = 3;
static const float RECENT_ROW_STEP = 120.0f;
static const float RECENT_ROW_H = 108.0f;
static const float RECENT_ICON_SIZE = 100.0f;

typedef struct
{
    float screenW;
    float screenH;
    float imgX;
    float imgY;
    Rectangle panel;
    Rectangle buttonNewProject;
    Rectangle buttonYT;
    Rectangle recentTitle;
    Rectangle recentList;
} SplashLayout;

typedef struct
{
    char projectPath[512];
    Texture2D icon;
    bool loaded;
} RecentIconItem;

static RecentIconItem recentIcons[RECENT_SLOTS] = {0};

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

static int GetVisibleRecentCount(void)
{
    int count = GetRecentProjectCount();
    if (count < 0)
        count = 0;
    if (count > RECENT_SLOTS)
        count = RECENT_SLOTS;
    return count;
}

static void BuildProjectIconPath(const char *projectJsonPath, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return;
    out[0] = '\0';
    if (!projectJsonPath || projectJsonPath[0] == '\0')
        return;

    const char *lastSlash = strrchr(projectJsonPath, '/');
    const char *lastBack = strrchr(projectJsonPath, '\\');
    const char *last = lastSlash;
    char sep = '/';
    if (lastBack && (!last || lastBack > last))
    {
        last = lastBack;
        sep = '\\';
    }

    if (!last)
    {
        strncpy(out, "icon.png", outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }

    size_t dirLen = (size_t)(last - projectJsonPath);
    if (dirLen + 10 >= outSize)
        dirLen = outSize - 11;
    memcpy(out, projectJsonPath, dirLen);
    out[dirLen] = sep;
    strncpy(out + dirLen + 1, "icon.png", outSize - dirLen - 2);
    out[outSize - 1] = '\0';
}

static void ClearRecentIconCache(void)
{
    for (int i = 0; i < RECENT_SLOTS; i++)
    {
        if (recentIcons[i].loaded)
        {
            UnloadTexture(recentIcons[i].icon);
            recentIcons[i].loaded = false;
        }
        recentIcons[i].projectPath[0] = '\0';
    }
}

static void RefreshRecentIconCache(void)
{
    int recentCount = GetVisibleRecentCount();
    for (int i = 0; i < RECENT_SLOTS; i++)
    {
        const char *projectPath = (i < recentCount) ? GetRecentProjectPath(i) : "";
        if (!projectPath)
            projectPath = "";

        if (strcmp(recentIcons[i].projectPath, projectPath) == 0)
            continue;

        if (recentIcons[i].loaded)
        {
            UnloadTexture(recentIcons[i].icon);
            recentIcons[i].loaded = false;
        }

        strncpy(recentIcons[i].projectPath, projectPath, sizeof(recentIcons[i].projectPath) - 1);
        recentIcons[i].projectPath[sizeof(recentIcons[i].projectPath) - 1] = '\0';

        if (projectPath[0] == '\0')
            continue;

        char iconPath[512] = {0};
        BuildProjectIconPath(projectPath, iconPath, sizeof(iconPath));
        if (iconPath[0] != '\0' && FileExists(iconPath))
        {
            recentIcons[i].icon = LoadTexture(iconPath);
            recentIcons[i].loaded = (recentIcons[i].icon.id > 0);
        }
    }
}

static void GetRecentDisplayName(const char *projectPath, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return;
    out[0] = '\0';
    if (!projectPath || projectPath[0] == '\0')
        return;

    const char *lastSlash = strrchr(projectPath, '/');
    const char *lastBack = strrchr(projectPath, '\\');
    const char *filePart = lastSlash;
    if (lastBack && (!filePart || lastBack > filePart))
        filePart = lastBack;
    if (!filePart)
    {
        strncpy(out, projectPath, outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }

    size_t folderEnd = (size_t)(filePart - projectPath);
    size_t i = folderEnd;
    while (i > 0 && projectPath[i - 1] != '/' && projectPath[i - 1] != '\\')
        i--;
    size_t len = folderEnd - i;
    if (len == 0)
        len = strlen(projectPath);
    if (len >= outSize)
        len = outSize - 1;
    strncpy(out, projectPath + i, len);
    out[len] = '\0';
}

static void FitTextToWidth(const char *text, char *out, size_t outSize, int fontSize, float maxWidth)
{
    if (!out || outSize == 0)
        return;
    out[0] = '\0';
    if (!text)
        return;

    strncpy(out, text, outSize - 1);
    out[outSize - 1] = '\0';

    if (maxWidth <= 0.0f || MeasureText(out, fontSize) <= maxWidth)
        return;

    const char *ellipsis = "...";
    int ellipsisW = MeasureText(ellipsis, fontSize);
    if (ellipsisW >= maxWidth)
    {
        out[0] = '\0';
        return;
    }

    size_t len = strlen(out);
    while (len > 0)
    {
        out[len - 1] = '\0';
        if ((float)(MeasureText(out, fontSize) + ellipsisW) <= maxWidth)
            break;
        len--;
    }

    size_t baseLen = strlen(out);
    if (baseLen + 3 >= outSize)
        baseLen = outSize - 4;
    memcpy(out + baseLen, ellipsis, 3);
    out[baseLen + 3] = '\0';
}

static SplashLayout GetSplashLayout(void)
{
    SplashLayout layout = {0};
    int recentCount = GetVisibleRecentCount();
    layout.screenW = (float)GetScreenWidth();
    layout.screenH = (float)GetScreenHeight();
    float leftW = (float)splashScreen.splash.width;
    float rightW = 300.0f;
    float gap = 20.0f;
    float panelW = leftW + rightW + gap + 32.0f;
    float leftH = (float)splashScreen.splash.height + 86.0f;
    float rightH = 34.0f + ((recentCount > 0) ? (26.0f + recentCount * RECENT_ROW_STEP) : 0.0f);
    float contentH = (leftH > rightH) ? leftH : rightH;
    float panelH = contentH + 62.0f;
    float panelX = layout.screenW * 0.5f - panelW * 0.5f;
    float panelY = layout.screenH * 0.5f - panelH * 0.5f;

    layout.panel = (Rectangle){panelX, panelY, panelW, panelH};
    layout.imgX = panelX + 16.0f;
    layout.imgY = panelY + 44.0f;

    layout.buttonYT = (Rectangle){
        layout.imgX + leftW * 0.5f - 125.0f,
        layout.imgY + splashScreen.splash.height + 48.0f,
        250.0f,
        30.0f};

    float rightX = layout.imgX + leftW + gap;
    float recentSectionH = RECENT_SLOTS * RECENT_ROW_STEP;
    float rightContentH = 34.0f + ((recentCount > 0) ? (20.0f + recentSectionH) : 0.0f);
    float recentTitleTop = panelY + (panelH - rightContentH) * 0.5f + 42.0f;
    float minTopPadding = panelY + 18.0f;
    if (recentTitleTop < minTopPadding)
        recentTitleTop = minTopPadding;
    layout.buttonNewProject = (Rectangle){
        rightX,
        recentTitleTop - 40.0f,
        rightW,
        30.0f};
    layout.recentTitle = (Rectangle){
        rightX,
        recentTitleTop,
        rightW,
        18.0f};
    layout.recentList = (Rectangle){
        rightX,
        layout.recentTitle.y + 20.0f,
        rightW,
        RECENT_SLOTS * RECENT_ROW_STEP};

    return layout;
}

void InitSplashScreen(void)
{
    char path[512] = {0};
    if (ResolveAssetPath("images/splash.png", path, sizeof(path)))
        splashScreen.splash = LoadTexture(path);
    splashScreen.iconYT = LoadSvgTextureAsset("icons/youtube.svg", 256);
    splashScreen.mostrar = true;
    splashScreen.bloquearFechar = true;
    splashScreen.texturaOk = (splashScreen.splash.id > 0);
    splashScreen.hoverYT = false;
    splashScreen.hoverNewProject = false;
    ClearRecentIconCache();
}

void UpdateSplashScreen(void)
{
    if (splashInputBlock)
    {
        bool anyMouseDown = IsMouseButtonDown(MOUSE_LEFT_BUTTON) ||
                            IsMouseButtonDown(MOUSE_MIDDLE_BUTTON) ||
                            IsMouseButtonDown(MOUSE_RIGHT_BUTTON);
        if (!anyMouseDown)
            splashInputBlock = false;
    }

    if (!splashScreen.mostrar || !splashScreen.texturaOk)
    {
        splashScreen.hoverYT = false;
        splashScreen.hoverNewProject = false;
        return;
    }

    SplashLayout layout = GetSplashLayout();
    RefreshRecentIconCache();
    UIButtonState newProjectState = UIButtonGetState(layout.buttonNewProject);
    UIButtonState ytState = UIButtonGetState(layout.buttonYT);
    splashScreen.hoverNewProject = newProjectState.hovered;
    splashScreen.hoverYT = ytState.hovered;

    if (newProjectState.clicked)
    {
        CreateNewProject();
        splashScreen.mostrar = false;
        splashInputBlock = true;
        return;
    }

    if (ytState.clicked)
        OpenURL("https://www.youtube.com/@Nanquimori");

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        Vector2 mousePos = GetMousePosition();
        int recentCount = GetVisibleRecentCount();
        for (int i = 0; i < recentCount; i++)
        {
            Rectangle row = {
                layout.recentList.x,
                layout.recentList.y + i * RECENT_ROW_STEP,
                layout.recentList.width,
                RECENT_ROW_H};
            if (!CheckCollisionPointRec(mousePos, row))
                continue;

            const char *path = GetRecentProjectPath(i);
            if (path && path[0] != '\0' && FileExists(path))
            {
                OpenProject(path);
                splashScreen.mostrar = false;
                splashInputBlock = true;
                return;
            }
        }
    }

    // Fechar splash ao clicar fora
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        if (splashScreen.bloquearFechar)
        {
            splashScreen.bloquearFechar = false;
        }
        else
        {
            Vector2 mousePos = GetMousePosition();
            if (!CheckCollisionPointRec(mousePos, layout.panel))
            {
                splashScreen.mostrar = false;
                splashInputBlock = true;
            }
        }
    }
}

void DrawSplashScreen(void)
{
    if (!splashScreen.mostrar || !splashScreen.texturaOk)
        return;

    SplashLayout layout = GetSplashLayout();

    // Desenhar overlay escuro atrás do splash
    const UIStyle *style = GetUIStyle();

    DrawRectangle(0, 0, (int)layout.screenW, (int)layout.screenH, style->panelOverlay);

    DrawRectangleRec(layout.panel, BLACK);
    DrawRectangleLinesEx(layout.panel, 2, style->panelBorder);

    DrawTexture(splashScreen.splash, (int)layout.imgX, (int)layout.imgY, WHITE);

    const char *titleText = "Nanquimori Engine";
    const char *versionText = "version v0.1";
    int titleFont = 20;
    int versionFont = 13;
    float logoCenterX = layout.imgX + (float)splashScreen.splash.width * 0.5f;
    int titleX = (int)(logoCenterX - (float)MeasureText(titleText, titleFont) * 0.5f);
    int versionX = (int)(logoCenterX - (float)MeasureText(versionText, versionFont) * 0.5f);
    int titleY = (int)(layout.panel.y + 4);
    int versionY = titleY + titleFont + 4;

    DrawText(titleText, titleX, titleY, titleFont, style->textPrimary);
    DrawText(versionText, versionX, versionY, versionFont, style->textSecondary);

    DrawText("Built on raylib 5.5",
             (int)(layout.panel.x + 16),
             (int)(layout.panel.y + layout.panel.height - 20),
             12,
             style->textMuted);

    UIButtonConfig btnCfg = {0};
    btnCfg.fontSize = 12;
    btnCfg.padding = 10;
    btnCfg.textColor = style->buttonText;
    btnCfg.textHoverColor = style->buttonTextHover;
    btnCfg.bgColor = style->buttonBg;
    btnCfg.bgHoverColor = style->buttonBgHover;
    btnCfg.borderColor = style->buttonBorder;
    btnCfg.borderHoverColor = style->buttonBorder;
    btnCfg.iconColor = style->buttonText;
    btnCfg.iconSize = 20.0f;
    btnCfg.borderThickness = 2.0f;
    UIButtonDraw(layout.buttonNewProject, "Novo Projeto", nullptr, &btnCfg, splashScreen.hoverNewProject);
    UIButtonDraw(layout.buttonYT, "Canal @Nanquimori", &splashScreen.iconYT, &btnCfg, splashScreen.hoverYT);

    int recentCount = GetVisibleRecentCount();
    if (recentCount > 0)
    {
        DrawText("Projetos Recentes", (int)layout.recentTitle.x, (int)layout.recentTitle.y, 14, style->accent);
        Vector2 mouse = GetMousePosition();
        for (int i = 0; i < recentCount; i++)
        {
            Rectangle row = {
                layout.recentList.x,
                layout.recentList.y + i * RECENT_ROW_STEP,
                layout.recentList.width,
                RECENT_ROW_H};
            bool hover = CheckCollisionPointRec(mouse, row);
            DrawRectangleRec(row, hover ? style->itemHover : style->itemBg);
            DrawRectangleLinesEx(row, 1, style->panelBorderSoft);

            Rectangle iconRect = {row.x + 4.0f, row.y + 4.0f, RECENT_ICON_SIZE, RECENT_ICON_SIZE};
            if (recentIcons[i].loaded)
            {
                Rectangle src = {0, 0, (float)recentIcons[i].icon.width, (float)recentIcons[i].icon.height};
                DrawTexturePro(recentIcons[i].icon, src, iconRect, (Vector2){0, 0}, 0.0f, WHITE);
            }
            else
            {
                DrawRectangleRec(iconRect, style->panelBgAlt);
                DrawRectangleLinesEx(iconRect, 1, style->panelBorderSoft);
            }

            const char *path = GetRecentProjectPath(i);
            char label[128] = {0};
            char fittedLabel[128] = {0};
            GetRecentDisplayName(path, label, sizeof(label));
            float textX = row.x + RECENT_ICON_SIZE + 12.0f;
            float textMaxWidth = row.width - (textX - row.x) - 8.0f;
            FitTextToWidth(label[0] ? label : "(projeto)", fittedLabel, sizeof(fittedLabel), 14, textMaxWidth);
            DrawText(fittedLabel, (int)textX, (int)(row.y + (row.height - 14.0f) * 0.5f), 14, hover ? style->textPrimary : style->textSecondary);
        }
    }
}

bool SplashScreenShouldShow(void)
{
    return splashScreen.mostrar;
}

bool SplashScreenIsInputBlocked(void)
{
    return splashInputBlock;
}

void ShowSplashScreen(void)
{
    splashScreen.mostrar = true;
    splashScreen.bloquearFechar = true;
    splashScreen.hoverYT = false;
    splashScreen.hoverNewProject = false;
    splashInputBlock = true;
}

void CloseSplashScreen(void)
{
    splashScreen.mostrar = false;
    splashInputBlock = true;
}

void UnloadSplashScreen(void)
{
    ClearRecentIconCache();
    if (splashScreen.splash.id > 0)
    {
        UnloadTexture(splashScreen.splash);
        splashScreen.splash = (Texture2D){0};
    }
    if (splashScreen.iconYT.id > 0)
    {
        UnloadTexture(splashScreen.iconYT);
        splashScreen.iconYT = (Texture2D){0};
    }
    splashScreen.texturaOk = false;
}

