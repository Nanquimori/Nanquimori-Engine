#include "properties_panel.h"
#include "app/application.h"
#include "scene/scene_manager.h"
#include "scene/scene_camera.h"
#include "editor/ui/color_picker.h"
#include "editor/ui/drag_float_input.h"
#include "editor/ui/text_input.h"
#include "raymath.h"
#include "editor/ui/ui_style.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Paleta baseada no UIStyle
#define COR_PAINEL (GetUIStyle()->panelBg)
#define COR_BORDA (GetUIStyle()->panelBorder)
#define COR_TEXTO (GetUIStyle()->textPrimary)
#define COR_ITEM (GetUIStyle()->itemHover)
#define COR_ITEM_SEL (GetUIStyle()->itemActive)
#define COR_EDIT_BG (GetUIStyle()->inputBg)
#define COR_TEXTO_SECUNDARIO (GetUIStyle()->textSecondary)

static bool propertiesObjectExpanded = true;
static bool propertiesTransformExpanded = true;
static bool propertiesCameraExpanded = true;
static bool propertiesPhysicsExpanded = true;
static bool propertiesPrototypeExpanded = true;

static bool CtrlHeld(void)
{
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}

static bool DrawSectionHeader(const char *title, int x, int *y, bool *expanded, bool allowInput, Vector2 mouse)
{
    Rectangle header = {(float)(x + 8), (float)(*y), (float)(PROPERTIES_PAINEL_LARGURA - 16), 18.0f};
    bool hover = CheckCollisionPointRec(mouse, header);
    bool clicked = allowInput && hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    if (clicked)
        *expanded = !(*expanded);
    DrawEditorCollapsibleHeader(header, title, 12, *expanded, hover);
    *y += 22;
    return clicked;
}

static void FocusPropertiesSection(bool *expandedSection)
{
    propertiesTransformExpanded = (expandedSection == &propertiesTransformExpanded);
    propertiesCameraExpanded = (expandedSection == &propertiesCameraExpanded);
    propertiesPhysicsExpanded = (expandedSection == &propertiesPhysicsExpanded);
    propertiesPrototypeExpanded = (expandedSection == &propertiesPrototypeExpanded);
}

typedef struct
{
    int id;
    bool isStatic;
    bool rigidbody;
    bool collider;
    bool gravity;
    float mass;
    int shapeType;
    Vector3 shapeSize;
    bool terrain;
} PhysEntry;

static bool fisicaInit = false;
static PhysEntry physEntries[MAX_OBJETOS] = {0};
static int physEntryCount = 0;

static ColorPickerState protoPicker = {0};
static int protoPickerTarget = 0;
static int protoPickerObjectId = -1;
static float propertiesScroll = 0.0f;
static float propertiesMaxScroll = 0.0f;
static bool propertiesScrollDragging = false;
static float propertiesScrollDragOffset = 0.0f;
static bool showCollisionDebug = false;
static TextInputState protoNameInput = {0};
static char protoNameBuffer[32] = "Custom";
static int protoNameObjectId = -1;
static int protoNamePackIndex = -1;
static ProtoCustomEntry protoCustomPacks[MAX_PROTO_CUSTOM] = {0};
static int protoCustomPackCount = 0;
static bool protoCustomLoaded = false;

enum
{
    TRANSFORM_POS_X = 0,
    TRANSFORM_POS_Y,
    TRANSFORM_POS_Z,
    TRANSFORM_ROT_X,
    TRANSFORM_ROT_Y,
    TRANSFORM_ROT_Z,
    TRANSFORM_FIELD_COUNT
};

static DragFloatInputState transformInputs[TRANSFORM_FIELD_COUNT] = {0};
static char transformBuffer[TRANSFORM_FIELD_COUNT][32] = {0};
static int transformObjectId = -1;
typedef struct
{
    int id;
    Vector3 posBefore;
    Vector3 rotBefore;
    Vector3 posAfter;
    Vector3 rotAfter;
} TransformHistoryEntry;
#define TRANSFORM_HISTORY_MAX 64
static TransformHistoryEntry transformUndoStack[TRANSFORM_HISTORY_MAX] = {0};
static int transformUndoTop = 0;
static TransformHistoryEntry transformRedoStack[TRANSFORM_HISTORY_MAX] = {0};
static int transformRedoTop = 0;
static bool transformEditSession = false;
static int transformEditId = -1;
static Vector3 transformEditStartPos = {0};
static Vector3 transformEditStartRot = {0};
static void SyncTransformBuffers(const ObjetoCena *obj);
extern void SetSelectedModelByObjetoId(int idObjeto);

typedef struct
{
    const char *name;
    Color base;
    bool invertPattern;
    bool useObjectColors;
} ProtoPack;

static float ColorLuma(Color c)
{
    return (0.2126f * (float)c.r + 0.7152f * (float)c.g + 0.0722f * (float)c.b) / 255.0f;
}

static unsigned char ClampByteLocal(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (unsigned char)v;
}

static Color MakeSecondary(Color base)
{
    base.a = 255;
    float l = ColorLuma(base);
    Color target = (l < 0.45f) ? RAYWHITE : BLACK;
    Color out = ColorLerp(base, target, 0.35f);
    out.a = 255;
    return out;
}

static Color DefaultCustomBase(void)
{
    return (Color){112, 112, 112, 255};
}

static Color DefaultCustomSecondary(void)
{
    return (Color){58, 58, 58, 255};
}

static void DrawCheckerPreview(Rectangle rect, Color a, Color b)
{
    a.a = 255;
    b.a = 255;
    float hw = rect.width * 0.5f;
    DrawRectangleRec((Rectangle){rect.x, rect.y, hw, rect.height}, a);
    DrawRectangleRec((Rectangle){rect.x + hw, rect.y, hw, rect.height}, b);
    DrawRectangleLinesEx(rect, 1, COR_BORDA);
}

static void GetPackColors(const ProtoPack *p, Color *baseOut, Color *secondaryOut, const Color *objBase, const Color *objSecondary)
{
    Color base = (p->useObjectColors && objBase) ? *objBase : p->base;
    base.a = 255;
    Color secondary = (p->useObjectColors && objSecondary) ? *objSecondary : MakeSecondary(base);
    if (!p->useObjectColors && p->invertPattern)
    {
        Color tmp = base;
        base = secondary;
        secondary = tmp;
    }
    if (baseOut)
        *baseOut = base;
    if (secondaryOut)
        *secondaryOut = secondary;
}

static const ProtoPack protoPacks[] = {
    {"GRAY", (Color){112, 112, 112, 255}, true, false},
    {"RED", (Color){184, 38, 36, 255}, true, false},
    {"BLUE", (Color){46, 104, 214, 255}, true, false},
    {"GREEN", (Color){58, 148, 78, 255}, false, false},
    {"YELLOW", (Color){214, 176, 46, 255}, false, false},
    {"CUSTOM", (Color){112, 112, 112, 255}, false, true}
};

