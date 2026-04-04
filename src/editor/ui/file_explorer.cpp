#include "file_explorer.h"
#include "scene/scene_manager.h"
#include "scene/outliner.h"
#include "export_dialog.h"
#include "ui_button.h"
#include "ui_style.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <math.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#define ITEM_ALTURA 36
#define BARRA_SUPERIOR 72
#define FILE_EXPLORER_PAINEL_LARGURA 700
#define FILE_EXPLORER_PAINEL_ALTURA 500
#define FILE_EXPLORER_CARD_LARGURA 152.0f
#define FILE_EXPLORER_CARD_ALTURA 168.0f
#define FILE_EXPLORER_CARD_GAP 12.0f
#define FILE_EXPLORER_PREVIEW_CACHE_SIZE 64
#define FILE_EXPLORER_PREVIEW_TAMANHO 192

// Cores
#define COR_FUNDO (GetUIStyle()->panelBgAlt)
#define COR_PAINEL (GetUIStyle()->panelBg)
#define COR_PAINEL_HOVER (GetUIStyle()->panelBgHover)
#define COR_BORDA (GetUIStyle()->panelBorder)
#define COR_TEXTO (GetUIStyle()->textPrimary)
#define COR_TEXTO_SUAVE (GetUIStyle()->textSecondary)
#define COR_DESTAQUE (GetUIStyle()->accent)

FileExplorer fileExplorer = {0};
static bool pathFeedbackInvalid = false;
static double pathFeedbackUntil = 0.0;

typedef enum
{
    FILE_EXPLORER_ENTRY_FOLDER = 0,
    FILE_EXPLORER_ENTRY_PROJECT,
    FILE_EXPLORER_ENTRY_IMAGE,
    FILE_EXPLORER_ENTRY_MODEL,
    FILE_EXPLORER_ENTRY_FILE
} FileExplorerEntryType;

typedef enum
{
    FILE_EXPLORER_PREVIEW_STORAGE_NONE = 0,
    FILE_EXPLORER_PREVIEW_STORAGE_TEXTURE,
    FILE_EXPLORER_PREVIEW_STORAGE_RENDER_TEXTURE
} FileExplorerPreviewStorage;

typedef struct
{
    Rectangle panel;
    Rectangle pathBox;
    Rectangle backButton;
    Rectangle searchBox;
    Rectangle listButton;
    Rectangle thumbButton;
    Rectangle contentArea;
} FileExplorerLayout;

typedef struct
{
    bool valid;
    bool failed;
    unsigned int lastUsed;
    char key[MAX_FILEPATH_SIZE];
    FileExplorerPreviewStorage storage;
    Texture2D texture;
    RenderTexture2D renderTexture;
} FileExplorerPreviewCacheEntry;

static FileExplorerPreviewCacheEntry previewCache[FILE_EXPLORER_PREVIEW_CACHE_SIZE] = {0};
static unsigned int previewCacheCounter = 1;

static bool MatchFiltroExtensao(const char *caminho, int filtro);
static bool MatchBuscaTexto(const char *caminho, const char *texto);
static bool TrySetCurrentPath(const char *newPath);

