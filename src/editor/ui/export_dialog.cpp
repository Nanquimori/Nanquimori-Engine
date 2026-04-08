#include "export_dialog.h"

#include "app/game_exporter.h"
#include "file_explorer.h"
#include "scene/scene_manager.h"
#include "text_input.h"
#include "ui_button.h"
#include "ui_style.h"
#include "ui_tooltip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    Rectangle panel;
    Rectangle inputGameName;
    Rectangle inputExeName;
    Rectangle inputOutputDir;
    Rectangle inputIconPath;
    Rectangle inputWidth;
    Rectangle inputHeight;
    Rectangle buttonSelectIcon;
    Rectangle buttonUseProjectIcon;
    Rectangle toggleConsole;
    Rectangle toggleBorderless;
    Rectangle toggleExclusive;
    Rectangle toggleMaximized;
    Rectangle toggleResizable;
    Rectangle buttonExport;
    Rectangle buttonCancel;
    Rectangle statusBounds;
} ExportDialogLayout;

static bool exportDialogOpen = false;
static ProjectExportSettings exportDialogSettings = {0};
static TextInputState exportInputGameName = {0};
static TextInputState exportInputExeName = {0};
static TextInputState exportInputOutputDir = {0};
static TextInputState exportInputIconPath = {0};
static TextInputState exportInputWidth = {0};
static TextInputState exportInputHeight = {0};
static char exportWidthBuffer[16] = {0};
static char exportHeightBuffer[16] = {0};
static char exportDialogStatus[256] = {0};
static bool exportDialogSuccess = false;
static int exportDialogInputWarmupFrames = 0;
static bool exportDialogDirty = false;

static int GetExportFullscreenMode(void)
{
    int mode = exportDialogSettings.fullscreenMode;
    if (mode < EXPORT_FULLSCREEN_DISABLED || mode > EXPORT_FULLSCREEN_BORDERLESS)
        mode = exportDialogSettings.startFullscreen ? EXPORT_FULLSCREEN_EXCLUSIVE : EXPORT_FULLSCREEN_DISABLED;
    return mode;
}

static bool IsExportFullscreenEnabled(void)
{
    return GetExportFullscreenMode() != EXPORT_FULLSCREEN_DISABLED;
}

static bool AreResolutionFieldsLocked(void)
{
    return exportDialogSettings.startMaximized || IsExportFullscreenEnabled();
}

static void SetExportFullscreenMode(int mode)
{
    if (mode < EXPORT_FULLSCREEN_DISABLED || mode > EXPORT_FULLSCREEN_BORDERLESS)
        mode = EXPORT_FULLSCREEN_DISABLED;

    exportDialogSettings.fullscreenMode = mode;
    exportDialogSettings.startFullscreen = (mode != EXPORT_FULLSCREEN_DISABLED);
    if (exportDialogSettings.startFullscreen)
        exportDialogSettings.startMaximized = false;
}

static void ToggleExportFullscreenMode(int mode)
{
    if (GetExportFullscreenMode() == mode)
        SetExportFullscreenMode(EXPORT_FULLSCREEN_DISABLED);
    else
        SetExportFullscreenMode(mode);
}