static void SanitizeCustomName(const char *src, char *dst, size_t dstSize)
{
    if (!dst || dstSize == 0)
        return;
    dst[0] = '\0';
    if (!src)
        return;
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    for (size_t i = 0; dst[i] != '\0'; i++)
    {
        if (dst[i] == '|' || dst[i] == ';' || dst[i] == '\n' || dst[i] == '\r')
            dst[i] = '_';
    }
}

static void ResetTransformInputs(void)
{
    for (int i = 0; i < TRANSFORM_FIELD_COUNT; i++)
        DragFloatInputInit(&transformInputs[i]);
}

static bool AnyTransformInputActive(void)
{
    for (int i = 0; i < TRANSFORM_FIELD_COUNT; i++)
        if (DragFloatInputIsActive(&transformInputs[i]))
            return true;
    return false;
}

static bool Vec3Equal(Vector3 a, Vector3 b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

static void PushTransformUndo(TransformHistoryEntry entry)
{
    if (transformUndoTop < TRANSFORM_HISTORY_MAX)
        transformUndoStack[transformUndoTop++] = entry;
    else
    {
        for (int i = 0; i < TRANSFORM_HISTORY_MAX - 1; i++)
            transformUndoStack[i] = transformUndoStack[i + 1];
        transformUndoStack[TRANSFORM_HISTORY_MAX - 1] = entry;
    }
    transformRedoTop = 0;
}

static void BeginTransformSession(int id, Vector3 startPos, Vector3 startRot)
{
    transformEditSession = true;
    transformEditId = id;
    transformEditStartPos = startPos;
    transformEditStartRot = startRot;
}

static void FinalizeTransformSession(void)
{
    if (!transformEditSession || transformEditId <= 0)
        return;

    int idx = BuscarIndicePorId(transformEditId);
    if (idx != -1)
    {
        Vector3 endPos = objetos[idx].posicao;
        Vector3 endRot = objetos[idx].rotacao;
        if (!Vec3Equal(transformEditStartPos, endPos) || !Vec3Equal(transformEditStartRot, endRot))
        {
            TransformHistoryEntry e = {0};
            e.id = transformEditId;
            e.posBefore = transformEditStartPos;
            e.rotBefore = transformEditStartRot;
            e.posAfter = endPos;
            e.rotAfter = endRot;
            PushTransformUndo(e);
        }
    }

    transformEditSession = false;
    transformEditId = -1;
}

static bool ApplyTransformEntry(const TransformHistoryEntry *entry, bool useAfter)
{
    if (!entry || entry->id <= 0)
        return false;
    int idx = BuscarIndicePorId(entry->id);
    if (idx == -1)
        return false;

    objetos[idx].posicao = useAfter ? entry->posAfter : entry->posBefore;
    objetos[idx].rotacao = useAfter ? entry->rotAfter : entry->rotBefore;
    SetSelectedModelByObjetoId(entry->id);
    if (transformObjectId == entry->id)
        SyncTransformBuffers(&objetos[idx]);
    return true;
}

static void SyncTransformBuffers(const ObjetoCena *obj)
{
    if (!obj)
        return;
    DragFloatInputFormat(transformBuffer[TRANSFORM_POS_X], (int)sizeof(transformBuffer[TRANSFORM_POS_X]), obj->posicao.x);
    DragFloatInputFormat(transformBuffer[TRANSFORM_POS_Y], (int)sizeof(transformBuffer[TRANSFORM_POS_Y]), obj->posicao.y);
    DragFloatInputFormat(transformBuffer[TRANSFORM_POS_Z], (int)sizeof(transformBuffer[TRANSFORM_POS_Z]), obj->posicao.z);
    DragFloatInputFormat(transformBuffer[TRANSFORM_ROT_X], (int)sizeof(transformBuffer[TRANSFORM_ROT_X]), obj->rotacao.x * RAD2DEG);
    DragFloatInputFormat(transformBuffer[TRANSFORM_ROT_Y], (int)sizeof(transformBuffer[TRANSFORM_ROT_Y]), obj->rotacao.y * RAD2DEG);
    DragFloatInputFormat(transformBuffer[TRANSFORM_ROT_Z], (int)sizeof(transformBuffer[TRANSFORM_ROT_Z]), obj->rotacao.z * RAD2DEG);
}

static bool GetCustomPacksPath(char *out, size_t outSize)
{
    const char *cwd = GetWorkingDirectory();
    if (!cwd || cwd[0] == '\0' || !out || outSize == 0)
        return false;
    snprintf(out, outSize, "%s/prototype_custom_packs.txt", cwd);
    out[outSize - 1] = '\0';
    return true;
}

static void LoadCustomPacks(void)
{
    if (protoCustomLoaded)
        return;
    protoCustomLoaded = true;
    protoCustomPackCount = 0;

    char path[512];
    if (!GetCustomPacksPath(path, sizeof(path)))
        return;
    FILE *f = fopen(path, "rb");
    if (!f)
        return;

    char line[256];
    while (fgets(line, (int)sizeof(line), f))
    {
        if (protoCustomPackCount >= MAX_PROTO_CUSTOM)
            break;
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0')
            continue;

        char name[32] = {0};
        int br = -1, bg = -1, bb = -1;
        int sr = -1, sg = -1, sb = -1;
        if (sscanf(line, "%31[^|]|%d,%d,%d|%d,%d,%d", name, &br, &bg, &bb, &sr, &sg, &sb) == 7)
        {
            ProtoCustomEntry *e = &protoCustomPacks[protoCustomPackCount++];
            SanitizeCustomName(name, e->name, sizeof(e->name));
            e->base = (Color){ClampByteLocal(br), ClampByteLocal(bg), ClampByteLocal(bb), 255};
            e->secondary = (Color){ClampByteLocal(sr), ClampByteLocal(sg), ClampByteLocal(sb), 255};
        }
    }
    fclose(f);
}

static void SaveCustomPacks(void)
{
    char path[512];
    if (!GetCustomPacksPath(path, sizeof(path)))
        return;
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    for (int i = 0; i < protoCustomPackCount; i++)
    {
        const ProtoCustomEntry *e = &protoCustomPacks[i];
        char name[32];
        SanitizeCustomName(e->name, name, sizeof(name));
        fprintf(f, "%s|%d,%d,%d|%d,%d,%d\n",
                name,
                (int)e->base.r, (int)e->base.g, (int)e->base.b,
                (int)e->secondary.r, (int)e->secondary.g, (int)e->secondary.b);
    }
    fclose(f);
}

static int GetCustomEditorIndex(void)
{
    return (int)(sizeof(protoPacks) / sizeof(protoPacks[0])) - 1;
}

static int GetStaticPackCount(void)
{
    return (int)(sizeof(protoPacks) / sizeof(protoPacks[0]));
}

static int GetPackCount(const ObjetoCena *obj)
{
    (void)obj;
    return GetStaticPackCount() + protoCustomPackCount;
}

static bool IsCustomEditorPack(int index)
{
    return index == GetCustomEditorIndex();
}

static bool IsCustomEntryPack(const ObjetoCena *obj, int index)
{
    int baseCount = GetStaticPackCount();
    return (index >= baseCount) && (index < baseCount + protoCustomPackCount);
}

static int GetCustomEntryIndex(const ObjetoCena *obj, int index)
{
    int baseCount = GetStaticPackCount();
    int entry = index - baseCount;
    if (entry < 0 || entry >= protoCustomPackCount)
        return -1;
    return entry;
}

static void GetPackDisplay(const ObjetoCena *obj, int index, const char **nameOut, Color *baseOut, Color *secondaryOut)
{
    if (!obj)
        return;
    if (IsCustomEditorPack(index))
    {
        if (nameOut) *nameOut = (obj->protoCustomName[0] != '\0') ? obj->protoCustomName : "Custom";
        if (baseOut) *baseOut = obj->protoCustomBase;
        if (secondaryOut) *secondaryOut = obj->protoCustomSecondary;
        return;
    }
    if (index < GetStaticPackCount())
    {
        const ProtoPack *p = &protoPacks[index];
        if (nameOut) *nameOut = p->name;
        GetPackColors(p, baseOut, secondaryOut, nullptr, nullptr);
        return;
    }

    int entryIndex = GetCustomEntryIndex(obj, index);
    if (entryIndex >= 0)
    {
        const ProtoCustomEntry *e = &protoCustomPacks[entryIndex];
        if (nameOut) *nameOut = (e->name[0] != '\0') ? e->name : "Custom";
        if (baseOut) *baseOut = e->base;
        if (secondaryOut) *secondaryOut = e->secondary;
    }
}

static void ApplyProtoPackIndex(ObjetoCena *obj, int index)
{
    if (!obj)
        return;
    obj->protoPack = index;

    if (IsCustomEditorPack(index))
    {
        obj->protoBaseColor = obj->protoCustomBase;
        obj->protoSecondaryColor = obj->protoCustomSecondary;
        return;
    }

    if (index < GetStaticPackCount())
    {
        const ProtoPack *p = &protoPacks[index];
        GetPackColors(p, &obj->protoBaseColor, &obj->protoSecondaryColor, nullptr, nullptr);
        return;
    }

    int entryIndex = GetCustomEntryIndex(obj, index);
    if (entryIndex >= 0)
    {
        const ProtoCustomEntry *e = &protoCustomPacks[entryIndex];
        obj->protoBaseColor = e->base;
        obj->protoSecondaryColor = e->secondary;
    }
}

static void ApplyProtoPack(ObjetoCena *obj, int idx)
{
    if (!obj)
        return;
    int count = GetPackCount(obj);
    if (idx < 0)
        idx = 0;
    if (idx >= count)
        idx = count - 1;
    ApplyProtoPackIndex(obj, idx);
}

static int FindPhysEntryIndex(int objetoId)
{
    for (int i = 0; i < physEntryCount; i++)
        if (physEntries[i].id == objetoId)
            return i;
    return -1;
}

static bool ObjetoExiste(int objetoId)
{
    return BuscarIndicePorId(objetoId) != -1;
}

static PhysEntry *EnsurePhysEntry(int objetoId)
{
    if (objetoId <= 0)
        return nullptr;

    int idx = FindPhysEntryIndex(objetoId);
    if (idx != -1)
        return &physEntries[idx];

    if (physEntryCount >= MAX_OBJETOS)
        return nullptr;

    PhysEntry *e = &physEntries[physEntryCount++];
    e->id = objetoId;
    e->isStatic = true;
    e->rigidbody = false;
    e->collider = true;
    e->gravity = true;
    e->mass = 1.0f;
    e->shapeType = COLLISION_SHAPE_MESH_BOUNDS;
    e->shapeSize = (Vector3){1.0f, 1.0f, 1.0f};
    e->terrain = false;
    return e;
}

static void CleanupPhysEntries(void)
{
    for (int i = physEntryCount - 1; i >= 0; i--)
    {
        if (!ObjetoExiste(physEntries[i].id))
        {
            for (int j = i; j < physEntryCount - 1; j++)
                physEntries[j] = physEntries[j + 1];
            physEntryCount--;
        }
    }
}

static void DrawFloatSlider(const char *label, float *value, float minValue, float maxValue, int x, int *y, float width, bool allowInput)
{
    DrawText(TextFormat("%s: %.2f", label, *value), x + 14, *y, 12, COR_TEXTO);
    Rectangle bar = {(float)(x + 14), (float)(*y + 14), width, 8.0f};
    DrawRectangleRec(bar, COR_EDIT_BG);
    DrawRectangleLinesEx(bar, 1, COR_BORDA);

    float t = (*value - minValue) / (maxValue - minValue);
    t = Clamp(t, 0.0f, 1.0f);

    Rectangle fill = {bar.x, bar.y, bar.width * t, bar.height};
    DrawRectangleRec(fill, COR_ITEM_SEL);

    float knobX = bar.x + bar.width * t;
    DrawCircle((int)knobX, (int)(bar.y + bar.height / 2), 5, COR_ITEM_SEL);

    if (allowInput)
    {
        Vector2 mouse = GetMousePosition();
        Rectangle hit = {bar.x - 6, bar.y - 6, bar.width + 12, bar.height + 12};
        if (CheckCollisionPointRec(mouse, hit) && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            float u = (mouse.x - bar.x) / bar.width;
            u = Clamp(u, 0.0f, 1.0f);
            *value = minValue + (maxValue - minValue) * u;
        }
    }

    *y += 28;
}

static bool DrawOptionButton(Rectangle rect, const char *label, bool active, bool allowInput, Vector2 mouse)
{
    bool hover = CheckCollisionPointRec(mouse, rect);
    Color bg = active ? COR_ITEM_SEL : (hover ? COR_ITEM : COR_PAINEL);
    DrawRectangleRec(rect, bg);
    DrawRectangleLinesEx(rect, 1, COR_BORDA);
    DrawText(label, (int)rect.x + 6, (int)rect.y + 3, 11, active ? GetUIStyle()->buttonTextHover : COR_TEXTO);
    return allowInput && hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

static void DrawVec3Inputs(const char *label, Vector3 *value, int baseIndex, int x, int *y, bool allowInput, bool useDegrees)
{
    DrawText(label, x + 14, *y, 12, COR_TEXTO);
    *y += 16;
    const UIStyle *style = GetUIStyle();
    const Color activeTransformText = style->accent;

    float gap = 6.0f;
    float availW = (float)(PROPERTIES_PAINEL_LARGURA - 28);
    float boxW = (availW - gap * 2.0f) / 3.0f;
    float labelY = (float)(*y);
    float boxY = labelY + 12.0f;
    float boxH = 20.0f;

    const char *axes[3] = {"X", "Y", "Z"};

    DragFloatInputConfig cfg = {0};
    cfg.fontSize = 12;
    cfg.padding = 4;
    cfg.textColor = COR_TEXTO;
    cfg.bgColor = COR_EDIT_BG;
    cfg.borderColor = COR_BORDA;
    cfg.selectionColor = GetUIStyle()->inputSelection;
    cfg.caretColor = GetUIStyle()->caret;
    cfg.allowInput = allowInput;
    cfg.clamp = false;
    cfg.dragSpeed = useDegrees ? 0.2f : 0.01f;
    cfg.fineDragSpeed = useDegrees ? 0.02f : 0.001f;

    for (int i = 0; i < 3; i++)
    {
        float boxX = (float)(x + 14) + i * (boxW + gap);
        DrawText(axes[i], (int)boxX + 2, (int)labelY, 10, COR_TEXTO_SECUNDARIO);
        Rectangle box = {boxX, boxY, boxW, boxH};

        int idx = baseIndex + i;
        bool fieldActive = DragFloatInputIsActive(&transformInputs[idx]);
        DragFloatInputConfig fieldCfg = cfg;
        fieldCfg.textColor = fieldActive ? activeTransformText : cfg.textColor;
        float display = (i == 0) ? value->x : (i == 1) ? value->y : value->z;
        if (useDegrees)
            display *= RAD2DEG;

        int flags = DragFloatInputDraw(box, transformBuffer[idx], (int)sizeof(transformBuffer[idx]), &transformInputs[idx], &display, &fieldCfg);
        if (flags & DRAG_FLOAT_INPUT_CHANGED)
        {
            float store = useDegrees ? (display * DEG2RAD) : display;
            if (i == 0)
                value->x = store;
            else if (i == 1)
                value->y = store;
            else
                value->z = store;
        }
    }

    *y = (int)(boxY + boxH + 10.0f);
}

void InitPropertiesPanel(void)
{
    if (!fisicaInit)
    {
        physEntryCount = 0;
        fisicaInit = true;
    }
    TextInputInit(&protoNameInput);
    ResetTransformInputs();
    transformObjectId = -1;
    transformUndoTop = 0;
    transformRedoTop = 0;
    transformEditSession = false;
    transformEditId = -1;
    propertiesScroll = 0.0f;
    propertiesMaxScroll = 0.0f;
    propertiesScrollDragging = false;
    propertiesScrollDragOffset = 0.0f;
    LoadCustomPacks();
}

void DrawPropertiesPanel(void)
{
    int screenW = GetScreenWidth();
    int screenH = GetScreenHeight();
    int x = screenW - PROPERTIES_PAINEL_LARGURA;
    const int contentLeft = x + 14;
    const int contentRight = x + PROPERTIES_PAINEL_LARGURA - 14;

    DrawRectangle(x, 0, PROPERTIES_PAINEL_LARGURA, screenH, COR_PAINEL);
    DrawLine(x, 0, x, screenH, COR_BORDA);
    const UIStyle *style = GetUIStyle();
    Rectangle propertiesHeader = {(float)(x + 6), 6.0f, (float)(PROPERTIES_PAINEL_LARGURA - 12), 22.0f};
    DrawEditorHeader(propertiesHeader, "Properties", 14);

    CleanupPhysEntries();

    int selId = ObterObjetoSelecionadoId();
    int selIdx = (selId > 0) ? BuscarIndicePorId(selId) : -1;
    bool protoPickerOpen = ColorPickerIsOpen(&protoPicker);
    if (protoPickerOpen && (selIdx < 0 || protoPickerObjectId != selId))
    {
        ColorPickerClose(&protoPicker);
        protoPickerOpen = false;
    }
    bool allowInput = !protoPickerOpen;
    Vector2 mouse = GetMousePosition();

    float contentTop = 40.0f;
    float viewH = (float)screenH - contentTop;
    Rectangle contentRect = {(float)x, contentTop, (float)PROPERTIES_PAINEL_LARGURA, viewH};
    if (propertiesScroll < 0.0f)
        propertiesScroll = 0.0f;
    if (propertiesScroll > propertiesMaxScroll)
        propertiesScroll = propertiesMaxScroll;
    if (allowInput && CheckCollisionPointRec(mouse, contentRect))
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            propertiesScroll -= wheel * 24.0f;
    }

    BeginScissorMode((int)contentRect.x, (int)contentRect.y, (int)contentRect.width, (int)contentRect.height);

    int y = (int)(contentTop + 4 - propertiesScroll);

    bool objectClicked = DrawSectionHeader("Objeto", x, &y, &propertiesObjectExpanded, allowInput, mouse);
    if (objectClicked && CtrlHeld() && propertiesObjectExpanded)
    {
        propertiesTransformExpanded = true;
        propertiesCameraExpanded = true;
        propertiesPhysicsExpanded = true;
        propertiesPrototypeExpanded = true;
    }
    if (!propertiesObjectExpanded)
    {
        if (transformEditSession)
            FinalizeTransformSession();
        if (transformObjectId != -1)
        {
            ResetTransformInputs();
            transformObjectId = -1;
        }
    }

    if (propertiesObjectExpanded && selIdx >= 0)
    {
        ObjetoCena *obj = &objetos[selIdx];
        bool isCamera = ObjetoEhCamera(obj);
        bool transformClicked = DrawSectionHeader("Transform", x, &y, &propertiesTransformExpanded, allowInput, mouse);
        if (transformClicked && CtrlHeld() && propertiesTransformExpanded)
            FocusPropertiesSection(&propertiesTransformExpanded);
        if (propertiesTransformExpanded)
        {
            bool transformWasActive = AnyTransformInputActive();
            Vector3 transformBeforePos = obj->posicao;
            Vector3 transformBeforeRot = obj->rotacao;

            if (transformObjectId != selId)
            {
                if (transformEditSession)
                    FinalizeTransformSession();
                ResetTransformInputs();
                SyncTransformBuffers(obj);
                transformObjectId = selId;
            }
            else if (!AnyTransformInputActive())
            {
                SyncTransformBuffers(obj);
            }

            DrawVec3Inputs("Location", &obj->posicao, TRANSFORM_POS_X, x, &y, allowInput, false);
            DrawVec3Inputs("Rotation", &obj->rotacao, TRANSFORM_ROT_X, x, &y, allowInput, true);
            bool transformNowActive = AnyTransformInputActive();
            if (!transformWasActive && transformNowActive)
                BeginTransformSession(selId, transformBeforePos, transformBeforeRot);
            if (transformEditSession && (!transformNowActive || transformEditId != selId))
                FinalizeTransformSession();
        }

        y += 6;
        if (isCamera)
        {
            bool cameraClicked = DrawSectionHeader("Camera", x, &y, &propertiesCameraExpanded, allowInput, mouse);
            if (cameraClicked && CtrlHeld() && propertiesCameraExpanded)
                FocusPropertiesSection(&propertiesCameraExpanded);
            if (propertiesCameraExpanded)
            {
                DrawText("Ativar Camera", x + 14, y, 12, COR_TEXTO);
                Rectangle mainToggle = {(float)(contentRight - 16), (float)(y - 2), 16.0f, 16.0f};
                DrawRectangleLinesEx(mainToggle, 1, COR_BORDA);
                if (obj->cameraPrincipal)
                    DrawRectangle((int)mainToggle.x + 3, (int)mainToggle.y + 3, 10, 10, COR_ITEM_SEL);
                if (allowInput && CheckCollisionPointRec(mouse, mainToggle) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                {
                    if (obj->cameraPrincipal)
                        obj->cameraPrincipal = false;
                    else
                        SetSceneRenderCameraObjectId(selId);
                }
                y += 22;

                DrawText("Projection", x + 14, y, 12, COR_TEXTO);
                y += 18;
                int projectionGap = 6;
                int projectionWidth = (PROPERTIES_PAINEL_LARGURA - 28 - projectionGap) / 2;
                Rectangle perspectiveBtn = {(float)(x + 14), (float)y, (float)projectionWidth, 20.0f};
                Rectangle orthoBtn = {(float)(x + 14 + projectionWidth + projectionGap), (float)y, (float)projectionWidth, 20.0f};
                if (DrawOptionButton(perspectiveBtn, "Perspective", obj->cameraProjection == CAMERA_PERSPECTIVE, allowInput, mouse))
                    obj->cameraProjection = CAMERA_PERSPECTIVE;
                if (DrawOptionButton(orthoBtn, "Orthographic", obj->cameraProjection == CAMERA_ORTHOGRAPHIC, allowInput, mouse))
                    obj->cameraProjection = CAMERA_ORTHOGRAPHIC;
                y += 28;

                if (obj->cameraProjection == CAMERA_ORTHOGRAPHIC)
                    DrawFloatSlider("Size", &obj->cameraOrthoSize, 0.1f, 50.0f, x, &y, (float)(PROPERTIES_PAINEL_LARGURA - 28), allowInput);
                else
                    DrawFloatSlider("Field of View", &obj->cameraPerspectiveFov, 10.0f, 140.0f, x, &y, (float)(PROPERTIES_PAINEL_LARGURA - 28), allowInput);

                int actionGap = 6;
                int actionWidth = (PROPERTIES_PAINEL_LARGURA - 28 - actionGap) / 2;
                Rectangle useViewBtn = {(float)(x + 14), (float)y, (float)actionWidth, 20.0f};
                Rectangle lookBtn = {(float)(x + 14 + actionWidth + actionGap), (float)y, (float)actionWidth, 20.0f};
                if (DrawOptionButton(useViewBtn, "Use View", false, allowInput, mouse))
                {
                    Camera viewCamera = GetEditorViewportCamera();
                    CopySceneObjectFromCameraView(obj, &viewCamera);
                }
                if (DrawOptionButton(lookBtn, "Look Through", false, allowInput, mouse))
                {
                    Camera objectCamera = {0};
                    if (BuildSceneCameraFromObject(obj, &objectCamera))
                        SetEditorViewportCamera(&objectCamera);
                }
                y += 30;
            }

            y += 6;
        }

        if (!isCamera)
        {
        bool physicsClicked = DrawSectionHeader("Fisica", x, &y, &propertiesPhysicsExpanded, allowInput, mouse);
        if (physicsClicked && CtrlHeld() && propertiesPhysicsExpanded)
            FocusPropertiesSection(&propertiesPhysicsExpanded);
        if (propertiesPhysicsExpanded)
        {
            PhysEntry *phys = EnsurePhysEntry(selId);

        DrawText("Tipo", x + 14, y, 12, style->accent);
        y += 18;
        const Color activeCheckText = (Color){0, 255, 102, 255}; // #00FF66

        Rectangle staticToggle = {(float)(x + 14), (float)y, 14.0f, 14.0f};
        Rectangle staticRow = {(float)(x + 14), (float)y - 2.0f, (float)(PROPERTIES_PAINEL_LARGURA - 28), 18.0f};
        bool staticOn = (phys && phys->isStatic);
        DrawRectangleLinesEx(staticToggle, 1, COR_BORDA);
        if (staticOn)
            DrawRectangle((int)staticToggle.x + 3, (int)staticToggle.y + 3, 8, 8, COR_ITEM_SEL);
        DrawText("Static", x + 34, y - 2, 12, staticOn ? activeCheckText : COR_TEXTO);
        if (allowInput && CheckCollisionPointRec(mouse, staticRow) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && phys)
        {
            phys->isStatic = true;
            phys->rigidbody = false;
        }
        y += 18;

        Rectangle rbToggle = {(float)(x + 14), (float)y, 14.0f, 14.0f};
        Rectangle rbRow = {(float)(x + 14), (float)y - 2.0f, (float)(PROPERTIES_PAINEL_LARGURA - 28), 18.0f};
        bool rbOn = (phys && phys->rigidbody);
        DrawRectangleLinesEx(rbToggle, 1, COR_BORDA);
        if (rbOn)
            DrawRectangle((int)rbToggle.x + 3, (int)rbToggle.y + 3, 8, 8, COR_ITEM_SEL);
        DrawText("Rigidbody", x + 34, y - 2, 12, rbOn ? activeCheckText : COR_TEXTO);
        if (allowInput && CheckCollisionPointRec(mouse, rbRow) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && phys)
        {
            phys->rigidbody = true;
            phys->isStatic = false;
        }
        y += 18;

        Rectangle colliderToggle = {(float)(x + 14), (float)y, 14.0f, 14.0f};
        Rectangle colliderRow = {(float)(x + 14), (float)y - 2.0f, (float)(PROPERTIES_PAINEL_LARGURA - 28), 18.0f};
        bool colliderOn = (phys && (phys->collider || phys->terrain));
        DrawRectangleLinesEx(colliderToggle, 1, COR_BORDA);
        if (colliderOn)
            DrawRectangle((int)colliderToggle.x + 3, (int)colliderToggle.y + 3, 8, 8, COR_ITEM_SEL);
        DrawText("Collider", x + 34, y - 2, 12, colliderOn ? activeCheckText : COR_TEXTO_SECUNDARIO);
        if (allowInput && CheckCollisionPointRec(mouse, colliderRow) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && phys)
        {
            phys->collider = !colliderOn;
            if (!phys->collider)
                phys->terrain = false;
        }
        y += 18;

        Rectangle gravityToggle = {(float)(x + 14), (float)y, 14.0f, 14.0f};
        Rectangle gravityRow = {(float)(x + 14), (float)y - 2.0f, (float)(PROPERTIES_PAINEL_LARGURA - 28), 18.0f};
        bool gravityOn = (phys && phys->gravity);
        DrawRectangleLinesEx(gravityToggle, 1, COR_BORDA);
        if (gravityOn)
            DrawRectangle((int)gravityToggle.x + 3, (int)gravityToggle.y + 3, 8, 8, COR_ITEM_SEL);
        DrawText("Gravity", x + 34, y - 2, 12, gravityOn ? activeCheckText : COR_TEXTO_SECUNDARIO);
        if (allowInput && CheckCollisionPointRec(mouse, gravityRow) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && phys)
            phys->gravity = !phys->gravity;
        y += 18;

        if (phys && (phys->collider || phys->terrain))
        {
            DrawText("Collision", x + 14, y, 12, COR_TEXTO);
            y += 18;

            Rectangle collDebugToggle = {(float)(x + 14), (float)y, 14.0f, 14.0f};
            DrawRectangleLinesEx(collDebugToggle, 1, COR_BORDA);
            if (showCollisionDebug)
                DrawRectangle((int)collDebugToggle.x + 3, (int)collDebugToggle.y + 3, 8, 8, COR_ITEM_SEL);
            DrawText("Show Collisions", x + 34, y - 2, 12, COR_TEXTO);
            if (allowInput && CheckCollisionPointRec(mouse, collDebugToggle) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                showCollisionDebug = !showCollisionDebug;
            y += 18;

            DrawText("Collision Mode", x + 14, y, 12, COR_TEXTO);
            y += 18;

            int gap = 6;
            int availW = PROPERTIES_PAINEL_LARGURA - 28;
            int btnW = (availW - gap) / 2;
            int btnH = 20;
            Rectangle btnMesh = {(float)(x + 14), (float)y, (float)btnW, (float)btnH};
            Rectangle btnTerrain = {(float)(x + 14 + btnW + gap), (float)y, (float)btnW, (float)btnH};
            bool meshActive = !phys->terrain;
            bool terrainActive = phys->terrain;

            bool hoverMesh = CheckCollisionPointRec(mouse, btnMesh);
            bool hoverTerrain = CheckCollisionPointRec(mouse, btnTerrain);
            Color meshBg = meshActive ? COR_ITEM_SEL : (hoverMesh ? COR_ITEM : COR_PAINEL);
            Color terrainBg = terrainActive ? COR_ITEM_SEL : (hoverTerrain ? COR_ITEM : COR_PAINEL);

            DrawRectangleRec(btnMesh, meshBg);
            DrawRectangleLinesEx(btnMesh, 1, COR_BORDA);
            Color meshText = meshActive ? GetUIStyle()->buttonTextHover : COR_TEXTO;
            DrawText("Mesh Bounds", (int)btnMesh.x + 4, (int)btnMesh.y + 3, 10, meshText);

            DrawRectangleRec(btnTerrain, terrainBg);
            DrawRectangleLinesEx(btnTerrain, 1, COR_BORDA);
            Color terrainText = terrainActive ? GetUIStyle()->buttonTextHover : COR_TEXTO;
            DrawText("Terrain", (int)btnTerrain.x + 4, (int)btnTerrain.y + 3, 10, terrainText);

            if (allowInput)
            {
                if (hoverMesh && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    phys->terrain = false;
                else if (hoverTerrain && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                {
                    phys->terrain = true;
                    phys->isStatic = true;
                    phys->rigidbody = false;
                    phys->collider = true;
                }
            }

            y += btnH + 6;
            if (phys->terrain)
            {
                y += 16;
            }
        }

        if (phys)
        {
            DrawFloatSlider("Mass", &phys->mass, 0.1f, 100.0f, x, &y, (float)(PROPERTIES_PAINEL_LARGURA - 28), allowInput);
        }
        else
        {
            y += 28;
        }
        }

        y += 6;
        bool prototypeClicked = DrawSectionHeader("Prototype", x, &y, &propertiesPrototypeExpanded, allowInput, mouse);
        if (prototypeClicked && CtrlHeld() && propertiesPrototypeExpanded)
            FocusPropertiesSection(&propertiesPrototypeExpanded);
        if (propertiesPrototypeExpanded)
        {

        DrawText("Enabled", contentLeft, y, 12, COR_TEXTO);
        Rectangle protoToggle = {(float)(contentRight - 16), (float)(y - 2), 16.0f, 16.0f};
        DrawRectangleLinesEx(protoToggle, 1, COR_BORDA);
        if (obj->protoEnabled)
            DrawRectangle((int)protoToggle.x + 3, (int)protoToggle.y + 3, 10, 10, COR_ITEM_SEL);

        if (allowInput && CheckCollisionPointRec(mouse, protoToggle) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            obj->protoEnabled = !obj->protoEnabled;
        }

        y += 18;

        if (obj->protoEnabled)
        {
            DrawText("Pack", contentLeft, y, 12, COR_TEXTO);
            y += 18;

            int packCount = GetPackCount(obj);
            if (obj->protoPack < 0 || obj->protoPack >= packCount)
                obj->protoPack = 0;
            int packCols = 3;
            int gap = 6;
            int packRows = (packCount + packCols - 1) / packCols;
            int startX2 = contentLeft;
            int availW2 = PROPERTIES_PAINEL_LARGURA - 28;
            int btnW2 = (availW2 - gap * (packCols - 1)) / packCols;
            int btnH2 = 34;

            for (int r = 0; r < packRows; r++)
            {
                for (int c = 0; c < packCols; c++)
                {
                    int i = r * packCols + c;
                    if (i >= packCount)
                        break;
                    Rectangle btn = {(float)(startX2 + c * (btnW2 + gap)), (float)(y + r * (btnH2 + gap)), (float)btnW2, (float)btnH2};
                    bool hover = CheckCollisionPointRec(mouse, btn);
                    bool active = (obj->protoPack == i);
                    Color bg = active ? COR_ITEM_SEL : (hover ? COR_ITEM : COR_PAINEL);
                    DrawRectangleRec(btn, bg);
                    DrawRectangleLinesEx(btn, 1, COR_BORDA);
                    const char *packName = "Custom";
                    Color base = {0};
                    Color secondary = {0};
                    GetPackDisplay(obj, i, &packName, &base, &secondary);
                    Color packText = active ? GetUIStyle()->buttonTextHover : COR_TEXTO;
                    DrawText(packName, (int)btn.x + 4, (int)btn.y + 3, 10, packText);

                    if ((IsCustomEditorPack(i) || IsCustomEntryPack(obj, i)) && i == obj->protoPack)
                    {
                        base = obj->protoBaseColor;
                        secondary = obj->protoSecondaryColor;
                    }
                    Rectangle preview = {btn.x + 4, btn.y + btn.height - 12, btn.width - 8, 8};
                    DrawCheckerPreview(preview, base, secondary);

                    if (allowInput && hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    {
                        ApplyProtoPack(obj, i);
                        protoNamePackIndex = -1;
                    }
                }
            }

            y += packRows * (btnH2 + gap) + 6;

            DrawText("Cor Base", contentLeft, y, 12, COR_TEXTO);
            Rectangle baseSwatch = {(float)(contentRight - 16), (float)(y - 2), 16.0f, 16.0f};
            DrawRectangleRec(baseSwatch, obj->protoBaseColor);
            DrawRectangleLinesEx(baseSwatch, 1, COR_BORDA);
            if (allowInput && CheckCollisionPointRec(mouse, baseSwatch) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            {
                protoPickerTarget = 0;
                protoPickerObjectId = selId;
                ColorPickerOpen(&protoPicker, obj->protoBaseColor);
            }
            y += 20;

            DrawText("Cor Secundaria", contentLeft, y, 12, COR_TEXTO);
            Rectangle secSwatch = {(float)(contentRight - 16), (float)(y - 2), 16.0f, 16.0f};
            DrawRectangleRec(secSwatch, obj->protoSecondaryColor);
            DrawRectangleLinesEx(secSwatch, 1, COR_BORDA);
            if (allowInput && CheckCollisionPointRec(mouse, secSwatch) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            {
                protoPickerTarget = 1;
                protoPickerObjectId = selId;
                ColorPickerOpen(&protoPicker, obj->protoSecondaryColor);
            }
            y += 22;

            bool isCustom = IsCustomEditorPack(obj->protoPack) || IsCustomEntryPack(obj, obj->protoPack);
            if (isCustom)
            {
                if (protoNameObjectId != selId || protoNamePackIndex != obj->protoPack)
                {
                    if (IsCustomEntryPack(obj, obj->protoPack))
                    {
                        int entryIndex = GetCustomEntryIndex(obj, obj->protoPack);
                        const char *entryName = (entryIndex >= 0) ? protoCustomPacks[entryIndex].name : "Custom";
                        strncpy(protoNameBuffer, entryName, sizeof(protoNameBuffer) - 1);
                    }
                    else
                    {
                        strncpy(protoNameBuffer, obj->protoCustomName, sizeof(protoNameBuffer) - 1);
                    }
                    protoNameBuffer[sizeof(protoNameBuffer) - 1] = '\0';
                    protoNameObjectId = selId;
                    protoNamePackIndex = obj->protoPack;
                    TextInputInit(&protoNameInput);
                }

                DrawText("Name", x + 14, y, 12, COR_TEXTO);
                Rectangle nameBox = {(float)(x + 14), (float)(y + 14), (float)(PROPERTIES_PAINEL_LARGURA - 28), 22.0f};
                TextInputConfig cfg = {0};
                cfg.fontSize = 12;
                cfg.padding = 4;
                cfg.textColor = COR_TEXTO;
                cfg.bgColor = COR_EDIT_BG;
                cfg.borderColor = COR_BORDA;
                cfg.selectionColor = GetUIStyle()->inputSelection;
                cfg.caretColor = GetUIStyle()->caret;
                cfg.filter = TEXT_INPUT_FILTER_NONE;
                cfg.allowInput = allowInput;

                int flags = TextInputDraw(nameBox, protoNameBuffer, (int)sizeof(protoNameBuffer), &protoNameInput, &cfg);
                if (flags & (TEXT_INPUT_CHANGED | TEXT_INPUT_SUBMITTED | TEXT_INPUT_DEACTIVATED))
                {
                    if (IsCustomEditorPack(obj->protoPack))
                    {
                        strncpy(obj->protoCustomName, protoNameBuffer, sizeof(obj->protoCustomName) - 1);
                        obj->protoCustomName[sizeof(obj->protoCustomName) - 1] = '\0';
                    }
                }
                y += 40;

                Rectangle saveBtn = {(float)(x + 14), (float)(y), (float)(PROPERTIES_PAINEL_LARGURA - 28), 20.0f};
                bool saveHover = CheckCollisionPointRec(mouse, saveBtn);
                Color saveBg = saveHover ? COR_ITEM : COR_PAINEL;
                DrawRectangleRec(saveBtn, saveBg);
                DrawRectangleLinesEx(saveBtn, 1, COR_BORDA);
                DrawText("Save Custom", (int)saveBtn.x + 6, (int)saveBtn.y + 3, 12, COR_TEXTO);
                if (allowInput && saveHover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                {
                    char saveName[32];
                    if (protoNameBuffer[0] == '\0')
                        strncpy(saveName, "Custom", sizeof(saveName) - 1);
                    else
                        strncpy(saveName, protoNameBuffer, sizeof(saveName) - 1);
                    saveName[sizeof(saveName) - 1] = '\0';

                    int targetIndex = -1;
                    if (IsCustomEntryPack(obj, obj->protoPack))
                        targetIndex = GetCustomEntryIndex(obj, obj->protoPack);
                    if (targetIndex < 0)
                    {
                        for (int c = 0; c < protoCustomPackCount; c++)
                        {
                            if (strcmp(protoCustomPacks[c].name, saveName) == 0)
                            {
                                targetIndex = c;
                                break;
                            }
                        }
                    }
                    if (targetIndex < 0)
                    {
                        if (protoCustomPackCount < MAX_PROTO_CUSTOM)
                            targetIndex = protoCustomPackCount++;
                        else
                            targetIndex = MAX_PROTO_CUSTOM - 1;
                    }
                    if (targetIndex >= 0)
                    {
                        ProtoCustomEntry *e = &protoCustomPacks[targetIndex];
                        strncpy(e->name, saveName, sizeof(e->name) - 1);
                        e->name[sizeof(e->name) - 1] = '\0';
                        e->base = obj->protoBaseColor;
                        e->secondary = obj->protoSecondaryColor;
                        obj->protoCustomBase = obj->protoBaseColor;
                        obj->protoCustomSecondary = obj->protoSecondaryColor;
                        strncpy(obj->protoCustomName, saveName, sizeof(obj->protoCustomName) - 1);
                        obj->protoCustomName[sizeof(obj->protoCustomName) - 1] = '\0';
                        obj->protoPack = GetCustomEditorIndex();
                        protoNamePackIndex = obj->protoPack;
                        SaveCustomPacks();

                        obj->protoBaseColor = DefaultCustomBase();
                        obj->protoSecondaryColor = DefaultCustomSecondary();
                        obj->protoCustomBase = obj->protoBaseColor;
                        obj->protoCustomSecondary = obj->protoSecondaryColor;
                        strncpy(obj->protoCustomName, "Custom", sizeof(obj->protoCustomName) - 1);
                        obj->protoCustomName[sizeof(obj->protoCustomName) - 1] = '\0';
                        protoNameBuffer[0] = '\0';
                        TextInputInit(&protoNameInput);
                    }
                }
                y += 30;

                if (IsCustomEntryPack(obj, obj->protoPack))
                {
                    Rectangle delBtn = {(float)(x + 14), (float)(y), (float)(PROPERTIES_PAINEL_LARGURA - 28), 20.0f};
                    bool delHover = CheckCollisionPointRec(mouse, delBtn);
                    Color delBg = delHover ? (Color){98, 30, 30, 255} : COR_PAINEL;
                    DrawRectangleRec(delBtn, delBg);
                    DrawRectangleLinesEx(delBtn, 1, COR_BORDA);
                    DrawText("Delete Custom", (int)delBtn.x + 6, (int)delBtn.y + 3, 12, COR_TEXTO);
                    if (allowInput && delHover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                    {
                        int delIndex = GetCustomEntryIndex(obj, obj->protoPack);
                        if (delIndex >= 0)
                        {
                            for (int c = delIndex; c < protoCustomPackCount - 1; c++)
                                protoCustomPacks[c] = protoCustomPacks[c + 1];
                            protoCustomPackCount--;
                            SaveCustomPacks();
                            ApplyProtoPackIndex(obj, GetCustomEditorIndex());
                            protoNamePackIndex = obj->protoPack;
                        }
                    }
                    y += 30;
                }
            }


            // preview agora fica nos botões de pack
        }
        else
        {
            y += 16;
        }
    }
        }
    }
    else if (propertiesObjectExpanded)
    {
        if (transformEditSession)
            FinalizeTransformSession();
        if (transformObjectId != -1)
        {
            ResetTransformInputs();
            transformObjectId = -1;
        }
        DrawText("Selecione um objeto", x + 14, y, 12, COR_TEXTO_SECUNDARIO);
        y += 18;
    }
    EndScissorMode();


    float contentHeight = (float)y + propertiesScroll - contentTop + 8.0f;
    float maxScroll = contentHeight - viewH;
    if (maxScroll < 0.0f)
        maxScroll = 0.0f;
    propertiesMaxScroll = maxScroll;
    if (propertiesScroll < 0.0f)
        propertiesScroll = 0.0f;
    if (propertiesScroll > maxScroll)
        propertiesScroll = maxScroll;

    if (maxScroll > 0.0f)
    {
        float barW = 6.0f;
        float barH = viewH * (viewH / (contentHeight + 1.0f));
        if (barH < 24.0f)
            barH = 24.0f;
        float barX = (float)(x + PROPERTIES_PAINEL_LARGURA) - barW - 2.0f;
        float barY = contentTop + (propertiesScroll / maxScroll) * (viewH - barH);
        Rectangle bar = {barX, barY, barW, barH};

        if (allowInput && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mouse, bar))
        {
            propertiesScrollDragging = true;
            propertiesScrollDragOffset = mouse.y - barY;
        }
        if (propertiesScrollDragging)
        {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
            {
                float newBarY = mouse.y - propertiesScrollDragOffset;
                if (newBarY < contentTop)
                    newBarY = contentTop;
                if (newBarY > contentTop + viewH - barH)
                    newBarY = contentTop + viewH - barH;
                propertiesScroll = ((newBarY - contentTop) / (viewH - barH)) * maxScroll;
            }
            else
            {
                propertiesScrollDragging = false;
            }
        }

        Color barCol = GetUIStyle()->panelBorderSoft;
        barCol.a = 200;
        DrawRectangleRec(bar, barCol);
        DrawRectangleLinesEx(bar, 1, COR_BORDA);
    }
    else
    {
        propertiesScrollDragging = false;
    }

    if (protoPickerOpen && selIdx >= 0)
    {
        ObjetoCena *obj = &objetos[selIdx];
        float pickerH = 220.0f;
        float pickerY = 130.0f;
        if (pickerY + pickerH > screenH - 16)
            pickerH = (float)screenH - pickerY - 16.0f;
        if (pickerH < 190.0f)
            pickerH = 190.0f;
        float pickerW = (float)(PROPERTIES_PAINEL_LARGURA - 80);
        float pickerX = (float)x + ((float)PROPERTIES_PAINEL_LARGURA - pickerW) * 0.5f;
        Rectangle pickerPanel = {pickerX, pickerY, pickerW, pickerH};
        const char *title = (protoPickerTarget == 0) ? "Cor Base" : "Cor Secundaria";
        Color newColor = ColorPickerDraw(&protoPicker, pickerPanel, title);
        if (protoPickerTarget == 0)
            obj->protoBaseColor = newColor;
        else
            obj->protoSecondaryColor = newColor;

        if (IsCustomEditorPack(obj->protoPack) || IsCustomEntryPack(obj, obj->protoPack))
        {
            obj->protoCustomBase = obj->protoBaseColor;
            obj->protoCustomSecondary = obj->protoSecondaryColor;
        }
    }

}

bool PropertiesHandleTransformShortcuts(void)
{
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (!ctrl)
        return false;

    if (IsKeyPressed(KEY_Z))
    {
        if (transformEditSession)
            FinalizeTransformSession();
        if (transformUndoTop <= 0)
            return false;

        TransformHistoryEntry entry = transformUndoStack[--transformUndoTop];
        if (!ApplyTransformEntry(&entry, false))
            return false;
        if (transformRedoTop < TRANSFORM_HISTORY_MAX)
            transformRedoStack[transformRedoTop++] = entry;
        return true;
    }

    if (IsKeyPressed(KEY_Y))
    {
        if (transformEditSession)
            FinalizeTransformSession();
        if (transformRedoTop <= 0)
            return false;

        TransformHistoryEntry entry = transformRedoStack[--transformRedoTop];
        if (!ApplyTransformEntry(&entry, true))
            return false;
        if (transformUndoTop < TRANSFORM_HISTORY_MAX)
            transformUndoStack[transformUndoTop++] = entry;
        return true;
    }

    return false;
}

bool PropertiesIsStatic(int index)
{
    if (index <= 0)
        return true;
    PhysEntry *phys = EnsurePhysEntry(index);
    return phys ? phys->isStatic : true;
}

bool PropertiesIsRigidbody(int index)
{
    if (index <= 0)
        return false;
    PhysEntry *phys = EnsurePhysEntry(index);
    return phys ? phys->rigidbody : false;
}

bool PropertiesHasGravity(int index)
{
    if (index <= 0)
        return false;
    PhysEntry *phys = EnsurePhysEntry(index);
    return phys ? phys->gravity : false;
}

bool PropertiesHasCollider(int index)
{
    if (index <= 0)
        return false;
    PhysEntry *phys = EnsurePhysEntry(index);
    return phys ? (phys->collider || phys->terrain) : false;
}

bool PropertiesIsTerrain(int index)
{
    if (index <= 0)
        return false;
    PhysEntry *phys = EnsurePhysEntry(index);
    return phys ? phys->terrain : false;
}

bool PropertiesShowCollisions(void)
{
    return showCollisionDebug;
}

void SetPropertiesShowCollisions(bool enabled)
{
    showCollisionDebug = enabled;
}

int PropertiesGetCollisionShape(int index)
{
    if (index <= 0)
        return COLLISION_SHAPE_MESH_BOUNDS;
    PhysEntry *phys = EnsurePhysEntry(index);
    if (!phys)
        return COLLISION_SHAPE_MESH_BOUNDS;
    return phys->shapeType;
}

Vector3 PropertiesGetCollisionSize(int index)
{
    if (index <= 0)
        return (Vector3){1.0f, 1.0f, 1.0f};
    PhysEntry *phys = EnsurePhysEntry(index);
    return phys ? phys->shapeSize : (Vector3){1.0f, 1.0f, 1.0f};
}

float PropertiesGetMass(int index)
{
    if (index <= 0)
        return 1.0f;
    PhysEntry *phys = EnsurePhysEntry(index);
    return phys ? phys->mass : 1.0f;
}

void PropertiesSyncToObjeto(ObjetoCena *o)
{
    if (!o)
        return;
    PhysEntry *phys = EnsurePhysEntry(o->id);
    if (!phys)
        return;
    o->physStatic = phys->isStatic;
    o->physRigidbody = phys->rigidbody;
    o->physCollider = phys->collider;
    o->physGravity = phys->gravity;
    o->physTerrain = phys->terrain;
    o->physMass = phys->mass;
    o->physShape = phys->shapeType;
    o->physSize = phys->shapeSize;
}

void PropertiesSyncFromObjeto(const ObjetoCena *o)
{
    if (!o)
        return;
    PhysEntry *phys = EnsurePhysEntry(o->id);
    if (!phys)
        return;
    phys->isStatic = o->physStatic;
    phys->rigidbody = o->physRigidbody;
    phys->collider = o->physCollider;
    phys->gravity = o->physGravity;
    phys->terrain = o->physTerrain;
    phys->mass = o->physMass;
    phys->shapeType = o->physShape;
    phys->shapeSize = o->physSize;
}

bool PropertiesIsBlockingViewport(void)
{
    return ColorPickerIsOpen(&protoPicker);
}