static float ClampFloat(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static Color LerpColor(Color from, Color to, float amount)
{
    amount = ClampFloat(amount, 0.0f, 1.0f);

    Color result = {0};
    result.r = (unsigned char)(from.r + (to.r - from.r) * amount);
    result.g = (unsigned char)(from.g + (to.g - from.g) * amount);
    result.b = (unsigned char)(from.b + (to.b - from.b) * amount);
    result.a = (unsigned char)(from.a + (to.a - from.a) * amount);
    return result;
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
    int ellipsisWidth = MeasureText(ellipsis, fontSize);
    if ((float)ellipsisWidth >= maxWidth)
    {
        out[0] = '\0';
        return;
    }

    size_t length = strlen(out);
    while (length > 0)
    {
        out[length - 1] = '\0';
        if ((float)(MeasureText(out, fontSize) + ellipsisWidth) <= maxWidth)
            break;
        length--;
    }

    size_t baseLength = strlen(out);
    if (baseLength + 3 >= outSize)
        baseLength = outSize - 4;
    memcpy(out + baseLength, ellipsis, 3);
    out[baseLength + 3] = '\0';
}

static bool IsImagePreviewFile(const char *path)
{
    if (!path || !IsPathFile(path))
        return false;

    const char *ext = GetFileExtension(path);
    if (!ext)
        return false;

    return TextIsEqual(ext, ".png") || TextIsEqual(ext, ".jpg") || TextIsEqual(ext, ".jpeg") ||
           TextIsEqual(ext, ".bmp") || TextIsEqual(ext, ".ico");
}

static bool IsModelPreviewFile(const char *path)
{
    if (!path || !IsPathFile(path))
        return false;

    const char *ext = GetFileExtension(path);
    if (!ext)
        return false;

    return TextIsEqual(ext, ".obj") || TextIsEqual(ext, ".glb") || TextIsEqual(ext, ".gltf") ||
           TextIsEqual(ext, ".fbx") || TextIsEqual(ext, ".iqm");
}

static bool IsProjectJsonFile(const char *path)
{
    if (!path || !IsPathFile(path))
        return false;
    return TextIsEqual(GetFileName(path), "project.json");
}

static void BuildSiblingPath(const char *basePath, const char *fileName, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return;
    out[0] = '\0';
    if (!basePath || basePath[0] == '\0' || !fileName || fileName[0] == '\0')
        return;

    const char *directoryPath = GetDirectoryPath(basePath);
    if (!directoryPath || directoryPath[0] == '\0')
        return;

    char directory[MAX_FILEPATH_SIZE] = {0};
    TextCopy(directory, directoryPath);
    if (directory[0] == '\0')
        return;

    snprintf(out, outSize, "%s/%s", directory, fileName);
    out[outSize - 1] = '\0';
}

static bool EntryHasProjectFile(const char *path, char *projectJsonOut, size_t projectJsonOutSize)
{
    if (projectJsonOut && projectJsonOutSize > 0)
        projectJsonOut[0] = '\0';
    if (!path || path[0] == '\0')
        return false;

    if (IsProjectJsonFile(path))
    {
        if (projectJsonOut && projectJsonOutSize > 0)
            TextCopy(projectJsonOut, path);
        return true;
    }

    if (!DirectoryExists(path))
        return false;

    char projectJson[MAX_FILEPATH_SIZE] = {0};
    snprintf(projectJson, sizeof(projectJson), "%s/project.json", path);
    projectJson[sizeof(projectJson) - 1] = '\0';
    if (!FileExists(projectJson))
        return false;

    if (projectJsonOut && projectJsonOutSize > 0)
        TextCopy(projectJsonOut, projectJson);
    return true;
}

static bool ResolveProjectIconPath(const char *path, char *iconOut, size_t iconOutSize)
{
    if (!iconOut || iconOutSize == 0)
        return false;
    iconOut[0] = '\0';
    if (!path || path[0] == '\0')
        return false;

    char projectJson[MAX_FILEPATH_SIZE] = {0};
    if (!EntryHasProjectFile(path, projectJson, sizeof(projectJson)))
        return false;

    BuildSiblingPath(projectJson, "icon.png", iconOut, iconOutSize);
    return iconOut[0] != '\0' && FileExists(iconOut);
}

static FileExplorerEntryType GetFileExplorerEntryType(const char *path)
{
    if (!path || path[0] == '\0')
        return FILE_EXPLORER_ENTRY_FILE;

    if (!IsPathFile(path))
    {
        if (DirectoryExists(path))
        {
            char projectJson[MAX_FILEPATH_SIZE] = {0};
            if (EntryHasProjectFile(path, projectJson, sizeof(projectJson)))
                return FILE_EXPLORER_ENTRY_PROJECT;
            return FILE_EXPLORER_ENTRY_FOLDER;
        }
    }

    if (IsProjectJsonFile(path))
        return FILE_EXPLORER_ENTRY_PROJECT;
    if (IsImagePreviewFile(path))
        return FILE_EXPLORER_ENTRY_IMAGE;
    if (IsModelPreviewFile(path))
        return FILE_EXPLORER_ENTRY_MODEL;
    return FILE_EXPLORER_ENTRY_FILE;
}

static const char *GetFileExplorerEntryTypeLabel(FileExplorerEntryType type)
{
    switch (type)
    {
    case FILE_EXPLORER_ENTRY_FOLDER:
        return "PASTA";
    case FILE_EXPLORER_ENTRY_PROJECT:
        return "PROJETO";
    case FILE_EXPLORER_ENTRY_IMAGE:
        return "IMAGEM";
    case FILE_EXPLORER_ENTRY_MODEL:
        return "MODELO 3D";
    default:
        return "ARQUIVO";
    }
}

static FileExplorerLayout GetFileExplorerLayout(void)
{
    FileExplorerLayout layout = {0};

    int larguraTela = GetScreenWidth();
    int alturaTela = GetScreenHeight();
    float painelX = (float)((larguraTela - FILE_EXPLORER_PAINEL_LARGURA) / 2);
    float painelY = (float)((alturaTela - FILE_EXPLORER_PAINEL_ALTURA) / 2);

    layout.panel = (Rectangle){painelX, painelY, (float)FILE_EXPLORER_PAINEL_LARGURA, (float)FILE_EXPLORER_PAINEL_ALTURA};
    layout.pathBox = (Rectangle){painelX + 74.0f, painelY + 20.0f, layout.panel.width - 128.0f, 24.0f};
    layout.backButton = (Rectangle){painelX + layout.panel.width - 44.0f, painelY + 20.0f, 24.0f, 24.0f};

    const float toggleWidth = 68.0f;
    const float toggleGap = 8.0f;
    layout.thumbButton = (Rectangle){layout.panel.x + layout.panel.width - 16.0f - toggleWidth, painelY + BARRA_SUPERIOR + 8.0f, toggleWidth, 24.0f};
    layout.listButton = (Rectangle){layout.thumbButton.x - toggleGap - toggleWidth, layout.thumbButton.y, toggleWidth, 24.0f};
    layout.searchBox = (Rectangle){painelX + 16.0f, painelY + BARRA_SUPERIOR + 8.0f, layout.listButton.x - (painelX + 28.0f), 24.0f};
    layout.contentArea = (Rectangle){painelX + 8.0f, painelY + BARRA_SUPERIOR + 40.0f, layout.panel.width - 16.0f, layout.panel.height - BARRA_SUPERIOR - 48.0f};
    return layout;
}

static Rectangle GetExplorerContentInnerArea(Rectangle contentArea)
{
    Rectangle inner = {
        contentArea.x + 8.0f,
        contentArea.y + 8.0f,
        contentArea.width - 16.0f,
        contentArea.height - 16.0f};

    if (inner.width < 40.0f)
        inner.width = 40.0f;
    if (inner.height < 40.0f)
        inner.height = 40.0f;
    return inner;
}

static void ClearPreviewCacheEntry(FileExplorerPreviewCacheEntry *entry)
{
    if (!entry)
        return;

    if (entry->valid)
    {
        if (entry->storage == FILE_EXPLORER_PREVIEW_STORAGE_TEXTURE && entry->texture.id > 0)
            UnloadTexture(entry->texture);
        else if (entry->storage == FILE_EXPLORER_PREVIEW_STORAGE_RENDER_TEXTURE && entry->renderTexture.id > 0)
            UnloadRenderTexture(entry->renderTexture);
    }

    *entry = (FileExplorerPreviewCacheEntry){0};
}

static void ClearFileExplorerPreviewCache(void)
{
    for (int i = 0; i < FILE_EXPLORER_PREVIEW_CACHE_SIZE; i++)
        ClearPreviewCacheEntry(&previewCache[i]);
    previewCacheCounter = 1;
}

static FileExplorerPreviewCacheEntry *FindPreviewCacheEntry(const char *key)
{
    if (!key || key[0] == '\0')
        return NULL;

    for (int i = 0; i < FILE_EXPLORER_PREVIEW_CACHE_SIZE; i++)
    {
        if ((previewCache[i].valid || previewCache[i].failed) && strcmp(previewCache[i].key, key) == 0)
            return &previewCache[i];
    }
    return NULL;
}

static FileExplorerPreviewCacheEntry *AcquirePreviewCacheSlot(const char *key)
{
    if (!key || key[0] == '\0')
        return NULL;

    FileExplorerPreviewCacheEntry *existing = FindPreviewCacheEntry(key);
    if (existing)
        return existing;

    int freeSlot = -1;
    unsigned int oldestUse = 0;
    int oldestSlot = 0;

    for (int i = 0; i < FILE_EXPLORER_PREVIEW_CACHE_SIZE; i++)
    {
        if (!previewCache[i].valid && !previewCache[i].failed)
        {
            freeSlot = i;
            break;
        }

        if (i == 0 || previewCache[i].lastUsed < oldestUse)
        {
            oldestUse = previewCache[i].lastUsed;
            oldestSlot = i;
        }
    }

    int slot = (freeSlot >= 0) ? freeSlot : oldestSlot;
    ClearPreviewCacheEntry(&previewCache[slot]);
    TextCopy(previewCache[slot].key, key);
    return &previewCache[slot];
}

static bool RenderModelPreviewToTexture(const char *filepath, RenderTexture2D *target)
{
    if (!filepath || !target || target->id == 0 || !IsModelPreviewFile(filepath))
        return false;

    Model model = LoadModel(filepath);
    if (model.meshCount <= 0)
        return false;

    BoundingBox bounds = GetModelBoundingBox(model);
    Vector3 size = {
        bounds.max.x - bounds.min.x,
        bounds.max.y - bounds.min.y,
        bounds.max.z - bounds.min.z};
    Vector3 center = {
        (bounds.min.x + bounds.max.x) * 0.5f,
        (bounds.min.y + bounds.max.y) * 0.5f,
        (bounds.min.z + bounds.max.z) * 0.5f};

    float maxExtent = fmaxf(size.x, fmaxf(size.y, size.z));
    if (maxExtent < 0.001f)
        maxExtent = 1.0f;

    Camera3D camera = {0};
    camera.target = (Vector3){0.0f, size.y * 0.1f, 0.0f};
    camera.position = (Vector3){maxExtent * 1.45f, maxExtent * 1.05f, maxExtent * 1.55f};
    camera.up = (Vector3){0.0f, 1.0f, 0.0f};
    camera.fovy = 38.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    Vector3 drawPosition = {-center.x, -center.y, -center.z};

    BeginTextureMode(*target);
    ClearBackground((Color){20, 18, 18, 255});
    BeginMode3D(camera);
    DrawGrid(10, maxExtent * 0.25f);
    DrawModel(model, drawPosition, 1.0f, WHITE);
    DrawModelWires(model, drawPosition, 1.0f, Fade(BLACK, 0.35f));
    EndMode3D();
    EndTextureMode();

    UnloadModel(model);
    return true;
}

static bool TryCacheTexturePreview(const char *key, FileExplorerPreviewCacheEntry **outEntry)
{
    FileExplorerPreviewCacheEntry *entry = AcquirePreviewCacheSlot(key);
    if (!entry)
        return false;

    if (entry->valid || entry->failed)
    {
        entry->lastUsed = previewCacheCounter++;
        if (outEntry)
            *outEntry = entry;
        return entry->valid;
    }

    Texture2D texture = LoadTexture(key);
    if (texture.id == 0)
    {
        entry->failed = true;
        entry->lastUsed = previewCacheCounter++;
        if (outEntry)
            *outEntry = entry;
        return false;
    }

    entry->valid = true;
    entry->storage = FILE_EXPLORER_PREVIEW_STORAGE_TEXTURE;
    entry->texture = texture;
    entry->lastUsed = previewCacheCounter++;
    if (outEntry)
        *outEntry = entry;
    return true;
}

static bool TryCacheModelPreview(const char *key, FileExplorerPreviewCacheEntry **outEntry)
{
    FileExplorerPreviewCacheEntry *entry = AcquirePreviewCacheSlot(key);
    if (!entry)
        return false;

    if (entry->valid || entry->failed)
    {
        entry->lastUsed = previewCacheCounter++;
        if (outEntry)
            *outEntry = entry;
        return entry->valid;
    }

    RenderTexture2D renderTexture = LoadRenderTexture(FILE_EXPLORER_PREVIEW_TAMANHO, FILE_EXPLORER_PREVIEW_TAMANHO);
    if (renderTexture.id == 0 || !RenderModelPreviewToTexture(key, &renderTexture))
    {
        if (renderTexture.id > 0)
            UnloadRenderTexture(renderTexture);
        entry->failed = true;
        entry->lastUsed = previewCacheCounter++;
        if (outEntry)
            *outEntry = entry;
        return false;
    }

    entry->valid = true;
    entry->storage = FILE_EXPLORER_PREVIEW_STORAGE_RENDER_TEXTURE;
    entry->renderTexture = renderTexture;
    entry->lastUsed = previewCacheCounter++;
    if (outEntry)
        *outEntry = entry;
    return true;
}

static FileExplorerPreviewCacheEntry *GetFileExplorerPreview(const char *path, FileExplorerEntryType type)
{
    if (!path || path[0] == '\0')
        return NULL;

    FileExplorerPreviewCacheEntry *entry = NULL;
    char previewPath[MAX_FILEPATH_SIZE] = {0};

    if (type == FILE_EXPLORER_ENTRY_PROJECT && ResolveProjectIconPath(path, previewPath, sizeof(previewPath)))
    {
        TryCacheTexturePreview(previewPath, &entry);
        return entry;
    }

    if (type == FILE_EXPLORER_ENTRY_IMAGE)
    {
        TryCacheTexturePreview(path, &entry);
        return entry;
    }

    if (type == FILE_EXPLORER_ENTRY_MODEL)
    {
        TryCacheModelPreview(path, &entry);
        return entry;
    }

    return NULL;
}

static void DrawPreviewTextureFit(const FileExplorerPreviewCacheEntry *entry, Rectangle bounds, Color tint)
{
    if (!entry || !entry->valid)
        return;

    Rectangle source = {0};
    float textureWidth = 0.0f;
    float textureHeight = 0.0f;

    if (entry->storage == FILE_EXPLORER_PREVIEW_STORAGE_TEXTURE)
    {
        textureWidth = (float)entry->texture.width;
        textureHeight = (float)entry->texture.height;
        source = (Rectangle){0.0f, 0.0f, textureWidth, textureHeight};
    }
    else if (entry->storage == FILE_EXPLORER_PREVIEW_STORAGE_RENDER_TEXTURE)
    {
        textureWidth = (float)entry->renderTexture.texture.width;
        textureHeight = (float)entry->renderTexture.texture.height;
        source = (Rectangle){0.0f, 0.0f, textureWidth, -textureHeight};
    }

    if (textureWidth <= 0.0f || textureHeight <= 0.0f)
        return;

    float scale = fminf(bounds.width / textureWidth, bounds.height / textureHeight);
    float drawWidth = textureWidth * scale;
    float drawHeight = textureHeight * scale;
    Rectangle dest = {
        bounds.x + (bounds.width - drawWidth) * 0.5f,
        bounds.y + (bounds.height - drawHeight) * 0.5f,
        drawWidth,
        drawHeight};

    if (entry->storage == FILE_EXPLORER_PREVIEW_STORAGE_TEXTURE)
        DrawTexturePro(entry->texture, source, dest, (Vector2){0}, 0.0f, tint);
    else if (entry->storage == FILE_EXPLORER_PREVIEW_STORAGE_RENDER_TEXTURE)
        DrawTexturePro(entry->renderTexture.texture, source, dest, (Vector2){0}, 0.0f, tint);
}

static void DrawFileExplorerPlaceholder(Rectangle bounds, FileExplorerEntryType type, bool hovered)
{
    const UIStyle *style = GetUIStyle();
    Color fill = hovered ? LerpColor(style->panelBgAlt, style->panelBgHover, 0.8f) : style->panelBgAlt;
    Color line = hovered ? LerpColor(style->panelBorderSoft, style->buttonBorder, 0.8f) : style->panelBorderSoft;

    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, 1.0f, line);

    if (type == FILE_EXPLORER_ENTRY_FOLDER || type == FILE_EXPLORER_ENTRY_PROJECT)
    {
        Rectangle tab = {bounds.x + 12.0f, bounds.y + 16.0f, bounds.width * 0.34f, bounds.height * 0.12f};
        Rectangle body = {bounds.x + 10.0f, bounds.y + 24.0f, bounds.width - 20.0f, bounds.height - 34.0f};
        DrawRectangleRounded(body, 0.12f, 8, hovered ? style->buttonBgHover : style->buttonBg);
        DrawRectangleRounded(tab, 0.18f, 8, hovered ? style->buttonBorder : style->panelBorder);
        if (type == FILE_EXPLORER_ENTRY_PROJECT)
            DrawText("P", (int)(bounds.x + bounds.width * 0.5f - 6.0f), (int)(bounds.y + bounds.height * 0.5f - 10.0f), 20, style->buttonTextHover);
    }
    else if (type == FILE_EXPLORER_ENTRY_MODEL)
    {
        Rectangle cube = {bounds.x + 18.0f, bounds.y + 18.0f, bounds.width - 36.0f, bounds.height - 36.0f};
        DrawRectangleRounded(cube, 0.1f, 10, hovered ? style->buttonBgHover : style->buttonBg);
        DrawRectangleLinesEx(cube, 1.0f, hovered ? style->buttonBorder : style->panelBorderSoft);
        DrawLineEx((Vector2){cube.x, cube.y + cube.height * 0.25f}, (Vector2){cube.x + cube.width * 0.5f, cube.y}, 2.0f, style->panelBorderSoft);
        DrawLineEx((Vector2){cube.x + cube.width, cube.y + cube.height * 0.25f}, (Vector2){cube.x + cube.width * 0.5f, cube.y}, 2.0f, style->panelBorderSoft);
        DrawLineEx((Vector2){cube.x, cube.y + cube.height * 0.25f}, (Vector2){cube.x, cube.y + cube.height * 0.75f}, 2.0f, style->panelBorderSoft);
        DrawLineEx((Vector2){cube.x + cube.width, cube.y + cube.height * 0.25f}, (Vector2){cube.x + cube.width, cube.y + cube.height * 0.75f}, 2.0f, style->panelBorderSoft);
        DrawLineEx((Vector2){cube.x + cube.width * 0.5f, cube.y}, (Vector2){cube.x + cube.width * 0.5f, cube.y + cube.height * 0.5f}, 2.0f, style->panelBorderSoft);
        DrawLineEx((Vector2){cube.x, cube.y + cube.height * 0.75f}, (Vector2){cube.x + cube.width * 0.5f, cube.y + cube.height}, 2.0f, style->panelBorderSoft);
        DrawLineEx((Vector2){cube.x + cube.width, cube.y + cube.height * 0.75f}, (Vector2){cube.x + cube.width * 0.5f, cube.y + cube.height}, 2.0f, style->panelBorderSoft);
    }
    else
    {
        Rectangle paper = {bounds.x + 20.0f, bounds.y + 14.0f, bounds.width - 40.0f, bounds.height - 28.0f};
        DrawRectangleRounded(paper, 0.08f, 8, hovered ? style->buttonBgHover : style->buttonBg);
        DrawRectangleLinesEx(paper, 1.0f, style->panelBorderSoft);
        for (int i = 0; i < 4; i++)
        {
            float lineY = paper.y + 18.0f + i * 14.0f;
            DrawLine((int)(paper.x + 12.0f), (int)lineY, (int)(paper.x + paper.width - 12.0f), (int)lineY, style->panelBorderSoft);
        }
    }
}