static void CopyStringSafe(char *dst, size_t dstSize, const char *src)
{
    if (!dst || dstSize == 0)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static int ClampInt(int value, int minValue, int maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static float ClampFloat(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
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
    if ((float)ellipsisW >= maxWidth)
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

static void DrawClippedText(const char *text, Rectangle bounds, int fontSize, Color color)
{
    char fitted[512] = {0};
    FitTextToWidth(text, fitted, sizeof(fitted), fontSize, bounds.width);
    BeginScissorMode((int)bounds.x, (int)bounds.y, (int)bounds.width, (int)bounds.height);
    DrawText(fitted, (int)bounds.x, (int)bounds.y, fontSize, color);
    EndScissorMode();
}

static ExportDialogLayout GetExportDialogLayout(void)
{
    ExportDialogLayout layout = {0};
    float panelW = ClampFloat((float)GetScreenWidth() - 56.0f, 700.0f, 860.0f);
    float panelH = ClampFloat((float)GetScreenHeight() - 56.0f, 650.0f, 744.0f);
    float panelX = (float)GetScreenWidth() * 0.5f - panelW * 0.5f;
    float panelY = (float)GetScreenHeight() * 0.5f - panelH * 0.5f;
    float margin = 24.0f;
    float fieldW = panelW - margin * 2.0f;
    float fieldH = 32.0f;
    float y = panelY + 92.0f;

    layout.panel = (Rectangle){panelX, panelY, panelW, panelH};
    layout.inputGameName = (Rectangle){panelX + margin, y, fieldW, fieldH};
    y += 56.0f;
    layout.inputExeName = (Rectangle){panelX + margin, y, fieldW, fieldH};
    y += 56.0f;
    layout.inputOutputDir = (Rectangle){panelX + margin, y, fieldW, fieldH};
    y += 56.0f;
    layout.inputIconPath = (Rectangle){panelX + margin, y, fieldW, fieldH};
    y += 44.0f;
    layout.buttonSelectIcon = (Rectangle){panelX + margin, y, 156.0f, 30.0f};
    layout.buttonUseProjectIcon = (Rectangle){layout.buttonSelectIcon.x + 168.0f, y, 180.0f, 30.0f};
    y += 76.0f;

    float resolutionW = (fieldW - 16.0f) * 0.5f;
    layout.inputWidth = (Rectangle){panelX + margin, y, resolutionW, fieldH};
    layout.inputHeight = (Rectangle){layout.inputWidth.x + resolutionW + 16.0f, y, resolutionW, fieldH};
    y += 66.0f;

    float toggleW = (fieldW - 12.0f) * 0.5f;
    float toggleH = 50.0f;
    layout.toggleConsole = (Rectangle){panelX + margin, y, toggleW, toggleH};
    layout.toggleBorderless = (Rectangle){layout.toggleConsole.x + toggleW + 12.0f, y, toggleW, toggleH};
    y += toggleH + 12.0f;
    layout.toggleExclusive = (Rectangle){panelX + margin, y, toggleW, toggleH};
    layout.toggleMaximized = (Rectangle){layout.toggleExclusive.x + toggleW + 12.0f, y, toggleW, toggleH};
    y += toggleH + 12.0f;
    layout.toggleResizable = (Rectangle){panelX + margin, y, fieldW, toggleH};

    layout.buttonExport = (Rectangle){panelX + panelW - 256.0f, panelY + panelH - 46.0f, 116.0f, 30.0f};
    layout.buttonCancel = (Rectangle){panelX + panelW - 128.0f, panelY + panelH - 46.0f, 104.0f, 30.0f};
    layout.statusBounds = (Rectangle){panelX + margin, panelY + panelH - 42.0f, layout.buttonExport.x - (panelX + margin) - 20.0f, 18.0f};
    return layout;
}

static void SyncSettingsToUiBuffers(void)
{
    snprintf(exportWidthBuffer, sizeof(exportWidthBuffer), "%d", exportDialogSettings.windowWidth);
    snprintf(exportHeightBuffer, sizeof(exportHeightBuffer), "%d", exportDialogSettings.windowHeight);
    exportWidthBuffer[sizeof(exportWidthBuffer) - 1] = '\0';
    exportHeightBuffer[sizeof(exportHeightBuffer) - 1] = '\0';
}

static void SyncUiBuffersToSettings(void)
{
    if (exportWidthBuffer[0] != '\0')
        exportDialogSettings.windowWidth = ClampInt(atoi(exportWidthBuffer), 320, 7680);
    if (exportHeightBuffer[0] != '\0')
        exportDialogSettings.windowHeight = ClampInt(atoi(exportHeightBuffer), 240, 4320);
}

static void SyncExportDialogSettingsToProject(bool saveProjectFile)
{
    SyncUiBuffersToSettings();
    exportDialogSettings.showStartupHud = false;
    SetProjectExportSettings(&exportDialogSettings);

    if (!saveProjectFile)
        return;

    const char *projectPath = GetProjectPath();
    if (projectPath && projectPath[0] != '\0')
        SaveProject();
}

static void ToggleExportOption(bool *value)
{
    if (!value)
        return;
    *value = !(*value);
}

static void DrawFieldLabel(Rectangle field, const char *label)
{
    Rectangle labelBounds = {field.x, field.y - 16.0f, field.width, 12.0f};
    DrawClippedText(label, labelBounds, 11, GetUIStyle()->textSecondary);
}

static void SetTooltipOnHover(Rectangle rect, Vector2 mouse, const char *id, const char *title, const char *description)
{
    if (CheckCollisionPointRec(mouse, rect))
        SetUITooltip(rect, id, title, description);
}

static Color ExportOptionHoverColor(void)
{
    return (Color){58, 26, 24, 255};
}

static void DrawOptionCard(Rectangle bounds, const char *title, const char *description, bool enabled)
{
    const UIStyle *style = GetUIStyle();
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);

    Color bg = hovered ? ExportOptionHoverColor() : style->itemBg;
    Color border = enabled ? style->accent : style->panelBorderSoft;
    Color badgeBg = enabled ? style->accentSoft : style->buttonBg;
    Color badgeBorder = enabled ? style->accent : style->buttonBorder;
    const char *badgeText = enabled ? "Ativado" : "Desativado";

    DrawRectangleRec(bounds, bg);
    DrawRectangleLinesEx(bounds, 1.0f, border);

    Rectangle titleBounds = {bounds.x + 12.0f, bounds.y + 8.0f, bounds.width - 124.0f, 14.0f};
    Rectangle descBounds = {bounds.x + 12.0f, bounds.y + 26.0f, bounds.width - 124.0f, 12.0f};
    DrawClippedText(title, titleBounds, 12, style->textPrimary);
    DrawClippedText(description, descBounds, 10, style->textMuted);

    Rectangle badge = {bounds.x + bounds.width - 102.0f, bounds.y + bounds.height * 0.5f - 12.0f, 90.0f, 24.0f};
    DrawRectangleRec(badge, badgeBg);
    DrawRectangleLinesEx(badge, 1.0f, badgeBorder);
    DrawClippedText(badgeText, (Rectangle){badge.x + 10.0f, badge.y + 6.0f, badge.width - 20.0f, 12.0f}, 11, style->buttonText);
}

void InitExportDialog(void)
{
    exportDialogOpen = false;
    exportDialogSettings = (ProjectExportSettings){0};
    TextInputInit(&exportInputGameName);
    TextInputInit(&exportInputExeName);
    TextInputInit(&exportInputOutputDir);
    TextInputInit(&exportInputIconPath);
    TextInputInit(&exportInputWidth);
    TextInputInit(&exportInputHeight);
    exportDialogStatus[0] = '\0';
    exportDialogSuccess = false;
    exportDialogInputWarmupFrames = 0;
    exportDialogDirty = false;
    SyncSettingsToUiBuffers();
}

void OpenExportDialog(void)
{
    GetProjectExportSettings(&exportDialogSettings);
    exportDialogSettings.showStartupHud = false;
    TextInputInit(&exportInputGameName);
    TextInputInit(&exportInputExeName);
    TextInputInit(&exportInputOutputDir);
    TextInputInit(&exportInputIconPath);
    TextInputInit(&exportInputWidth);
    TextInputInit(&exportInputHeight);
    exportInputGameName.active = true;
    SyncSettingsToUiBuffers();
    exportDialogStatus[0] = '\0';
    exportDialogSuccess = false;
    exportDialogOpen = true;
    exportDialogInputWarmupFrames = 1;
    exportDialogDirty = false;
}

void CloseExportDialog(void)
{
    if (exportDialogDirty)
        SyncExportDialogSettingsToProject(true);
    exportDialogOpen = false;
    exportDialogInputWarmupFrames = 0;
    exportDialogDirty = false;
}

bool IsExportDialogOpen(void)
{
    return exportDialogOpen;
}

static bool CommitExport(void)
{
    SyncExportDialogSettingsToProject(false);
    ProjectExportSettings settings = exportDialogSettings;

    bool saved = SaveProject();
    if (!saved)
    {
        const char *fallbackName = settings.gameName[0] ? settings.gameName : settings.exeName;
        if (!fallbackName || fallbackName[0] == '\0')
            fallbackName = "Meu Jogo";
        saved = SaveProjectAs(fallbackName);
    }

    if (!saved)
    {
        CopyStringSafe(exportDialogStatus, sizeof(exportDialogStatus), "Nao foi possivel salvar o projeto antes de gerar o build.");
        exportDialogSuccess = false;
        return false;
    }

    SetProjectExportSettings(&settings);
    SaveProject();

    bool ok = ExportGameBuild(&settings, exportDialogStatus, sizeof(exportDialogStatus));
    exportDialogSuccess = ok;
    exportDialogDirty = false;
    GetProjectExportSettings(&exportDialogSettings);
    SyncSettingsToUiBuffers();
    return ok;
}

void UpdateExportDialog(void)
{
    if (!exportDialogOpen)
        return;

    char selectedIconPath[512] = {0};
    if (FileExplorerConsumeSelectedExportIconPath(selectedIconPath))
    {
        CopyStringSafe(exportDialogSettings.iconPath, sizeof(exportDialogSettings.iconPath), selectedIconPath);
        exportDialogDirty = true;
        SyncExportDialogSettingsToProject(false);
    }

    if (fileExplorer.aberto)
        return;

    if (exportDialogInputWarmupFrames > 0)
    {
        exportDialogInputWarmupFrames--;
        return;
    }

    ExportDialogLayout layout = GetExportDialogLayout();
    Vector2 mouse = GetMousePosition();

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !CheckCollisionPointRec(mouse, layout.panel))
    {
        CloseExportDialog();
        return;
    }

    if (UIButtonGetState(layout.buttonSelectIcon).clicked)
    {
        OpenExportIconExplorer();
        return;
    }

    if (UIButtonGetState(layout.buttonUseProjectIcon).clicked)
    {
        exportDialogSettings.iconPath[0] = '\0';
        exportDialogDirty = true;
        SyncExportDialogSettingsToProject(false);
        return;
    }

    if (UIButtonGetState(layout.buttonExport).clicked)
    {
        CommitExport();
        return;
    }

    if (UIButtonGetState(layout.buttonCancel).clicked)
    {
        CloseExportDialog();
        return;
    }

    if (CheckCollisionPointRec(mouse, layout.toggleConsole) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        ToggleExportOption(&exportDialogSettings.showConsole);
        exportDialogDirty = true;
        SyncExportDialogSettingsToProject(false);
    }
    else if (CheckCollisionPointRec(mouse, layout.toggleBorderless) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        ToggleExportFullscreenMode(EXPORT_FULLSCREEN_BORDERLESS);
        exportDialogDirty = true;
        SyncExportDialogSettingsToProject(false);
    }
    else if (CheckCollisionPointRec(mouse, layout.toggleExclusive) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        ToggleExportFullscreenMode(EXPORT_FULLSCREEN_EXCLUSIVE);
        exportDialogDirty = true;
        SyncExportDialogSettingsToProject(false);
    }
    else if (CheckCollisionPointRec(mouse, layout.toggleMaximized) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        ToggleExportOption(&exportDialogSettings.startMaximized);
        if (exportDialogSettings.startMaximized)
        {
            SetExportFullscreenMode(EXPORT_FULLSCREEN_DISABLED);
            exportInputWidth.active = false;
            exportInputHeight.active = false;
        }
        exportDialogDirty = true;
        SyncExportDialogSettingsToProject(false);
    }
    else if (CheckCollisionPointRec(mouse, layout.toggleResizable) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        ToggleExportOption(&exportDialogSettings.resizableWindow);
        exportDialogDirty = true;
        SyncExportDialogSettingsToProject(false);
    }
}

void DrawExportDialog(void)
{
    if (!exportDialogOpen)
        return;

    ExportDialogLayout layout = GetExportDialogLayout();
    const UIStyle *style = GetUIStyle();

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), style->panelOverlay);
    DrawRectangleRec(layout.panel, style->panelBg);
    DrawRectangle((int)layout.panel.x + 1, (int)layout.panel.y + 1, 4, (int)layout.panel.height - 2, style->accent);
    DrawRectangleLinesEx(layout.panel, 1.0f, style->panelBorder);

    DrawText("Configuracao de Build", (int)layout.panel.x + 24, (int)layout.panel.y + 18, 16, style->textPrimary);
    DrawText("Defina como o jogo sera empacotado e como a janela do player deve abrir.", (int)layout.panel.x + 24, (int)layout.panel.y + 40, 11, style->textSecondary);
    DrawText("Essas opcoes ficam salvas no projeto e reaparecem na proxima exportacao.", (int)layout.panel.x + 24, (int)layout.panel.y + 56, 11, style->textMuted);

    TextInputConfig textCfg = {0};
    textCfg.fontSize = 12;
    textCfg.padding = 6;
    textCfg.textColor = style->inputText;
    textCfg.bgColor = style->inputBg;
    textCfg.borderColor = style->inputBorder;
    textCfg.selectionColor = style->inputSelection;
    textCfg.caretColor = style->caret;
    textCfg.filter = TEXT_INPUT_FILTER_NONE;
    textCfg.allowInput = true;

    DrawFieldLabel(layout.inputGameName, "Titulo da janela");
    int gameNameFlags = TextInputDraw(layout.inputGameName, exportDialogSettings.gameName,
                                      (int)sizeof(exportDialogSettings.gameName), &exportInputGameName, &textCfg);

    DrawFieldLabel(layout.inputExeName, "Arquivo principal (.exe)");
    int exeNameFlags = TextInputDraw(layout.inputExeName, exportDialogSettings.exeName,
                                     (int)sizeof(exportDialogSettings.exeName), &exportInputExeName, &textCfg);

    DrawFieldLabel(layout.inputOutputDir, "Pasta do build");
    int outputDirFlags = TextInputDraw(layout.inputOutputDir, exportDialogSettings.outputDir,
                                       (int)sizeof(exportDialogSettings.outputDir), &exportInputOutputDir, &textCfg);

    DrawFieldLabel(layout.inputIconPath, "Icone do executavel");
    int iconPathFlags = TextInputDraw(layout.inputIconPath, exportDialogSettings.iconPath,
                                      (int)sizeof(exportDialogSettings.iconPath), &exportInputIconPath, &textCfg);

    UIButtonConfig neutralButton = {0};
    neutralButton.centerText = true;
    neutralButton.fontSize = 12;
    neutralButton.padding = 6;
    neutralButton.textColor = style->buttonText;
    neutralButton.textHoverColor = style->buttonTextHover;
    neutralButton.bgColor = style->buttonBg;
    neutralButton.bgHoverColor = style->buttonBgHover;
    neutralButton.borderColor = style->buttonBorder;
    neutralButton.borderHoverColor = style->buttonBorder;
    neutralButton.borderThickness = 1.0f;

    Vector2 mouse = GetMousePosition();
    SetTooltipOnHover(layout.inputGameName, mouse, "export.game_name", "Titulo da janela", "Nome exibido na barra da janela do jogo.");
    SetTooltipOnHover(layout.inputExeName, mouse, "export.exe_name", "Arquivo principal", "Nome do executavel gerado no build.");
    SetTooltipOnHover(layout.inputOutputDir, mouse, "export.output_dir", "Pasta do build", "Define a pasta onde o build sera exportado.");
    SetTooltipOnHover(layout.inputIconPath, mouse, "export.icon_path", "Icone do executavel", "Aceita caminho relativo ou absoluto para a imagem do icone.");
    UIButtonDraw(layout.buttonSelectIcon, "Escolher Arquivo", nullptr, &neutralButton,
                 CheckCollisionPointRec(mouse, layout.buttonSelectIcon));
    UIButtonDraw(layout.buttonUseProjectIcon, "Usar Icone do Projeto", nullptr, &neutralButton,
                 CheckCollisionPointRec(mouse, layout.buttonUseProjectIcon));
    SetTooltipOnHover(layout.buttonSelectIcon, mouse, "export.select_icon", "Escolher Arquivo", "Abre o explorador para selecionar a imagem do icone.");
    SetTooltipOnHover(layout.buttonUseProjectIcon, mouse, "export.use_project_icon", "Usar Icone do Projeto", "Reaproveita o icon.png ja salvo no projeto.");

    Rectangle iconHintBounds = {layout.panel.x + 24.0f, layout.buttonSelectIcon.y + 38.0f, layout.panel.width - 48.0f, 14.0f};
    const char *iconHint = exportDialogSettings.iconPath[0] != '\0'
                               ? "Aceita caminho relativo ou absoluto. PNG/JPG/BMP sao convertidos para .ico automaticamente."
                               : "Se ficar vazio, o build usa o icon.png ja salvo dentro da pasta do projeto.";
    DrawClippedText(iconHint, iconHintBounds, 10, style->textMuted);

    TextInputConfig intCfg = textCfg;
    intCfg.filter = TEXT_INPUT_FILTER_INT;
    bool resolutionLocked = AreResolutionFieldsLocked();
    int fullscreenMode = GetExportFullscreenMode();
    if (resolutionLocked)
    {
        intCfg.allowInput = false;
        intCfg.textColor = style->textMuted;
        intCfg.bgColor = style->itemBg;
        intCfg.borderColor = style->panelBorderSoft;
        exportInputWidth.active = false;
        exportInputHeight.active = false;
    }

    DrawFieldLabel(layout.inputWidth, "Largura da janela");
    int widthFlags = TextInputDraw(layout.inputWidth, exportWidthBuffer, (int)sizeof(exportWidthBuffer), &exportInputWidth, &intCfg);
    DrawFieldLabel(layout.inputHeight, "Altura da janela");
    int heightFlags = TextInputDraw(layout.inputHeight, exportHeightBuffer, (int)sizeof(exportHeightBuffer), &exportInputHeight, &intCfg);
    SetTooltipOnHover(layout.inputWidth, mouse, "export.window_width", "Largura da janela", "Define a largura inicial da janela do player.");
    SetTooltipOnHover(layout.inputHeight, mouse, "export.window_height", "Altura da janela", "Define a altura inicial da janela do player.");
    int textFlags = gameNameFlags | exeNameFlags | outputDirFlags | iconPathFlags | widthFlags | heightFlags;
    if (textFlags & (TEXT_INPUT_CHANGED | TEXT_INPUT_SUBMITTED | TEXT_INPUT_DEACTIVATED))
    {
        exportDialogDirty = true;
        SyncExportDialogSettingsToProject(false);
    }
    if (resolutionLocked)
    {
        Rectangle resolutionHintBounds = {layout.inputWidth.x, layout.inputHeight.y + layout.inputHeight.height + 6.0f, layout.inputHeight.x + layout.inputHeight.width - layout.inputWidth.x, 12.0f};
        const char *resolutionHint = "Largura e altura ficam desativadas enquanto Janela maximizada estiver ativa.";
        if (fullscreenMode == EXPORT_FULLSCREEN_BORDERLESS)
            resolutionHint = "Tela cheia sem borda ignora a largura e altura da janela e usa o monitor atual.";
        else if (fullscreenMode == EXPORT_FULLSCREEN_EXCLUSIVE)
            resolutionHint = "Tela cheia exclusiva ignora a largura e altura da janela e usa o monitor atual.";
        DrawClippedText(resolutionHint, resolutionHintBounds, 10, style->textMuted);
    }

    DrawOptionCard(layout.toggleConsole,
                   "Console de depuracao",
                   "Mostra uma janela de console para logs e diagnostico no Windows.",
                   exportDialogSettings.showConsole);
    SetTooltipOnHover(layout.toggleConsole, mouse, "export.console", "Console de depuracao", "Exibe uma janela de console para logs e diagnostico.");
    DrawOptionCard(layout.toggleBorderless,
                   "Tela cheia sem borda (Recomendado)",
                   "Abre como janela sem borda no tamanho do monitor, sem trocar o modo de video.",
                   fullscreenMode == EXPORT_FULLSCREEN_BORDERLESS);
    SetTooltipOnHover(layout.toggleBorderless, mouse, "export.borderless", "Tela cheia sem borda", "Usa o monitor atual sem trocar o modo de video.");
    DrawOptionCard(layout.toggleExclusive,
                   "Tela cheia exclusiva",
                   "Assume o monitor diretamente e pode trocar resolucao ou refresh do monitor.",
                   fullscreenMode == EXPORT_FULLSCREEN_EXCLUSIVE);
    SetTooltipOnHover(layout.toggleExclusive, mouse, "export.exclusive", "Tela cheia exclusiva", "Assume o monitor diretamente e pode mudar resolucao.");
    DrawOptionCard(layout.toggleMaximized,
                   "Janela maximizada",
                   "Abre ocupando a area util do monitor sem depender da largura e altura definidas.",
                   exportDialogSettings.startMaximized);
    SetTooltipOnHover(layout.toggleMaximized, mouse, "export.maximized", "Janela maximizada", "Abre ocupando a area util do monitor e ignora largura/altura.");
    DrawOptionCard(layout.toggleResizable,
                   "Janela redimensionavel",
                   "Permite que o jogador ajuste o tamanho da janela manualmente.",
                   exportDialogSettings.resizableWindow);
    SetTooltipOnHover(layout.toggleResizable, mouse, "export.resizable", "Janela redimensionavel", "Permite que o jogador redimensione a janela manualmente.");

    UIButtonConfig exportButton = neutralButton;
    exportButton.bgColor = style->accentSoft;
    exportButton.bgHoverColor = style->accent;
    exportButton.borderColor = style->accent;
    exportButton.borderHoverColor = style->accent;
    exportButton.textColor = style->buttonText;
    exportButton.textHoverColor = style->buttonTextHover;

    UIButtonDraw(layout.buttonExport, "Gerar Build", nullptr, &exportButton,
                 CheckCollisionPointRec(mouse, layout.buttonExport));
    UIButtonDraw(layout.buttonCancel, "Fechar", nullptr, &neutralButton,
                 CheckCollisionPointRec(mouse, layout.buttonCancel));
    SetTooltipOnHover(layout.buttonExport, mouse, "export.build", "Gerar Build", "Salva o projeto e gera o build exportado.");
    SetTooltipOnHover(layout.buttonCancel, mouse, "export.close", "Fechar", "Fecha a configuracao de build.");

    if (exportDialogStatus[0] != '\0')
    {
        Color statusColor = exportDialogSuccess ? (Color){78, 182, 112, 255} : (Color){200, 88, 78, 255};
        DrawClippedText(exportDialogStatus, layout.statusBounds, 11, statusColor);
    }
}