static int BuildVisibleEntryIndexList(int **outIndices)
{
    if (outIndices)
        *outIndices = NULL;

    if (!fileExplorer.arquivosCarregados || fileExplorer.arquivos.count <= 0)
        return 0;

    int total = 0;
    for (int i = 0; i < (int)fileExplorer.arquivos.count; i++)
    {
        if (!MatchFiltroExtensao(fileExplorer.arquivos.paths[i], fileExplorer.extensaoFiltro))
            continue;
        if (!MatchBuscaTexto(fileExplorer.arquivos.paths[i], fileExplorer.bufferTexto))
            continue;
        total++;
    }

    if (total <= 0 || !outIndices)
        return total;

    int *indices = (int *)MemAlloc(sizeof(int) * (size_t)total);
    if (!indices)
        return 0;

    int cursor = 0;
    for (int i = 0; i < (int)fileExplorer.arquivos.count; i++)
    {
        if (!MatchFiltroExtensao(fileExplorer.arquivos.paths[i], fileExplorer.extensaoFiltro))
            continue;
        if (!MatchBuscaTexto(fileExplorer.arquivos.paths[i], fileExplorer.bufferTexto))
            continue;
        indices[cursor++] = i;
    }

    *outIndices = indices;
    return total;
}

static int GetExplorerGridColumns(Rectangle contentArea)
{
    float fullCardWidth = FILE_EXPLORER_CARD_LARGURA + FILE_EXPLORER_CARD_GAP;
    int columns = (int)((contentArea.width + FILE_EXPLORER_CARD_GAP) / fullCardWidth);
    if (columns < 1)
        columns = 1;
    return columns;
}

static float GetExplorerContentHeight(int itemCount, Rectangle contentArea)
{
    if (itemCount <= 0)
        return 0.0f;

    if (fileExplorer.viewMode == FILE_EXPLORER_VIEW_LIST)
        return itemCount * ITEM_ALTURA;

    int columns = GetExplorerGridColumns(contentArea);
    int rows = (itemCount + columns - 1) / columns;
    return rows * FILE_EXPLORER_CARD_ALTURA + (rows - 1) * FILE_EXPLORER_CARD_GAP;
}

static float GetExplorerMaxScroll(int itemCount, Rectangle contentArea)
{
    float contentHeight = GetExplorerContentHeight(itemCount, contentArea);
    float maxScroll = contentHeight - contentArea.height;
    if (maxScroll < 0.0f)
        maxScroll = 0.0f;
    return maxScroll;
}

static Rectangle GetExplorerItemRect(Rectangle contentArea, int visualIndex)
{
    if (fileExplorer.viewMode == FILE_EXPLORER_VIEW_LIST)
    {
        return (Rectangle){
            contentArea.x,
            contentArea.y + visualIndex * ITEM_ALTURA - fileExplorer.scrollOffset,
            contentArea.width,
            (float)(ITEM_ALTURA - 4)};
    }

    int columns = GetExplorerGridColumns(contentArea);
    int row = visualIndex / columns;
    int column = visualIndex % columns;
    return (Rectangle){
        contentArea.x + column * (FILE_EXPLORER_CARD_LARGURA + FILE_EXPLORER_CARD_GAP),
        contentArea.y + row * (FILE_EXPLORER_CARD_ALTURA + FILE_EXPLORER_CARD_GAP) - fileExplorer.scrollOffset,
        FILE_EXPLORER_CARD_LARGURA,
        FILE_EXPLORER_CARD_ALTURA};
}

static Rectangle GetExplorerInteractiveItemRect(Rectangle contentArea, int visualIndex, float maxScroll)
{
    Rectangle rect = GetExplorerItemRect(contentArea, visualIndex);
    if (fileExplorer.viewMode == FILE_EXPLORER_VIEW_LIST)
    {
        rect.width -= 6.0f;
        if (maxScroll > 0.0f)
            rect.width -= 8.0f;
    }
    return rect;
}

static bool IsItemVisibleInContent(Rectangle itemRect, Rectangle contentArea)
{
    Rectangle clipped = {
        itemRect.x,
        itemRect.y,
        itemRect.width,
        itemRect.height};
    return CheckCollisionRecs(clipped, contentArea);
}

static bool HandleFileExplorerItemClick(const char *path)
{
    if (!path || path[0] == '\0')
        return false;

    bool isDirectory = !IsPathFile(path);
    bool directoryExists = isDirectory && DirectoryExists(path);

    if (directoryExists)
    {
        if (fileExplorer.modoProjetoAbrir)
        {
            char projectJson[MAX_FILEPATH_SIZE] = {0};
            if (EntryHasProjectFile(path, projectJson, sizeof(projectJson)))
            {
                OpenProject(projectJson);
                CloseFileExplorer();
                return true;
            }
        }

        if (TrySetCurrentPath(path))
            return true;
        return false;
    }

    if (fileExplorer.modoProjetoAbrir)
    {
        if (IsProjectJsonFile(path))
        {
            OpenProject(path);
            CloseFileExplorer();
            return true;
        }
        return false;
    }

    TextCopy(fileExplorer.caminhoSelecionado, path);
    fileExplorer.selectedPurpose = fileExplorer.pickPurpose;
    CloseFileExplorer();
    return true;
}

static void DrawExplorerScrollBar(Rectangle contentArea, int itemCount)
{
    float maxScroll = GetExplorerMaxScroll(itemCount, contentArea);
    if (maxScroll <= 0.0f)
        return;

    const UIStyle *style = GetUIStyle();
    float contentHeight = GetExplorerContentHeight(itemCount, contentArea);
    float ratio = contentArea.height / contentHeight;
    float thumbHeight = ClampFloat(contentArea.height * ratio, 28.0f, contentArea.height);
    float trackHeight = contentArea.height - thumbHeight;
    float scrollRatio = (maxScroll > 0.0f) ? (fileExplorer.scrollOffset / maxScroll) : 0.0f;
    Rectangle track = {contentArea.x + contentArea.width - 6.0f, contentArea.y + 2.0f, 4.0f, contentArea.height - 4.0f};
    Rectangle thumb = {track.x, track.y + trackHeight * scrollRatio, track.width, thumbHeight};

    DrawRectangleRounded(track, 1.0f, 4, style->panelBgAlt);
    DrawRectangleRounded(thumb, 1.0f, 4, style->accentSoft);
}

static Color FileMenuOptionHoverColor(void)
{
    return (Color){58, 26, 24, 255};
}

static Color FileMenuOptionBorderColor(void)
{
    return (Color){104, 56, 52, 255};
}

static void EnsureProjectsRootPath(char *out, size_t outSize)
{
    if (outSize == 0)
        return;
    out[0] = '\0';

    const char *cwd = GetWorkingDirectory();
    if (!cwd || cwd[0] == '\0')
        return;

    snprintf(out, outSize, "%s/projects", cwd);
    if (!DirectoryExists(out))
    {
#ifdef _WIN32
        _mkdir(out);
#else
        mkdir(out, 0755);
#endif
    }
}

static bool MatchFiltroExtensao(const char *caminho, int filtro)
{
    if (!IsPathFile(caminho))
        return true; // Diretórios sempre passam

    const char *ext = strrchr(caminho, '.');
    if (!ext)
        return false;

    ext++;

    switch (filtro)
    {
    case 1: // .obj
        return strcmp(ext, "obj") == 0 || strcmp(ext, "OBJ") == 0;
    case 2: // .glb
        return strcmp(ext, "glb") == 0 || strcmp(ext, "GLB") == 0;
    case 3: // .gltf
        return strcmp(ext, "gltf") == 0 || strcmp(ext, "GLTF") == 0;
    case 4: // .fbx
        return strcmp(ext, "fbx") == 0 || strcmp(ext, "FBY") == 0;
    case 5: // .ico / imagens
        return strcmp(ext, "ico") == 0 || strcmp(ext, "ICO") == 0 ||
               strcmp(ext, "png") == 0 || strcmp(ext, "PNG") == 0 ||
               strcmp(ext, "bmp") == 0 || strcmp(ext, "BMP") == 0 ||
               strcmp(ext, "jpg") == 0 || strcmp(ext, "JPG") == 0 ||
               strcmp(ext, "jpeg") == 0 || strcmp(ext, "JPEG") == 0;
    default:
        return true;
    }
}

static bool MatchBuscaTexto(const char *caminho, const char *texto)
{
    if (texto[0] == '\0')
        return true; // Se não há texto, aceita tudo

    const char *nome = GetFileName(caminho);

    // Busca case-insensitive
    char nomeMinusculo[256] = {0};
    char textoMinusculo[256] = {0};

    TextCopy(nomeMinusculo, nome);
    TextCopy(textoMinusculo, texto);

    // Converter para minúsculas
    for (int i = 0; nomeMinusculo[i]; i++)
        nomeMinusculo[i] = tolower(nomeMinusculo[i]);
    for (int i = 0; textoMinusculo[i]; i++)
        textoMinusculo[i] = tolower(textoMinusculo[i]);

    return strstr(nomeMinusculo, textoMinusculo) != NULL;
}

static void NormalizePathInput(char *path)
{
    if (!path)
        return;

    int len = (int)strlen(path);
    int start = 0;
    while (start < len && isspace((unsigned char)path[start]))
        start++;

    int end = len - 1;
    while (end >= start && isspace((unsigned char)path[end]))
        end--;

    if (start > 0 || end < len - 1)
    {
        int out = 0;
        for (int i = start; i <= end; i++)
            path[out++] = path[i];
        path[out] = '\0';
        len = out;
    }

    if (len >= 2 && path[0] == '"' && path[len - 1] == '"')
    {
        for (int i = 1; i < len - 1; i++)
            path[i - 1] = path[i];
        path[len - 2] = '\0';
    }
}

static bool TrySetCurrentPath(const char *newPath)
{
    if (!newPath || newPath[0] == '\0')
        return false;
    if (!DirectoryExists(newPath))
        return false;

    TextCopy(fileExplorer.caminhoAtual, newPath);
    TextCopy(fileExplorer.bufferCaminho, newPath);
    TextCopy(fileExplorer.bufferTexto, "");
    if (fileExplorer.arquivosCarregados)
        UnloadDirectoryFiles(fileExplorer.arquivos);
    fileExplorer.arquivos = LoadDirectoryFiles(fileExplorer.caminhoAtual);
    fileExplorer.arquivosCarregados = true;
    fileExplorer.scrollOffset = 0.0f;
    TextInputInit(&fileExplorer.inputBusca);
    fileExplorer.inputBusca.active = true;
    pathFeedbackInvalid = false;
    pathFeedbackUntil = 0.0;
    return true;
}

void InitFileExplorer(void)
{
    fileExplorer.aberto = false;
    fileExplorer.arquivosCarregados = false;
    fileExplorer.extensaoFiltro = 0;
    fileExplorer.modoEdicao = false;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.mostrarMenuFile = false;
    fileExplorer.mostrarSubmenuImport = false;
    fileExplorer.itemHoverFile = -1;
    fileExplorer.itemHoverImport = -1;
    fileExplorer.menuFileAbertoEsteFrame = false;
    fileExplorer.viewMode = FILE_EXPLORER_VIEW_THUMBNAILS;
    fileExplorer.scrollOffset = 0.0f;
    fileExplorer.pickPurpose = FILE_EXPLORER_PICK_NONE;
    fileExplorer.selectedPurpose = FILE_EXPLORER_PICK_NONE;
    fileExplorer.modoProjetoAbrir = false;
    fileExplorer.modoProjetoSalvar = false;
    TextCopy(fileExplorer.bufferNomeProjeto, "");
    TextCopy(fileExplorer.caminhoAtual, GetWorkingDirectory());
    TextCopy(fileExplorer.bufferCaminho, GetWorkingDirectory());
    TextCopy(fileExplorer.caminhoSelecionado, "");
    TextCopy(fileExplorer.bufferTexto, "");
    pathFeedbackInvalid = false;
    pathFeedbackUntil = 0.0;
    ClearFileExplorerPreviewCache();
    TextInputInit(&fileExplorer.inputNomeProjeto);
    TextInputInit(&fileExplorer.inputCaminho);
    TextInputInit(&fileExplorer.inputBusca);
}

void OpenFileExplorer(int filtro)
{
    fileExplorer.aberto = true;
    fileExplorer.modoEdicao = true;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.extensaoFiltro = filtro;
    fileExplorer.pickPurpose = FILE_EXPLORER_PICK_MODEL_IMPORT;
    TextCopy(fileExplorer.bufferTexto, "");
    TextCopy(fileExplorer.bufferCaminho, fileExplorer.caminhoAtual);
    fileExplorer.modoProjetoAbrir = false;
    fileExplorer.modoProjetoSalvar = false;
    fileExplorer.viewMode = FILE_EXPLORER_VIEW_THUMBNAILS;
    fileExplorer.scrollOffset = 0.0f;
    ClearFileExplorerPreviewCache();
    TextInputInit(&fileExplorer.inputCaminho);
    TextInputInit(&fileExplorer.inputBusca);
    fileExplorer.inputBusca.active = true;

    if (!fileExplorer.arquivosCarregados)
    {
        fileExplorer.arquivos = LoadDirectoryFiles(fileExplorer.caminhoAtual);
        fileExplorer.arquivosCarregados = true;
    }
}

void CloseFileExplorer(void)
{
    fileExplorer.aberto = false;
    fileExplorer.modoEdicao = false;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.modoProjetoAbrir = false;
    fileExplorer.modoProjetoSalvar = false;
    fileExplorer.pickPurpose = FILE_EXPLORER_PICK_NONE;
    TextCopy(fileExplorer.bufferTexto, "");
    TextCopy(fileExplorer.bufferCaminho, fileExplorer.caminhoAtual);
    fileExplorer.scrollOffset = 0.0f;
    pathFeedbackInvalid = false;
    pathFeedbackUntil = 0.0;
    ClearFileExplorerPreviewCache();
    TextInputInit(&fileExplorer.inputNomeProjeto);
    TextInputInit(&fileExplorer.inputCaminho);
    TextInputInit(&fileExplorer.inputBusca);
}

void OpenProjectExplorer(void)
{
    fileExplorer.aberto = true;
    fileExplorer.modoEdicao = true;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.extensaoFiltro = 0;
    fileExplorer.pickPurpose = FILE_EXPLORER_PICK_NONE;
    TextCopy(fileExplorer.bufferTexto, "");
    fileExplorer.modoProjetoAbrir = true;
    fileExplorer.modoProjetoSalvar = false;
    fileExplorer.viewMode = FILE_EXPLORER_VIEW_THUMBNAILS;
    fileExplorer.scrollOffset = 0.0f;
    ClearFileExplorerPreviewCache();
    TextInputInit(&fileExplorer.inputCaminho);
    TextInputInit(&fileExplorer.inputBusca);
    fileExplorer.inputBusca.active = true;

    char projectsPath[MAX_FILEPATH_SIZE] = {0};
    EnsureProjectsRootPath(projectsPath, sizeof(projectsPath));
    if (projectsPath[0] != '\0')
        TextCopy(fileExplorer.caminhoAtual, projectsPath);
    TextCopy(fileExplorer.bufferCaminho, fileExplorer.caminhoAtual);
    pathFeedbackInvalid = false;
    pathFeedbackUntil = 0.0;

    if (fileExplorer.arquivosCarregados)
        UnloadDirectoryFiles(fileExplorer.arquivos);
    fileExplorer.arquivos = LoadDirectoryFiles(fileExplorer.caminhoAtual);
    fileExplorer.arquivosCarregados = true;
}

void OpenProjectSaveAs(void)
{
    fileExplorer.aberto = true;
    fileExplorer.modoEdicao = false;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.modoProjetoAbrir = false;
    fileExplorer.modoProjetoSalvar = true;
    fileExplorer.pickPurpose = FILE_EXPLORER_PICK_NONE;
    TextCopy(fileExplorer.bufferNomeProjeto, "");
    fileExplorer.scrollOffset = 0.0f;
    TextInputInit(&fileExplorer.inputNomeProjeto);
    fileExplorer.inputNomeProjeto.active = true;
}

void OpenExportIconExplorer(void)
{
    fileExplorer.aberto = true;
    fileExplorer.modoEdicao = true;
    fileExplorer.modoEdicaoCaminho = false;
    fileExplorer.extensaoFiltro = 5;
    fileExplorer.pickPurpose = FILE_EXPLORER_PICK_EXPORT_ICON;
    TextCopy(fileExplorer.bufferTexto, "");
    fileExplorer.modoProjetoAbrir = false;
    fileExplorer.modoProjetoSalvar = false;
    fileExplorer.viewMode = FILE_EXPLORER_VIEW_THUMBNAILS;
    fileExplorer.scrollOffset = 0.0f;
    ClearFileExplorerPreviewCache();
    TextInputInit(&fileExplorer.inputCaminho);
    TextInputInit(&fileExplorer.inputBusca);
    fileExplorer.inputBusca.active = true;

    const char *projectDir = GetProjectDir();
    if (projectDir && projectDir[0] != '\0')
        TextCopy(fileExplorer.caminhoAtual, projectDir);
    else
        TextCopy(fileExplorer.caminhoAtual, GetWorkingDirectory());
    TextCopy(fileExplorer.bufferCaminho, fileExplorer.caminhoAtual);
    pathFeedbackInvalid = false;
    pathFeedbackUntil = 0.0;

    if (fileExplorer.arquivosCarregados)
        UnloadDirectoryFiles(fileExplorer.arquivos);
    fileExplorer.arquivos = LoadDirectoryFiles(fileExplorer.caminhoAtual);
    fileExplorer.arquivosCarregados = true;
}

void UpdateFileExplorer(void)
{
    if (!fileExplorer.aberto)
        return;

    if (fileExplorer.modoProjetoSalvar)
    {
        // Modal simples de Save As
        Vector2 mouse = GetMousePosition();
        int larguraTela = GetScreenWidth();
        int alturaTela = GetScreenHeight();
        Rectangle painel = {(float)(larguraTela / 2 - 200), (float)(alturaTela / 2 - 90), 400, 180};
        Rectangle btnSalvar = {painel.x + 20, painel.y + 110, 120, 28};
        Rectangle btnCancelar = {painel.x + 160, painel.y + 110, 120, 28};

        if (!CheckCollisionPointRec(mouse, painel) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            CloseFileExplorer();
            return;
        }

        UIButtonState saveState = UIButtonGetState(btnSalvar);
        UIButtonState cancelState = UIButtonGetState(btnCancelar);

        if (saveState.clicked)
        {
            if (fileExplorer.bufferNomeProjeto[0] != '\0')
                SaveProjectAs(fileExplorer.bufferNomeProjeto);
            CloseFileExplorer();
            return;
        }

        if (cancelState.clicked)
        {
            CloseFileExplorer();
            return;
        }

        return;
    }

    FileExplorerLayout layout = GetFileExplorerLayout();
    Vector2 mouse = GetMousePosition();

    // Fechar ao clicar fora do painel
    if (!CheckCollisionPointRec(mouse, layout.panel) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        CloseFileExplorer();
        return;
    }

    UIButtonState backState = UIButtonGetState(layout.backButton);
    if (backState.clicked)
    {
        char caminhoAnterior[MAX_FILEPATH_SIZE] = {0};
        TextCopy(caminhoAnterior, GetPrevDirectoryPath(fileExplorer.caminhoAtual));
        TrySetCurrentPath(caminhoAnterior);
        return;
    }

    UIButtonState listState = UIButtonGetState(layout.listButton);
    UIButtonState thumbState = UIButtonGetState(layout.thumbButton);
    if (listState.clicked)
    {
        fileExplorer.viewMode = FILE_EXPLORER_VIEW_LIST;
        fileExplorer.scrollOffset = 0.0f;
    }
    else if (thumbState.clicked)
    {
        fileExplorer.viewMode = FILE_EXPLORER_VIEW_THUMBNAILS;
        fileExplorer.scrollOffset = 0.0f;
    }

    Rectangle contentInnerArea = GetExplorerContentInnerArea(layout.contentArea);
    int *visibleIndices = NULL;
    int visibleCount = BuildVisibleEntryIndexList(&visibleIndices);
    float maxScroll = GetExplorerMaxScroll(visibleCount, contentInnerArea);
    fileExplorer.scrollOffset = ClampFloat(fileExplorer.scrollOffset, 0.0f, maxScroll);

    if (CheckCollisionPointRec(mouse, layout.contentArea))
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            float scrollStep = (fileExplorer.viewMode == FILE_EXPLORER_VIEW_THUMBNAILS) ? 84.0f : (float)ITEM_ALTURA;
            fileExplorer.scrollOffset -= wheel * scrollStep;
            fileExplorer.scrollOffset = ClampFloat(fileExplorer.scrollOffset, 0.0f, maxScroll);
        }
    }

    for (int i = 0; i < visibleCount; i++)
    {
        Rectangle areaItem = GetExplorerInteractiveItemRect(contentInnerArea, i, maxScroll);
        if (areaItem.y + areaItem.height < contentInnerArea.y)
            continue;
        if (areaItem.y > contentInnerArea.y + contentInnerArea.height)
            break;

        bool hover = CheckCollisionPointRec(mouse, areaItem);
        if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            const char *path = fileExplorer.arquivos.paths[visibleIndices[i]];
            bool handled = HandleFileExplorerItemClick(path);
            MemFree(visibleIndices);
            if (handled)
                return;
        }
    }

    if (visibleIndices)
        MemFree(visibleIndices);
}

void DrawFileExplorer(void)
{
    if (!fileExplorer.aberto)
        return;

    if (fileExplorer.modoProjetoSalvar)
    {
        int larguraTela = GetScreenWidth();
        int alturaTela = GetScreenHeight();

        DrawRectangle(0, 0, larguraTela, alturaTela, GetUIStyle()->panelOverlay);

        Rectangle painel = {(float)(larguraTela / 2 - 200), (float)(alturaTela / 2 - 90), 400, 180};
        DrawRectangleRec(painel, COR_PAINEL);
        DrawRectangle((int)painel.x + 1, (int)painel.y + 1, 4, (int)painel.height - 2, GetUIStyle()->accent);

        DrawText("Save Project As", (int)painel.x + 24, (int)painel.y + 20, 14, COR_TEXTO);
        DrawText("Nome do projeto:", (int)painel.x + 20, (int)painel.y + 45, 10, COR_TEXTO_SUAVE);

        TextInputConfig cfg = {0};
        cfg.fontSize = 12;
        cfg.padding = 6;
        cfg.textColor = COR_TEXTO;
        cfg.bgColor = COR_FUNDO;
        cfg.borderColor = COR_BORDA;
        cfg.selectionColor = GetUIStyle()->inputSelection;
        cfg.caretColor = GetUIStyle()->caret;
        cfg.filter = TEXT_INPUT_FILTER_NONE;
        cfg.allowInput = true;

        Rectangle nomeBox = {painel.x + 20, painel.y + 60, painel.width - 40, 28};
        int flags = TextInputDraw(nomeBox, fileExplorer.bufferNomeProjeto,
                                  (int)sizeof(fileExplorer.bufferNomeProjeto),
                                  &fileExplorer.inputNomeProjeto, &cfg);
        if ((flags & TEXT_INPUT_SUBMITTED) && fileExplorer.bufferNomeProjeto[0] != '\0')
        {
            SaveProjectAs(fileExplorer.bufferNomeProjeto);
            CloseFileExplorer();
            return;
        }

        Rectangle btnSalvar = {painel.x + 20, painel.y + 110, 120, 28};
        Rectangle btnCancelar = {painel.x + 160, painel.y + 110, 120, 28};

        Vector2 mouse = GetMousePosition();
        bool hoverSalvar = CheckCollisionPointRec(mouse, btnSalvar);
        bool hoverCancelar = CheckCollisionPointRec(mouse, btnCancelar);

        const UIStyle *style = GetUIStyle();
        UIButtonConfig bwCfg = {0};
        bwCfg.centerText = true;
        bwCfg.fontSize = 12;
        bwCfg.padding = 6;
        bwCfg.textColor = style->buttonText;
        bwCfg.textHoverColor = style->buttonTextHover;
        bwCfg.bgColor = style->buttonBg;
        bwCfg.bgHoverColor = style->buttonBgHover;
        bwCfg.borderColor = style->buttonBorder;
        bwCfg.borderHoverColor = style->buttonBorder;
        bwCfg.borderThickness = 1.0f;

        UIButtonDraw(btnSalvar, "Salvar", nullptr, &bwCfg, hoverSalvar);
        UIButtonDraw(btnCancelar, "Cancelar", nullptr, &bwCfg, hoverCancelar);
        return;
    }

    FileExplorerLayout layout = GetFileExplorerLayout();
    const UIStyle *style = GetUIStyle();
    Vector2 mouse = GetMousePosition();

    // Overlay escuro
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), style->panelOverlay);

    // Barra superior do explorador
    DrawRectangleRec(layout.panel, COR_PAINEL);
    DrawRectangle((int)layout.panel.x + 1, (int)layout.panel.y + 1, 4, (int)layout.panel.height - 2, style->accent);
    DrawRectangle((int)layout.panel.x, (int)layout.panel.y, (int)layout.panel.width, BARRA_SUPERIOR, style->panelBgAlt);
    const char *title = fileExplorer.modoProjetoAbrir ? "Open Project" : "Import Model";
    if (fileExplorer.pickPurpose == FILE_EXPLORER_PICK_EXPORT_ICON)
        title = "Escolher Icone do Executavel";
    DrawText(title, (int)layout.panel.x + 16, (int)layout.panel.y + 5, 13, style->accent);
    DrawText("PATH", (int)layout.panel.x + 16, (int)layout.panel.y + 24, 10, COR_TEXTO_SUAVE);

    TextInputConfig cfg = {0};
    cfg.fontSize = 12;
    cfg.padding = 6;
    cfg.textColor = COR_TEXTO;
    cfg.bgColor = style->inputBg;
    cfg.borderColor = (pathFeedbackInvalid && GetTime() < pathFeedbackUntil) ? (Color){150, 46, 42, 255} : COR_BORDA;
    cfg.selectionColor = style->inputSelection;
    cfg.caretColor = style->caret;
    cfg.filter = TEXT_INPUT_FILTER_NONE;
    cfg.allowInput = true;

    int pathFlags = TextInputDraw(layout.pathBox, fileExplorer.bufferCaminho,
                                  MAX_FILEPATH_SIZE, &fileExplorer.inputCaminho, &cfg);
    if (pathFlags & TEXT_INPUT_SUBMITTED)
    {
        char pathParsed[MAX_FILEPATH_SIZE] = {0};
        TextCopy(pathParsed, fileExplorer.bufferCaminho);
        NormalizePathInput(pathParsed);
        TextCopy(fileExplorer.bufferCaminho, pathParsed);
        if (!TrySetCurrentPath(pathParsed))
        {
            pathFeedbackInvalid = true;
            pathFeedbackUntil = GetTime() + 2.0;
            fileExplorer.inputCaminho.active = true;
        }
    }
    if (pathFeedbackInvalid && GetTime() >= pathFeedbackUntil)
        pathFeedbackInvalid = false;

    bool hoverVoltar = CheckCollisionPointRec(mouse, layout.backButton);
    UIButtonConfig backCfg = {0};
    backCfg.centerText = true;
    backCfg.fontSize = 12;
    backCfg.padding = 4;
    backCfg.textColor = style->buttonText;
    backCfg.textHoverColor = style->buttonTextHover;
    backCfg.bgColor = style->buttonBg;
    backCfg.bgHoverColor = style->buttonBgHover;
    backCfg.borderColor = style->buttonBorder;
    backCfg.borderHoverColor = style->buttonBorder;
    backCfg.borderThickness = 1.0f;
    UIButtonDraw(layout.backButton, "<", nullptr, &backCfg, hoverVoltar);

    if (pathFeedbackInvalid)
        DrawText("PATH invalido ou inexistente", (int)layout.pathBox.x, (int)layout.panel.y + 48, 10, (Color){186, 56, 52, 255});

    TextInputDraw(layout.searchBox, fileExplorer.bufferTexto,
                  MAX_BUFFER_TEXTO, &fileExplorer.inputBusca, &cfg);

    DrawText("BUSCAR", (int)layout.panel.x + 16, (int)layout.panel.y + BARRA_SUPERIOR - 8, 10, COR_TEXTO_SUAVE);

    UIButtonConfig toggleCfg = {0};
    toggleCfg.centerText = true;
    toggleCfg.fontSize = 11;
    toggleCfg.padding = 4;
    toggleCfg.textColor = style->buttonText;
    toggleCfg.textHoverColor = style->buttonTextHover;
    toggleCfg.bgColor = style->buttonBg;
    toggleCfg.bgHoverColor = style->buttonBgHover;
    toggleCfg.borderColor = style->buttonBorder;
    toggleCfg.borderHoverColor = style->buttonBorder;
    toggleCfg.borderThickness = 1.0f;

    bool listActive = fileExplorer.viewMode == FILE_EXPLORER_VIEW_LIST;
    bool thumbActive = fileExplorer.viewMode == FILE_EXPLORER_VIEW_THUMBNAILS;
    UIButtonConfig listCfg = toggleCfg;
    UIButtonConfig thumbCfg = toggleCfg;
    if (listActive)
    {
        listCfg.bgColor = style->accentSoft;
        listCfg.bgHoverColor = style->accentSoft;
        listCfg.textColor = style->buttonTextHover;
        listCfg.textHoverColor = style->buttonTextHover;
        listCfg.borderColor = style->accent;
        listCfg.borderHoverColor = style->accent;
    }
    if (thumbActive)
    {
        thumbCfg.bgColor = style->accentSoft;
        thumbCfg.bgHoverColor = style->accentSoft;
        thumbCfg.textColor = style->buttonTextHover;
        thumbCfg.textHoverColor = style->buttonTextHover;
        thumbCfg.borderColor = style->accent;
        thumbCfg.borderHoverColor = style->accent;
    }

    UIButtonDraw(layout.listButton, "Lista", nullptr, &listCfg, CheckCollisionPointRec(mouse, layout.listButton) || listActive);
    UIButtonDraw(layout.thumbButton, "Cards", nullptr, &thumbCfg, CheckCollisionPointRec(mouse, layout.thumbButton) || thumbActive);

    DrawRectangleRec(layout.contentArea, style->panelBgAlt);
    DrawRectangleLinesEx(layout.contentArea, 1.0f, style->panelBorderSoft);

    Rectangle contentInnerArea = GetExplorerContentInnerArea(layout.contentArea);
    int *visibleIndices = NULL;
    int visibleCount = BuildVisibleEntryIndexList(&visibleIndices);
    float maxScroll = GetExplorerMaxScroll(visibleCount, contentInnerArea);
    fileExplorer.scrollOffset = ClampFloat(fileExplorer.scrollOffset, 0.0f, maxScroll);

    for (int i = 0; i < visibleCount; i++)
    {
        Rectangle areaItem = GetExplorerInteractiveItemRect(contentInnerArea, i, maxScroll);
        if (areaItem.y + areaItem.height < contentInnerArea.y)
            continue;
        if (areaItem.y > contentInnerArea.y + contentInnerArea.height)
            break;

        const char *path = fileExplorer.arquivos.paths[visibleIndices[i]];
        FileExplorerEntryType entryType = GetFileExplorerEntryType(path);
        (void)GetFileExplorerPreview(path, entryType);
    }

    BeginScissorMode((int)contentInnerArea.x, (int)contentInnerArea.y, (int)contentInnerArea.width, (int)contentInnerArea.height);
    if (visibleCount <= 0)
    {
        DrawText("Nenhum item encontrado para esse filtro.", (int)contentInnerArea.x + 8, (int)contentInnerArea.y + 8, 12, style->textSecondary);
    }
    else
    {
        for (int i = 0; i < visibleCount; i++)
        {
            Rectangle areaItem = GetExplorerInteractiveItemRect(contentInnerArea, i, maxScroll);
            if (!IsItemVisibleInContent(areaItem, contentInnerArea))
                continue;

            const char *path = fileExplorer.arquivos.paths[visibleIndices[i]];
            const char *nome = GetFileName(path);
            FileExplorerEntryType entryType = GetFileExplorerEntryType(path);
            bool hover = CheckCollisionPointRec(mouse, areaItem);
            FileExplorerPreviewCacheEntry *preview = GetFileExplorerPreview(path, entryType);

            if (fileExplorer.viewMode == FILE_EXPLORER_VIEW_LIST)
            {
                Color fundoItem = hover ? style->accentSoft : style->itemBg;
                DrawRectangleRec(areaItem, fundoItem);
                if (!hover)
                    DrawLine((int)areaItem.x + 6, (int)(areaItem.y + areaItem.height), (int)(areaItem.x + areaItem.width - 6), (int)(areaItem.y + areaItem.height), style->panelBorderSoft);

                Color corTexto = hover ? style->buttonTextHover : COR_TEXTO;
                if (entryType == FILE_EXPLORER_ENTRY_FOLDER || entryType == FILE_EXPLORER_ENTRY_PROJECT)
                {
                    DrawRectangle((int)areaItem.x + 4, (int)areaItem.y + 6, 3, (int)areaItem.height - 12, hover ? style->accent : style->accentSoft);
                    if (!hover)
                        corTexto = COR_DESTAQUE;
                }

                char fittedName[256] = {0};
                FitTextToWidth(nome, fittedName, sizeof(fittedName), 12, areaItem.width - 26.0f);
                DrawText(fittedName, (int)(areaItem.x + 14.0f), (int)(areaItem.y + 10.0f), 12, corTexto);
            }
            else
            {
                Color cardBg = hover ? LerpColor(style->itemBg, style->itemHover, 0.9f) : style->itemBg;
                Color border = hover ? LerpColor(style->panelBorderSoft, style->buttonBorder, 0.7f) : style->panelBorderSoft;
                Rectangle previewBounds = {areaItem.x + 10.0f, areaItem.y + 10.0f, areaItem.width - 20.0f, areaItem.height - 58.0f};

                DrawRectangleRec(areaItem, cardBg);
                DrawRectangleLinesEx(areaItem, 1.0f, border);

                if (preview && preview->valid)
                {
                    DrawRectangleRec(previewBounds, style->panelBg);
                    DrawPreviewTextureFit(preview, previewBounds, WHITE);
                }
                else
                {
                    DrawFileExplorerPlaceholder(previewBounds, entryType, hover);
                }

                char fittedName[128] = {0};
                char fittedType[64] = {0};
                FitTextToWidth(nome, fittedName, sizeof(fittedName), 12, areaItem.width - 16.0f);
                FitTextToWidth(GetFileExplorerEntryTypeLabel(entryType), fittedType, sizeof(fittedType), 10, areaItem.width - 16.0f);
                DrawText(fittedName, (int)(areaItem.x + 8.0f), (int)(areaItem.y + areaItem.height - 34.0f), 12, hover ? style->buttonTextHover : style->textPrimary);
                DrawText(fittedType, (int)(areaItem.x + 8.0f), (int)(areaItem.y + areaItem.height - 18.0f), 10, hover ? style->textSecondary : style->textMuted);
            }
        }
    }
    EndScissorMode();

    DrawExplorerScrollBar(contentInnerArea, visibleCount);

    if (visibleIndices)
        MemFree(visibleIndices);
}

bool FileExplorerArquivoSelecionado(char *saida)
{
    if (fileExplorer.selectedPurpose != FILE_EXPLORER_PICK_MODEL_IMPORT)
        return false;

    if (fileExplorer.caminhoSelecionado[0] != '\0')
    {
        TextCopy(saida, fileExplorer.caminhoSelecionado);
        TextCopy(fileExplorer.caminhoSelecionado, "");
        fileExplorer.selectedPurpose = FILE_EXPLORER_PICK_NONE;
        return true;
    }
    return false;
}

bool FileExplorerConsumeSelectedExportIconPath(char *saida)
{
    if (fileExplorer.selectedPurpose != FILE_EXPLORER_PICK_EXPORT_ICON)
        return false;

    if (fileExplorer.caminhoSelecionado[0] != '\0')
    {
        TextCopy(saida, fileExplorer.caminhoSelecionado);
        TextCopy(fileExplorer.caminhoSelecionado, "");
        fileExplorer.selectedPurpose = FILE_EXPLORER_PICK_NONE;
        return true;
    }
    return false;
}

void UnloadFileExplorer(void)
{
    if (fileExplorer.arquivosCarregados)
    {
        UnloadDirectoryFiles(fileExplorer.arquivos);
        fileExplorer.arquivosCarregados = false;
    }
    ClearFileExplorerPreviewCache();
}

void ToggleFileMenu(void)
{
    fileExplorer.mostrarMenuFile = !fileExplorer.mostrarMenuFile;
    fileExplorer.menuFileAbertoEsteFrame = fileExplorer.mostrarMenuFile;
}

bool IsFileMenuOpen(void)
{
    return fileExplorer.mostrarMenuFile;
}

void UpdateFileMenu(void)
{
    if (!fileExplorer.mostrarMenuFile)
        return;

    Vector2 mouse = GetMousePosition();

    Rectangle fileButtonRect = {(float)PAINEL_LARGURA + 4.0f, 2.0f, 64.0f, 20.0f};

    // Menu principal File
    const float menuX = (float)PAINEL_LARGURA + 8.0f;
    const float menuY = 24.0f;
    Rectangle menuFileRect = {menuX, menuY, 200.0f, 96.0f};

    // Itens do menu
    Rectangle itemImport = {menuFileRect.x, menuFileRect.y, menuFileRect.width, 24.0f};
    Rectangle itemOpen = {menuFileRect.x, menuFileRect.y + 24.0f, menuFileRect.width, 24.0f};
    Rectangle itemSave = {menuFileRect.x, menuFileRect.y + 48.0f, menuFileRect.width, 24.0f};
    Rectangle itemExport = {menuFileRect.x, menuFileRect.y + 72.0f, menuFileRect.width, 24.0f};

    // Área do submenu
    const float submenuItemH = 24.0f;
    const float submenuCount = 4.0f;
    Rectangle submenuRect = {menuFileRect.x + menuFileRect.width + 2, menuFileRect.y, 150.0f, submenuItemH * submenuCount};
    bool mouseEmSubmenu = CheckCollisionPointRec(mouse, submenuRect);

    Rectangle keepOpenZone = {fileButtonRect.x - 8.0f,
                              fileButtonRect.y - 6.0f,
                              (submenuRect.x + submenuRect.width) - (fileButtonRect.x - 8.0f) + 8.0f,
                              (menuFileRect.y + menuFileRect.height) - (fileButtonRect.y - 6.0f) + 8.0f};
    if (!CheckCollisionPointRec(mouse, keepOpenZone))
    {
        fileExplorer.mostrarMenuFile = false;
        fileExplorer.mostrarSubmenuImport = false;
        fileExplorer.itemHoverFile = -1;
        fileExplorer.itemHoverImport = -1;
        fileExplorer.menuFileAbertoEsteFrame = false;
        return;
    }

    // Verificar hover no menu principal
    fileExplorer.itemHoverFile = -1;
    if (CheckCollisionPointRec(mouse, itemImport) || mouseEmSubmenu)
    {
        fileExplorer.itemHoverFile = 0;
        fileExplorer.mostrarSubmenuImport = true;
    }
    else if (CheckCollisionPointRec(mouse, itemOpen))
    {
        fileExplorer.itemHoverFile = 1;
        fileExplorer.mostrarSubmenuImport = false;
    }
    else if (CheckCollisionPointRec(mouse, itemSave))
    {
        fileExplorer.itemHoverFile = 2;
        fileExplorer.mostrarSubmenuImport = false;
    }
    else if (CheckCollisionPointRec(mouse, itemExport))
    {
        fileExplorer.itemHoverFile = 3;
        fileExplorer.mostrarSubmenuImport = false;
    }
    else if (!CheckCollisionPointRec(mouse, menuFileRect))
    {
        fileExplorer.mostrarSubmenuImport = false;
    }

    // Submenu
    if (fileExplorer.mostrarSubmenuImport && fileExplorer.itemHoverFile == 0)
    {
        Rectangle itemObj = {submenuRect.x, submenuRect.y, submenuRect.width, 24.0f};
        Rectangle itemGlb = {submenuRect.x, submenuRect.y + 24.0f, submenuRect.width, 24.0f};
        Rectangle itemGltf = {submenuRect.x, submenuRect.y + 48.0f, submenuRect.width, 24.0f};
        Rectangle itemFbx = {submenuRect.x, submenuRect.y + 72.0f, submenuRect.width, 24.0f};

        fileExplorer.itemHoverImport = -1;
        if (CheckCollisionPointRec(mouse, itemObj))
            fileExplorer.itemHoverImport = 0;
        else if (CheckCollisionPointRec(mouse, itemGlb))
            fileExplorer.itemHoverImport = 1;
        else if (CheckCollisionPointRec(mouse, itemGltf))
            fileExplorer.itemHoverImport = 2;
        else if (CheckCollisionPointRec(mouse, itemFbx))
            fileExplorer.itemHoverImport = 3;

        // Processar cliques no submenu
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !fileExplorer.menuFileAbertoEsteFrame)
        {
            if (fileExplorer.itemHoverImport >= 0 && fileExplorer.itemHoverImport <= 3)
            {
                OpenFileExplorer(fileExplorer.itemHoverImport + 1);
                fileExplorer.mostrarMenuFile = false;
                fileExplorer.mostrarSubmenuImport = false;
            }
        }
    }

    // Processar cliques no menu principal
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !fileExplorer.menuFileAbertoEsteFrame)
    {
        if (fileExplorer.itemHoverFile == 1)
        {
            OpenProjectExplorer();
            fileExplorer.mostrarMenuFile = false;
        }
        else if (fileExplorer.itemHoverFile == 2)
        {
            if (!SaveProject())
                OpenProjectSaveAs();
            fileExplorer.mostrarMenuFile = false;
        }
        else if (fileExplorer.itemHoverFile == 3)
        {
            OpenExportDialog();
            fileExplorer.mostrarMenuFile = false;
        }
        else if (!CheckCollisionPointRec(mouse, menuFileRect))
        {
            fileExplorer.mostrarMenuFile = false;
            fileExplorer.mostrarSubmenuImport = false;
        }
    }

    fileExplorer.menuFileAbertoEsteFrame = false;
}

void DrawFileMenu(void)
{
    if (!fileExplorer.mostrarMenuFile)
        return;

    const float menuX = (float)PAINEL_LARGURA + 8.0f;
    const float menuY = 24.0f;
    const UIStyle *style = GetUIStyle();

    // Menu principal File
    Rectangle menuFileRect = {menuX, menuY, 200.0f, 96.0f};
    DrawRectangleRec(menuFileRect, style->panelBg);

    // Itens do menu
    Rectangle itemImport = {menuFileRect.x, menuFileRect.y, menuFileRect.width, 24.0f};
    Rectangle itemOpen = {menuFileRect.x, menuFileRect.y + 24.0f, menuFileRect.width, 24.0f};
    Rectangle itemSave = {menuFileRect.x, menuFileRect.y + 48.0f, menuFileRect.width, 24.0f};
    Rectangle itemExport = {menuFileRect.x, menuFileRect.y + 72.0f, menuFileRect.width, 24.0f};

    // Área do submenu
    const float submenuItemH = 24.0f;
    const float submenuCount = 4.0f;
    Rectangle submenuRect = {menuFileRect.x + menuFileRect.width + 2, menuFileRect.y, 150.0f, submenuItemH * submenuCount};

    // Desenhar itens
    bool hoverImport = fileExplorer.itemHoverFile == 0;
    bool hoverOpen = fileExplorer.itemHoverFile == 1;
    bool hoverSave = fileExplorer.itemHoverFile == 2;
    bool hoverExport = fileExplorer.itemHoverFile == 3;

    DrawRectangleRec(itemImport, hoverImport ? FileMenuOptionHoverColor() : style->buttonBg);
    DrawRectangleRec(itemOpen, hoverOpen ? FileMenuOptionHoverColor() : style->buttonBg);
    DrawRectangleRec(itemSave, hoverSave ? FileMenuOptionHoverColor() : style->buttonBg);
    DrawRectangleRec(itemExport, hoverExport ? FileMenuOptionHoverColor() : style->buttonBg);
    if (hoverImport)
        DrawRectangleLinesEx(itemImport, 1.0f, FileMenuOptionBorderColor());
    if (hoverOpen)
        DrawRectangleLinesEx(itemOpen, 1.0f, FileMenuOptionBorderColor());
    if (hoverSave)
        DrawRectangleLinesEx(itemSave, 1.0f, FileMenuOptionBorderColor());
    if (hoverExport)
        DrawRectangleLinesEx(itemExport, 1.0f, FileMenuOptionBorderColor());

    DrawText("Import Model", (int)(itemImport.x + 10), (int)(itemImport.y + 6), 12, hoverImport ? style->buttonTextHover : style->buttonText);
    DrawText("Open Project", (int)(itemOpen.x + 10), (int)(itemOpen.y + 6), 12, hoverOpen ? style->buttonTextHover : style->buttonText);
    DrawText("Save Project", (int)(itemSave.x + 10), (int)(itemSave.y + 6), 12, hoverSave ? style->buttonTextHover : style->buttonText);
    DrawText("Configurar Build", (int)(itemExport.x + 10), (int)(itemExport.y + 6), 12, hoverExport ? style->buttonTextHover : style->buttonText);

    // Seta para indicar submenu
    DrawText(">", (int)(itemImport.x + menuFileRect.width - 20), (int)(itemImport.y + 6), 12, hoverImport ? style->buttonTextHover : style->buttonText);

    // SUBMENU DE IMPORT
    if (fileExplorer.mostrarSubmenuImport && fileExplorer.itemHoverFile == 0)
    {
        DrawRectangleRec(submenuRect, style->buttonBg);

        Rectangle itemObj = {submenuRect.x, submenuRect.y, submenuRect.width, submenuItemH};
        Rectangle itemGlb = {submenuRect.x, submenuRect.y + submenuItemH, submenuRect.width, submenuItemH};
        Rectangle itemGltf = {submenuRect.x, submenuRect.y + submenuItemH * 2.0f, submenuRect.width, submenuItemH};
        Rectangle itemFbx = {submenuRect.x, submenuRect.y + submenuItemH * 3.0f, submenuRect.width, submenuItemH};

        bool hoverObj = fileExplorer.itemHoverImport == 0;
        bool hoverGlb = fileExplorer.itemHoverImport == 1;
        bool hoverGltf = fileExplorer.itemHoverImport == 2;
        bool hoverFbx = fileExplorer.itemHoverImport == 3;

        DrawRectangleRec(itemObj, hoverObj ? FileMenuOptionHoverColor() : style->buttonBg);
        DrawRectangleRec(itemGlb, hoverGlb ? FileMenuOptionHoverColor() : style->buttonBg);
        DrawRectangleRec(itemGltf, hoverGltf ? FileMenuOptionHoverColor() : style->buttonBg);
        DrawRectangleRec(itemFbx, hoverFbx ? FileMenuOptionHoverColor() : style->buttonBg);
        if (hoverObj)
            DrawRectangleLinesEx(itemObj, 1.0f, FileMenuOptionBorderColor());
        if (hoverGlb)
            DrawRectangleLinesEx(itemGlb, 1.0f, FileMenuOptionBorderColor());
        if (hoverGltf)
            DrawRectangleLinesEx(itemGltf, 1.0f, FileMenuOptionBorderColor());
        if (hoverFbx)
            DrawRectangleLinesEx(itemFbx, 1.0f, FileMenuOptionBorderColor());

        DrawText(".OBJ (Wavefront)", (int)(itemObj.x + 10), (int)(itemObj.y + 6), 11, hoverObj ? style->buttonTextHover : style->buttonText);
        DrawText(".GLB (glTF Binary)", (int)(itemGlb.x + 10), (int)(itemGlb.y + 6), 11, hoverGlb ? style->buttonTextHover : style->buttonText);
        DrawText(".GLTF (glTF Text)", (int)(itemGltf.x + 10), (int)(itemGltf.y + 6), 11, hoverGltf ? style->buttonTextHover : style->buttonText);
        DrawText(".FBX (Autodesk)", (int)(itemFbx.x + 10), (int)(itemFbx.y + 6), 11, hoverFbx ? style->buttonTextHover : style->buttonText);
    }
}




