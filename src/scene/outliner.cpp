#include "outliner.h"
#include "scene_manager.h"
#include "assets/model_manager.h"
#include "editor/ui/properties_panel.h"
#include "editor/ui/text_input.h"
#include "editor/ui/ui_style.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Paleta baseada no UIStyle
#define COR_FUNDO (GetUIStyle()->panelBgAlt)
#define COR_PAINEL (GetUIStyle()->panelBg)
#define COR_BORDA (GetUIStyle()->panelBorder)
#define COR_TEXTO (GetUIStyle()->textPrimary)
#define COR_ITEM (GetUIStyle()->textSecondary)
#define COR_ITEM_SEL (GetUIStyle()->itemActive)
#define COR_ITEM_DRAG (GetUIStyle()->itemHover)
#define COR_MENU (GetUIStyle()->panelBg)
#define COR_MENU_HOVER (GetUIStyle()->itemHover)
#define COR_EDIT_BG (GetUIStyle()->inputBg)
typedef enum
{
    ACAO_REPARENT,
    ACAO_DELETE
} TipoAcao;

typedef struct
{
    TipoAcao tipo;
    int objetoId;
    int paiAntigo;
    ObjetoCena backup;
} AcaoUndo;

ObjetoCena objetos[MAX_OBJETOS];
int totalObjetos = 0;
static int proximoId = 1;

static AcaoUndo undoStack[MAX_UNDO];
static int undoTopo = 0;

static Color DefaultPrototypeCustomBaseColor(void)
{
    return (Color){112, 112, 112, 255};
}

static Color DefaultPrototypeCustomSecondaryColor(void)
{
    return (Color){58, 58, 58, 255};
}

static bool menuAtivo = false;
static int menuObjetoId = -1;
static Vector2 menuPos;
static bool menuMoverCenaAtivo = false;
static int menuMoverHoverScene = -1;
static int dropSceneIndex = -1;

static bool arrastando = false;
static int objetoArrastadoId = -1;
static int alvoArrasteId = -1;
static Vector2 arrasteInicioMouse = {0};
static bool cliqueSemShiftPendenteSelecaoUnica = false;

static bool renomeando = false;
static int renomearObjetoId = -1;
static char bufferNome[32];
static TextInputState renameInput = {0};
static Rectangle caixaEdicao;
static bool bloquearCliqueOutliner = false;
static int propUltimoSelecionadoId = -1;
static char propBufferNome[32] = {0};
static TextInputState propNameInput = {0};
static bool renomeandoCena = false;
static int cenaEditandoIndex = -1;
static char bufferCena[32] = {0};
static TextInputState sceneNameInput = {0};
static double ultimoCliqueCena = 0.0;
static int ultimoCliqueCenaIndex = -1;
static bool menuCenaContextoAtivo = false;
static Vector2 menuCenaPos = {0};
static int menuCenaIndex = -1;
static bool sceneExpanded[MAX_SCENES] = {0};
static bool sceneSelected[MAX_SCENES] = {0};
static int sceneExpandedInitCount = 0;
static float outlinerScroll = 0.0f;
static float outlinerMaxScroll = 0.0f;
static bool outlinerScrollDragging = false;
static float outlinerScrollDragOffset = 0.0f;
static float objectSettingsScroll = 0.0f;
static float objectSettingsMaxScroll = 0.0f;
static bool objectSettingsScrollDragging = false;
static float objectSettingsScrollDragOffset = 0.0f;
static int objetoSelecionadoPrincipalId = -1;

static int ObterPaiId(int id);
static bool TemAncestralSelecionado(int id);
static bool EhDescendenteId(int paiId, int filhoId);
static void PushUndo(AcaoUndo acao);
static int ColetarIdsSelecionados(int *selectedIds, int maxCount);
int BuscarIndicePorId(int id);

static bool ArrasteUltrapassouLimite(Vector2 inicio, Vector2 atual)
{
    const float limite = 4.0f;
    float dx = atual.x - inicio.x;
    float dy = atual.y - inicio.y;
    return (dx * dx + dy * dy) >= (limite * limite);
}

static bool ShiftHeld(void)
{
    return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

static bool CtrlHeld(void)
{
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}

static bool outlinerScenesExpanded = true;
static bool outlinerObjectExpanded = true;

static bool DrawOutlinerSectionHeader(Rectangle header, const char *title, int textSize, bool *expanded, bool allowInput, Vector2 mouse)
{
    bool hover = CheckCollisionPointRec(mouse, header);
    bool clicked = allowInput && hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
    if (clicked)
        *expanded = !(*expanded);
    DrawBlender266CollapsibleHeader(header, title, textSize, *expanded, hover);
    return clicked;
}

static void LimparSelecaoObjetos(void)
{
    for (int i = 0; i < totalObjetos; i++)
        objetos[i].selecionado = false;
    objetoSelecionadoPrincipalId = -1;
}

static void LimparSelecaoCenas(void)
{
    for (int i = 0; i < MAX_SCENES; i++)
        sceneSelected[i] = false;
}

static int PrimeiroObjetoSelecionadoId(void)
{
    for (int i = 0; i < totalObjetos; i++)
        if (objetos[i].selecionado)
            return objetos[i].id;
    return -1;
}

static int IndexInIdList(int id, const int *ids, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (ids[i] == id)
            return i;
    }
    return -1;
}

static void ColetarSubarvoreIdsDuplicacao(int id, int *ids, int *count, int maxCount)
{
    if (!ids || !count || *count >= maxCount)
        return;
    if (IndexInIdList(id, ids, *count) != -1)
        return;

    ids[(*count)++] = id;
    for (int i = 0; i < totalObjetos; i++)
    {
        if (objetos[i].paiId == id)
            ColetarSubarvoreIdsDuplicacao(objetos[i].id, ids, count, maxCount);
    }
}

static int MapearIdDuplicado(int oldId, const int *oldIds, const int *newIds, int count)
{
    for (int i = 0; i < count; i++)
    {
        if (oldIds[i] == oldId)
            return newIds[i];
    }
    return -1;
}

static void NormalizeDuplicateNameBase(const char *sourceName, char *outBase, size_t outSize)
{
    if (!outBase || outSize == 0)
        return;
    outBase[0] = '\0';

    const char *fallback = (sourceName && sourceName[0] != '\0') ? sourceName : "Object";
    strncpy(outBase, fallback, outSize - 1);
    outBase[outSize - 1] = '\0';

    int len = (int)strlen(outBase);
    while (len > 0 && outBase[len - 1] == ' ')
        outBase[--len] = '\0';

    // Limpa nomes legados como "Cube Copy" e "Cube Copy 2".
    bool removedCopy = true;
    while (removedCopy && len > 0)
    {
        removedCopy = false;
        int end = len;
        while (end > 0 && isdigit((unsigned char)outBase[end - 1]))
            end--;
        if (end < len && end > 0 && outBase[end - 1] == ' ')
        {
            len = end - 1;
            outBase[len] = '\0';
            while (len > 0 && outBase[len - 1] == ' ')
                outBase[--len] = '\0';
        }

        if (len >= 5 && strcmp(&outBase[len - 5], " Copy") == 0)
        {
            len -= 5;
            outBase[len] = '\0';
            while (len > 0 && outBase[len - 1] == ' ')
                outBase[--len] = '\0';
            removedCopy = true;
        }
    }

    // Se o nome ja termina com numero, trata esse numero como sufixo de instancia.
    int end = len;
    while (end > 0 && isdigit((unsigned char)outBase[end - 1]))
        end--;
    if (end < len && end > 0 && outBase[end - 1] == ' ')
    {
        len = end - 1;
        outBase[len] = '\0';
        while (len > 0 && outBase[len - 1] == ' ')
            outBase[--len] = '\0';
    }

    if (outBase[0] == '\0')
    {
        strncpy(outBase, "Object", outSize - 1);
        outBase[outSize - 1] = '\0';
    }
}

static void BuildDuplicateObjectName(const char *sourceName, char *outName, size_t outSize)
{
    if (!outName || outSize == 0)
        return;
    outName[0] = '\0';

    char base[32] = {0};
    NormalizeDuplicateNameBase(sourceName, base, sizeof(base));

    int highestSuffix = -1;
    for (int i = 0; i < totalObjetos; i++)
    {
        const char *name = objetos[i].nome;
        size_t baseLen = strlen(base);
        if (strcmp(name, base) == 0)
        {
            if (highestSuffix < 0)
                highestSuffix = 0;
            continue;
        }
        if (strncmp(name, base, baseLen) != 0)
            continue;
        if (name[baseLen] != ' ')
            continue;

        const char *suffix = name + baseLen + 1;
        if (*suffix == '\0')
            continue;
        bool allDigits = true;
        for (int c = 0; suffix[c] != '\0'; c++)
        {
            if (!isdigit((unsigned char)suffix[c]))
            {
                allDigits = false;
                break;
            }
        }
        if (!allDigits)
            continue;

        int value = atoi(suffix);
        if (value > highestSuffix)
            highestSuffix = value;
    }

    if (highestSuffix < 0)
    {
        strncpy(outName, base, outSize - 1);
        outName[outSize - 1] = '\0';
        if (!ObjetoExisteNoOutliner(outName))
            return;
        highestSuffix = 0;
    }

    for (int i = highestSuffix + 1; i < 1000; i++)
    {
        snprintf(outName, outSize, "%s %d", base, i);
        outName[outSize - 1] = '\0';
        if (!ObjetoExisteNoOutliner(outName))
            return;
    }

    snprintf(outName, outSize, "Object %d", ObterProximoId());
    outName[outSize - 1] = '\0';
}

static int ColetarIdsSelecionados(int *selectedIds, int maxCount)
{
    if (!selectedIds || maxCount <= 0)
        return 0;

    int selectedCount = 0;
    for (int i = 0; i < totalObjetos && selectedCount < maxCount; i++)
    {
        if (!objetos[i].selecionado)
            continue;
        selectedIds[selectedCount++] = objetos[i].id;
    }
    return selectedCount;
}

static int ColetarRaizesSelecionadas(int *rootIds, int maxCount)
{
    if (!rootIds || maxCount <= 0)
        return 0;

    int rootCount = 0;
    for (int i = 0; i < totalObjetos && rootCount < maxCount; i++)
    {
        if (!objetos[i].selecionado)
            continue;
        if (TemAncestralSelecionado(objetos[i].id))
            continue;
        rootIds[rootCount++] = objetos[i].id;
    }
    return rootCount;
}

static int ColetarIdsDeArraste(int draggedId, int *dragIds, int maxCount)
{
    if (!dragIds || maxCount <= 0)
        return 0;

    int draggedIdx = BuscarIndicePorId(draggedId);
    if (draggedIdx == -1)
        return 0;

    if (!objetos[draggedIdx].selecionado)
    {
        dragIds[0] = draggedId;
        return 1;
    }

    int dragCount = ColetarRaizesSelecionadas(dragIds, maxCount);
    if (dragCount <= 0)
    {
        dragIds[0] = draggedId;
        return 1;
    }

    return dragCount;
}

static bool PodeSoltarGrupoNoAlvo(const int *dragIds, int dragCount, int targetId)
{
    if (!dragIds || dragCount <= 0 || targetId <= 0)
        return false;

    for (int i = 0; i < dragCount; i++)
    {
        if (dragIds[i] <= 0)
            continue;
        if (dragIds[i] == targetId || EhDescendenteId(dragIds[i], targetId))
            return false;
    }

    return true;
}

static bool AlgumObjetoDaListaTemPai(const int *ids, int count)
{
    if (!ids || count <= 0)
        return false;

    for (int i = 0; i < count; i++)
    {
        int idx = BuscarIndicePorId(ids[i]);
        if (idx != -1 && objetos[idx].paiId != -1)
            return true;
    }

    return false;
}

static int DesparentarObjetosPorIds(const int *ids, int count)
{
    if (!ids || count <= 0)
        return 0;

    int changedCount = 0;
    for (int i = 0; i < count; i++)
    {
        int idx = BuscarIndicePorId(ids[i]);
        if (idx == -1 || objetos[idx].paiId == -1)
            continue;

        PushUndo((AcaoUndo){
            ACAO_REPARENT,
            ids[i],
            objetos[idx].paiId,
            {0}});

        objetos[idx].paiId = -1;
        changedCount++;
    }

    return changedCount;
}

static void DesparentarObjetosDoContexto(int contextId)
{
    int idxContext = BuscarIndicePorId(contextId);
    if (idxContext == -1)
        return;

    if (!objetos[idxContext].selecionado)
    {
        DesparentarObjetosPorIds(&contextId, 1);
        return;
    }

    int selectedIds[MAX_OBJETOS] = {0};
    int selectedCount = ColetarIdsSelecionados(selectedIds, MAX_OBJETOS);
    if (selectedCount <= 0)
        return;

    DesparentarObjetosPorIds(selectedIds, selectedCount);
}

static void MoverObjetosDoContextoParaCena(int contextId, int cenaIndex)
{
    int idxContext = BuscarIndicePorId(contextId);
    if (idxContext == -1)
        return;

    int moveIds[MAX_OBJETOS] = {0};
    int moveCount = 0;

    if (objetos[idxContext].selecionado)
        moveCount = ColetarRaizesSelecionadas(moveIds, MAX_OBJETOS);

    if (moveCount <= 0)
    {
        moveIds[0] = contextId;
        moveCount = 1;
    }

    for (int i = 0; i < moveCount; i++)
        MoveObjetoParaCena(moveIds[i], cenaIndex);
}

static bool DuplicarObjetosPorLista(const int *sourceIds, int sourceCount,
                                    const int *selectionIds, int selectionCount,
                                    int activeSourceId, Vector3 deslocamento)
{
    if (!sourceIds || sourceCount <= 0)
        return false;

    int duplicateModelCount = 0;
    int validSourceCount = 0;

    for (int i = 0; i < sourceCount; i++)
    {
        int idx = BuscarIndicePorId(sourceIds[i]);
        if (idx == -1)
            continue;

        PropertiesSyncToObjeto(&objetos[idx]);
        if (objetos[idx].caminhoModelo[0] != '\0')
            duplicateModelCount++;
        validSourceCount++;
    }

    if (validSourceCount <= 0)
        return false;
    if (totalObjetos + validSourceCount > MAX_OBJETOS)
    {
        TraceLog(LOG_WARNING, "Nao foi possivel duplicar: limite maximo de objetos atingido.");
        return false;
    }

    if (modelManager.modelCount + duplicateModelCount > MAX_MODELS)
    {
        TraceLog(LOG_WARNING, "Nao foi possivel duplicar: limite maximo de modelos ativos atingido.");
        return false;
    }

    int oldIds[MAX_OBJETOS] = {0};
    int newIds[MAX_OBJETOS] = {0};
    int createdCount = 0;

    for (int i = 0; i < sourceCount; i++)
    {
        int idx = BuscarIndicePorId(sourceIds[i]);
        if (idx == -1)
            continue;

        ObjetoCena src = objetos[idx];
        char duplicateName[32] = {0};
        BuildDuplicateObjectName(src.nome, duplicateName, sizeof(duplicateName));

        Vector3 newPos = {
            src.posicao.x + deslocamento.x,
            src.posicao.y + deslocamento.y,
            src.posicao.z + deslocamento.z};

        int newId = RegistrarObjeto(duplicateName, newPos, -1);
        int newIdx = BuscarIndicePorId(newId);
        if (newId <= 0 || newIdx == -1)
            continue;

        objetos[newIdx] = src;
        objetos[newIdx].id = newId;
        strncpy(objetos[newIdx].nome, duplicateName, MAX_NOME);
        objetos[newIdx].nome[MAX_NOME] = '\0';
        objetos[newIdx].posicao = newPos;
        objetos[newIdx].paiId = -1;
        objetos[newIdx].selecionado = false;

        oldIds[createdCount] = src.id;
        newIds[createdCount] = newId;
        createdCount++;
    }

    if (createdCount <= 0)
        return false;

    for (int i = 0; i < createdCount; i++)
    {
        int srcIdx = BuscarIndicePorId(oldIds[i]);
        int dstIdx = BuscarIndicePorId(newIds[i]);
        if (srcIdx == -1 || dstIdx == -1)
            continue;

        int mappedParent = MapearIdDuplicado(objetos[srcIdx].paiId, oldIds, newIds, createdCount);
        objetos[dstIdx].paiId = (mappedParent != -1) ? mappedParent : -1;

        PropertiesSyncFromObjeto(&objetos[dstIdx]);
        if (objetos[dstIdx].caminhoModelo[0] != '\0')
            CarregarModeloParaObjeto(objetos[dstIdx].caminhoModelo, objetos[dstIdx].nome, objetos[dstIdx].id);
    }

    LimparSelecaoObjetos();

    int firstSelectedDuplicateId = -1;
    for (int i = 0; i < selectionCount; i++)
    {
        int duplicatedId = MapearIdDuplicado(selectionIds[i], oldIds, newIds, createdCount);
        int idx = BuscarIndicePorId(duplicatedId);
        if (idx == -1)
            continue;

        objetos[idx].selecionado = true;
        if (firstSelectedDuplicateId == -1)
            firstSelectedDuplicateId = duplicatedId;
    }

    int duplicatedActiveId = MapearIdDuplicado(activeSourceId, oldIds, newIds, createdCount);
    int activeIdx = BuscarIndicePorId(duplicatedActiveId);
    if (activeIdx != -1 && objetos[activeIdx].selecionado)
        objetoSelecionadoPrincipalId = duplicatedActiveId;
    else if (firstSelectedDuplicateId != -1)
        objetoSelecionadoPrincipalId = firstSelectedDuplicateId;
    else
    {
        int idx = BuscarIndicePorId(newIds[0]);
        if (idx != -1)
        {
            objetos[idx].selecionado = true;
            objetoSelecionadoPrincipalId = newIds[0];
        }
    }

    SetSelectedModelByObjetoId(ObterObjetoSelecionadoId());
    return true;
}

static bool DuplicarObjetosPorRaizes(const int *rootIds, int rootCount, Vector3 deslocamento)
{
    if (!rootIds || rootCount <= 0)
        return false;

    int sourceIds[MAX_OBJETOS] = {0};
    int sourceCount = 0;

    for (int i = 0; i < rootCount; i++)
    {
        if (BuscarIndicePorId(rootIds[i]) == -1)
            continue;
        ColetarSubarvoreIdsDuplicacao(rootIds[i], sourceIds, &sourceCount, MAX_OBJETOS);
    }

    if (sourceCount <= 0)
        return false;

    int activeRootId = rootIds[0];
    if (IndexInIdList(objetoSelecionadoPrincipalId, rootIds, rootCount) != -1)
        activeRootId = objetoSelecionadoPrincipalId;

    return DuplicarObjetosPorLista(sourceIds, sourceCount, rootIds, rootCount, activeRootId, deslocamento);
}

Vector3 ObterDeslocamentoPadraoDuplicacao(void)
{
    return (Vector3){0.0f, 0.0f, 0.0f};
}

static bool DuplicarObjetosDoContexto(int contextId, Vector3 deslocamento)
{
    int idxContext = BuscarIndicePorId(contextId);
    if (idxContext == -1)
        return false;

    int selectedIds[MAX_OBJETOS] = {0};
    int selectedCount = ColetarIdsSelecionados(selectedIds, MAX_OBJETOS);

    // Mantem o comportamento da viewport: se ha multiseleção ativa,
    // a duplicação sempre considera o conjunto selecionado inteiro.
    if (selectedCount > 1 && objetos[idxContext].selecionado)
        return DuplicarObjetosPorLista(selectedIds, selectedCount, selectedIds, selectedCount,
                                       objetoSelecionadoPrincipalId, deslocamento);

    // Se o objeto do menu faz parte da seleção atual, duplica essa seleção.
    if (selectedCount == 1 && objetos[idxContext].selecionado)
        return DuplicarObjetosPorRaizes(selectedIds, selectedCount, deslocamento);

    // Caso contrário, duplica apenas o objeto contextual.
    selectedIds[0] = contextId;
    selectedCount = 1;

    return DuplicarObjetosPorRaizes(selectedIds, selectedCount, deslocamento);
}

static void DeleteSelectedScenesFromContext(int contextIndex)
{
    int sceneCount = GetSceneCount();
    if (contextIndex < 0 || contextIndex >= sceneCount)
        return;

    bool contextSelected = (contextIndex < MAX_SCENES && sceneSelected[contextIndex]);
    if (!contextSelected)
    {
        DeleteScene(contextIndex);
        return;
    }

    int selectedIndices[MAX_SCENES] = {0};
    int selectedCount = 0;
    for (int i = 0; i < sceneCount && i < MAX_SCENES; i++)
    {
        if (sceneSelected[i])
            selectedIndices[selectedCount++] = i;
    }

    if (selectedCount <= 1)
    {
        DeleteScene(contextIndex);
        return;
    }

    for (int i = selectedCount - 1; i >= 0; i--)
        DeleteScene(selectedIndices[i]);

    for (int i = 0; i < MAX_SCENES; i++)
        sceneSelected[i] = false;
    int active = GetActiveSceneIndex();
    if (active >= 0 && active < MAX_SCENES)
        sceneSelected[active] = true;
}

static bool IdEstaSelecionado(int id)
{
    int idx = BuscarIndicePorId(id);
    return (idx != -1 && objetos[idx].selecionado);
}

static bool TemAncestralSelecionado(int id)
{
    int pai = ObterPaiId(id);
    while (pai != -1)
    {
        if (IdEstaSelecionado(pai))
            return true;
        pai = ObterPaiId(pai);
    }
    return false;
}

static void DeleteSelectedObjectsFromContext(int contextId)
{
    int idxContext = BuscarIndicePorId(contextId);
    if (idxContext == -1)
        return;

    if (!objetos[idxContext].selecionado)
    {
        RemoverObjetoRecursivoId(contextId);
        return;
    }

    int selectedIds[MAX_OBJETOS] = {0};
    int selectedCount = 0;
    for (int i = 0; i < totalObjetos; i++)
    {
        if (objetos[i].selecionado)
            selectedIds[selectedCount++] = objetos[i].id;
    }

    if (selectedCount <= 1)
    {
        RemoverObjetoRecursivoId(contextId);
        return;
    }

    int rootIds[MAX_OBJETOS] = {0};
    int rootCount = 0;
    for (int i = 0; i < selectedCount; i++)
    {
        int id = selectedIds[i];
        if (!TemAncestralSelecionado(id))
            rootIds[rootCount++] = id;
    }

    for (int i = 0; i < rootCount; i++)
    {
        if (BuscarIndicePorId(rootIds[i]) != -1)
            RemoverObjetoRecursivoId(rootIds[i]);
    }

    objetoSelecionadoPrincipalId = -1;
    SetSelectedModelByObjetoId(-1);
}

static bool TemIrmaoDepois(int id)
{
    int idx = BuscarIndicePorId(id);
    if (idx == -1)
        return false;

    int paiId = objetos[idx].paiId;
    for (int i = idx + 1; i < totalObjetos; i++)
        if (objetos[i].paiId == paiId)
            return true;
    return false;
}

static int ObterPaiId(int id)
{
    int idx = BuscarIndicePorId(id);
    if (idx == -1)
        return -1;
    return objetos[idx].paiId;
}

void InitOutliner(void)
{
    totalObjetos = 0;
    proximoId = 1;
    undoTopo = 0;
    menuAtivo = false;
    arrastando = false;
    cliqueSemShiftPendenteSelecaoUnica = false;
    arrasteInicioMouse = (Vector2){0};
    renomeando = false;
    outlinerScroll = 0.0f;
    outlinerMaxScroll = 0.0f;
    outlinerScrollDragging = false;
    outlinerScrollDragOffset = 0.0f;
    objectSettingsScroll = 0.0f;
    objectSettingsMaxScroll = 0.0f;
    objectSettingsScrollDragging = false;
    objectSettingsScrollDragOffset = 0.0f;
    TextInputInit(&renameInput);
    TextInputInit(&propNameInput);
    TextInputInit(&sceneNameInput);
    LimparSelecaoCenas();
    objetoSelecionadoPrincipalId = -1;
}

int BuscarIndicePorId(int id)
{
    for (int i = 0; i < totalObjetos; i++)
        if (objetos[i].id == id)
            return i;
    return -1;
}

int RegistrarObjeto(const char *nome, Vector3 posicao, int paiId)
{
    if (totalObjetos >= MAX_OBJETOS)
        return -1;

    ObjetoCena *obj = &objetos[totalObjetos];
    obj->id = proximoId++;
    strncpy(obj->nome, nome, MAX_NOME);
    obj->nome[MAX_NOME] = '\0';
    obj->posicao = posicao;
    obj->rotacao = (Vector3){0, 0, 0};
    obj->paiId = paiId;
    obj->ativo = true;
    obj->selecionado = false;
    obj->protoEnabled = false;
    obj->protoBaseColor = (Color){112, 112, 112, 255};
    obj->protoSecondaryColor = (Color){58, 58, 58, 255};
    obj->protoPack = 0;
    strncpy(obj->protoCustomName, "Custom", sizeof(obj->protoCustomName) - 1);
    obj->protoCustomName[sizeof(obj->protoCustomName) - 1] = '\0';
    obj->protoCustomBase = DefaultPrototypeCustomBaseColor();
    obj->protoCustomSecondary = DefaultPrototypeCustomSecondaryColor();
    obj->protoCustomCount = 0;
    obj->physStatic = true;
    obj->physRigidbody = false;
    obj->physCollider = true;
    obj->physGravity = true;
    obj->physTerrain = false;
    obj->physMass = 1.0f;
    obj->physShape = COLLISION_SHAPE_MESH_BOUNDS;
    obj->physSize = (Vector3){1.0f, 1.0f, 1.0f};

    totalObjetos++;
    return obj->id;
}

static bool EhDescendenteId(int paiId, int filhoId)
{
    int idx = BuscarIndicePorId(filhoId);
    while (idx != -1 && objetos[idx].paiId != -1)
    {
        if (objetos[idx].paiId == paiId)
            return true;
        idx = BuscarIndicePorId(objetos[idx].paiId);
    }
    return false;
}

static void PushUndo(AcaoUndo acao)
{
    if (undoTopo < MAX_UNDO)
        undoStack[undoTopo++] = acao;
}

void Undo(void)
{
    if (undoTopo <= 0)
        return;

    AcaoUndo acao = undoStack[--undoTopo];

    if (acao.tipo == ACAO_REPARENT)
    {
        int idx = BuscarIndicePorId(acao.objetoId);
        if (idx != -1)
            objetos[idx].paiId = acao.paiAntigo;
    }
    else if (acao.tipo == ACAO_DELETE)
    {
        if (totalObjetos < MAX_OBJETOS)
        {
            objetos[totalObjetos++] = acao.backup;

            // Restaurar o modelo 3D se havia um
            if (strlen(acao.backup.caminhoModelo) > 0)
            {
                extern void RestaurarModeloPorFilepath(const char *filepath, const char *nome);
                RestaurarModeloPorFilepath(acao.backup.caminhoModelo, acao.backup.nome);
            }
        }
    }
}

static void RemoverObjetoDiretoIndice(int index)
{
    for (int i = index; i < totalObjetos - 1; i++)
        objetos[i] = objetos[i + 1];
    totalObjetos--;
}

void RemoverObjetoRecursivoId(int id)
{
    int index = BuscarIndicePorId(id);
    if (index == -1)
        return;

    // Remover filhos primeiro
    for (int i = totalObjetos - 1; i >= 0; i--)
        if (objetos[i].paiId == id)
            RemoverObjetoRecursivoId(objetos[i].id);

    PushUndo((AcaoUndo){ACAO_DELETE, id, -1, objetos[index]});
    RemoverObjetoDiretoIndice(index);

    // Remover do model manager também
    extern void RemoverModeloPorIdObjeto(int idObjeto);
    RemoverModeloPorIdObjeto(id);
}

static int DrawOutlinerItem(int id, int nivel, int y)
{
    int index = BuscarIndicePorId(id);
    if (index == -1)
        return y;

    float baseX = (float)(12 + nivel * 16);
    Rectangle item = {
        baseX,
        (float)y,
        (float)(PAINEL_LARGURA - 24 - nivel * 16),
        22.0f};
    Vector2 mouse = GetMousePosition();
    bool hoverItem = (!renomeando && !bloquearCliqueOutliner && CheckCollisionPointRec(mouse, item));

    Color fundo = COR_PAINEL;
    bool selecionado = objetos[index].selecionado;
    bool selecionadoPrincipal = selecionado && (id == objetoSelecionadoPrincipalId);
    if (id == alvoArrasteId && arrastando)
        fundo = COR_ITEM_DRAG;
    else if (selecionadoPrincipal)
        fundo = GetUIStyle()->accent;
    else if (selecionado)
        fundo = GetUIStyle()->accentSoft;
    else if (hoverItem)
        fundo = GetUIStyle()->itemHover;

    DrawRectangleRec(item, fundo);
    if (hoverItem && !selecionado)
        DrawRectangleLinesEx(item, 1, GetUIStyle()->accentSoft);

    // Linhas de hierarquia (pai/filho) estilo IDE
    if (nivel > 0)
    {
        const int INDENT = 16;
        const int LINE_OFFSET = 6;
        int midY = (int)(item.y + item.height / 2);

        int ancestors[MAX_OBJETOS] = {0};
        int ancCount = 0;
        int cur = id;
        while (true)
        {
            int pai = ObterPaiId(cur);
            if (pai == -1 || ancCount >= MAX_OBJETOS)
                break;
            ancestors[ancCount++] = pai;
            cur = pai;
        }

        // Desenhar verticais dos ancestrais que possuem irmãos abaixo
        for (int a = ancCount - 1, level = 0; a >= 0; a--, level++)
        {
            int ancId = ancestors[a];
            if (!TemIrmaoDepois(ancId))
                continue;

            int lineX = 12 + level * INDENT + LINE_OFFSET;
            DrawLine(lineX, (int)item.y, lineX, (int)(item.y + item.height), COR_BORDA);
        }

        // Conector do item atual
        int lineX = 12 + (nivel - 1) * INDENT + LINE_OFFSET;
        bool isLast = !TemIrmaoDepois(id);
        DrawLine(lineX, (int)item.y, lineX, isLast ? midY : (int)(item.y + item.height), COR_BORDA);
        DrawLine(lineX, midY, (int)baseX, midY, COR_BORDA);
    }

    if (renomeando && renomearObjetoId == id)
    {
        caixaEdicao = item;
        TextInputConfig cfg = {0};
        cfg.fontSize = 14;
        cfg.padding = 4;
        cfg.textColor = COR_TEXTO;
        cfg.bgColor = COR_EDIT_BG;
        cfg.borderColor = COR_BORDA;
        cfg.selectionColor = GetUIStyle()->inputSelection;
        cfg.caretColor = GetUIStyle()->caret;
        cfg.allowInput = true;

        if (IsKeyPressed(KEY_ESCAPE))
        {
            renomeando = false;
            renomearObjetoId = -1;
            TextInputInit(&renameInput);
        }
        else
        {
            int flags = TextInputDraw(caixaEdicao, bufferNome, (int)sizeof(bufferNome), &renameInput, &cfg);
            if (flags & (TEXT_INPUT_SUBMITTED | TEXT_INPUT_DEACTIVATED))
            {
                int idxRename = BuscarIndicePorId(renomearObjetoId);
                if (idxRename != -1 && bufferNome[0] != '\0')
                {
                    strncpy(objetos[idxRename].nome, bufferNome, MAX_NOME);
                    objetos[idxRename].nome[MAX_NOME] = '\0';
                }
                renomeando = false;
                renomearObjetoId = -1;
                TextInputInit(&renameInput);
            }
        }
    }
    else
    {
        DrawText(objetos[index].nome, (int)item.x + 6, y + 4, 14, UiTextForBackground(fundo));
    }

    if (hoverItem)
    {
        alvoArrasteId = id;

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            if (ShiftHeld())
            {
                objetos[index].selecionado = !objetos[index].selecionado;
                if (objetos[index].selecionado)
                    objetoSelecionadoPrincipalId = id;
                else if (objetoSelecionadoPrincipalId == id)
                    objetoSelecionadoPrincipalId = PrimeiroObjetoSelecionadoId();
                SetSelectedModelByObjetoId(ObterObjetoSelecionadoId());
                arrastando = false;
                objetoArrastadoId = -1;
                cliqueSemShiftPendenteSelecaoUnica = false;
            }
            else
            {
                int selectedIds[MAX_OBJETOS] = {0};
                int selectedCount = ColetarIdsSelecionados(selectedIds, MAX_OBJETOS);
                bool cliqueEmSelecionadoComGrupo = objetos[index].selecionado && selectedCount > 1;

                if (!cliqueEmSelecionadoComGrupo)
                    SelecionarObjetoPorId(id);
                else
                    objetoSelecionadoPrincipalId = id;
                SetSelectedModelByObjetoId(id);
                objetoArrastadoId = id;
                arrastando = true;
                arrasteInicioMouse = mouse;
                cliqueSemShiftPendenteSelecaoUnica = cliqueEmSelecionadoComGrupo;
            }
        }

        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
        {
            if (!objetos[index].selecionado)
                SelecionarObjetoPorId(id);
            else
                objetoSelecionadoPrincipalId = id;
            SetSelectedModelByObjetoId(id);
            arrastando = false;
            objetoArrastadoId = -1;
            cliqueSemShiftPendenteSelecaoUnica = false;
            menuAtivo = true;
            menuObjetoId = id;
            menuPos = mouse;
        }
    }

    y += 26;

    for (int i = 0; i < totalObjetos; i++)
        if (objetos[i].paiId == id)
            y = DrawOutlinerItem(objetos[i].id, nivel + 1, y);

    return y;
}

static void ProcessarDragDrop(void)
{
    if (arrastando && IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
    {
        bool dragConfirmado = ArrasteUltrapassouLimite(arrasteInicioMouse, GetMousePosition());

        if (cliqueSemShiftPendenteSelecaoUnica && !dragConfirmado)
        {
            SelecionarObjetoPorId(objetoArrastadoId);
            SetSelectedModelByObjetoId(objetoArrastadoId);
            arrastando = false;
            objetoArrastadoId = -1;
            alvoArrasteId = -1;
            cliqueSemShiftPendenteSelecaoUnica = false;
            return;
        }

        int dragIds[MAX_OBJETOS] = {0};
        int dragCount = ColetarIdsDeArraste(objetoArrastadoId, dragIds, MAX_OBJETOS);

        if (dropSceneIndex != -1)
        {
            if (dropSceneIndex == GetActiveSceneIndex())
                DesparentarObjetosPorIds(dragIds, dragCount);
            else
            {
                for (int i = 0; i < dragCount; i++)
                    MoveObjetoParaCena(dragIds[i], dropSceneIndex);
            }
            arrastando = false;
            objetoArrastadoId = -1;
            alvoArrasteId = -1;
            dropSceneIndex = -1;
            cliqueSemShiftPendenteSelecaoUnica = false;
            return;
        }

        if (dragConfirmado && PodeSoltarGrupoNoAlvo(dragIds, dragCount, alvoArrasteId))
        {
            for (int i = 0; i < dragCount; i++)
            {
                int idx = BuscarIndicePorId(dragIds[i]);
                if (idx == -1 || objetos[idx].paiId == alvoArrasteId)
                    continue;

                PushUndo((AcaoUndo){
                    ACAO_REPARENT,
                    dragIds[i],
                    objetos[idx].paiId,
                    {0}});

                objetos[idx].paiId = alvoArrasteId;
            }
        }

        arrastando = false;
        objetoArrastadoId = -1;
        alvoArrasteId = -1;
        cliqueSemShiftPendenteSelecaoUnica = false;
    }
}

static void DrawContextMenu(void)
{
    const float itemH = 26.0f;
    const Vector3 duplicateOffset = ObterDeslocamentoPadraoDuplicacao();
    int idxMenu = BuscarIndicePorId(menuObjetoId);
    int selectedIds[MAX_OBJETOS] = {0};
    int selectedCount = ColetarIdsSelecionados(selectedIds, MAX_OBJETOS);
    bool contextUsesSelection = (idxMenu != -1 && objetos[idxMenu].selecionado && selectedCount > 1);
    bool canUnparent = false;
    if (contextUsesSelection)
        canUnparent = AlgumObjetoDaListaTemPai(selectedIds, selectedCount);
    else
        canUnparent = (idxMenu != -1 && objetos[idxMenu].paiId != -1);
    int menuItems = canUnparent ? 5 : 4;
    Rectangle menu = {(float)menuPos.x, (float)menuPos.y, 190.0f, itemH * menuItems};
    Rectangle itemRename = {menu.x, menu.y, menu.width, itemH};
    Rectangle itemDuplicate = {menu.x, menu.y + itemH, menu.width, itemH};
    Rectangle itemMove = {menu.x, menu.y + itemH * 2.0f, menu.width, itemH};
    Rectangle itemUnparent = {menu.x, menu.y + itemH * 3.0f, menu.width, itemH};
    Rectangle itemDelete = {menu.x, menu.y + itemH * (canUnparent ? 4.0f : 3.0f), menu.width, itemH};

    Vector2 mouse = GetMousePosition();
    const UIStyle *style = GetUIStyle();

    DrawRectangleRec(menu, COR_MENU);

    bool hoverRename = CheckCollisionPointRec(mouse, itemRename);
    bool hoverDuplicate = CheckCollisionPointRec(mouse, itemDuplicate);
    bool hoverMove = CheckCollisionPointRec(mouse, itemMove);
    bool hoverUnparent = canUnparent && CheckCollisionPointRec(mouse, itemUnparent);
    bool hoverDelete = CheckCollisionPointRec(mouse, itemDelete);
    if (hoverRename)
        DrawRectangleRec(itemRename, style->accent);
    if (hoverDuplicate)
        DrawRectangleRec(itemDuplicate, style->accent);
    if (hoverMove)
        DrawRectangleRec(itemMove, style->accent);
    if (hoverUnparent)
        DrawRectangleRec(itemUnparent, style->accent);
    if (hoverDelete)
        DrawRectangleRec(itemDelete, style->accent);

    DrawText("Renomear", (int)itemRename.x + 10, (int)itemRename.y + 6, 14, hoverRename ? style->buttonTextHover : COR_TEXTO);
    DrawText("Duplicar", (int)itemDuplicate.x + 10, (int)itemDuplicate.y + 6, 14, hoverDuplicate ? style->buttonTextHover : COR_TEXTO);
    DrawText("Mover para Cena", (int)itemMove.x + 10, (int)itemMove.y + 6, 14, hoverMove ? style->buttonTextHover : COR_TEXTO);
    if (canUnparent)
        DrawText("Desparentar", (int)itemUnparent.x + 10, (int)itemUnparent.y + 6, 14, hoverUnparent ? style->buttonTextHover : COR_TEXTO);
    DrawText("Deletar", (int)itemDelete.x + 10, (int)itemDelete.y + 6, 14, hoverDelete ? style->buttonTextHover : COR_TEXTO);

    Rectangle submenu = {menu.x + menu.width + 2, itemMove.y, 170.0f, (float)(GetSceneCount() * 22)};
    menuMoverHoverScene = -1;
    bool mouseNoSubmenu = CheckCollisionPointRec(mouse, submenu);
    if (CheckCollisionPointRec(mouse, itemMove) || mouseNoSubmenu)
        menuMoverCenaAtivo = true;
    else
        menuMoverCenaAtivo = false;

    if (menuMoverCenaAtivo)
    {
        DrawRectangleRec(submenu, COR_MENU);

        for (int i = 0; i < GetSceneCount(); i++)
        {
            Rectangle item = {submenu.x, submenu.y + i * 22.0f, submenu.width, 22.0f};
            bool hover = CheckCollisionPointRec(mouse, item);
            if (hover)
            {
                menuMoverHoverScene = i;
                DrawRectangleRec(item, style->accent);
            }

            Color cor = hover ? style->buttonTextHover : ((i == GetActiveSceneIndex()) ? COR_ITEM : COR_TEXTO);
            DrawText(GetSceneName(i), (int)item.x + 6, (int)item.y + 4, 12, cor);
        }
    }

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        if (CheckCollisionPointRec(mouse, itemRename))
        {
            int idx = BuscarIndicePorId(menuObjetoId);
            if (idx != -1)
            {
                strncpy(bufferNome, objetos[idx].nome, MAX_NOME);
                bufferNome[MAX_NOME] = '\0';
                renomeando = true;
                renomearObjetoId = menuObjetoId;
                TextInputInit(&renameInput);
                renameInput.active = true;
                renameInput.caret = (int)strlen(bufferNome);
                renameInput.selStart = 0;
                renameInput.selEnd = renameInput.caret;
            }
            menuAtivo = false;
        }
        else if (CheckCollisionPointRec(mouse, itemDuplicate))
        {
            DuplicarObjetosDoContexto(menuObjetoId, duplicateOffset);
            menuAtivo = false;
        }
        else if (menuMoverCenaAtivo && menuMoverHoverScene != -1 && menuMoverHoverScene != GetActiveSceneIndex())
        {
            MoverObjetosDoContextoParaCena(menuObjetoId, menuMoverHoverScene);
            menuAtivo = false;
        }
        else if (canUnparent && CheckCollisionPointRec(mouse, itemUnparent))
        {
            DesparentarObjetosDoContexto(menuObjetoId);
            menuAtivo = false;
        }
        else if (CheckCollisionPointRec(mouse, itemDelete))
        {
            DeleteSelectedObjectsFromContext(menuObjetoId);
            menuAtivo = false;
        }
        else if (!CheckCollisionPointRec(mouse, menu) && !(menuMoverCenaAtivo && mouseNoSubmenu))
        {
            menuAtivo = false;
        }
    }
}

static void SincronizarNomeSelecionadoProp(int id, const char *nomeAtual)
{
    if (id != propUltimoSelecionadoId)
    {
        propUltimoSelecionadoId = id;
        strncpy(propBufferNome, nomeAtual, MAX_NOME);
        propBufferNome[MAX_NOME] = '\0';
        TextInputInit(&propNameInput);
        propNameInput.active = false;
        propNameInput.caret = (int)strlen(propBufferNome);
        propNameInput.selStart = propNameInput.caret;
        propNameInput.selEnd = propNameInput.caret;
        return;
    }
    if (!propNameInput.active && strcmp(propBufferNome, nomeAtual) != 0)
    {
        strncpy(propBufferNome, nomeAtual, MAX_NOME);
        propBufferNome[MAX_NOME] = '\0';
        TextInputInit(&propNameInput);
        propNameInput.active = false;
        propNameInput.caret = (int)strlen(propBufferNome);
        propNameInput.selStart = propNameInput.caret;
        propNameInput.selEnd = propNameInput.caret;
    }
}

static void DrawObjectSettings(float startY, float height)
{
    const UIStyle *style = GetUIStyle();
    Color secondaryWhite = Fade(style->textPrimary, 0.72f);
    Vector2 mouse = GetMousePosition();
    int screenH = GetScreenHeight();
    if (height <= 0 || startY >= screenH)
        return;

    Rectangle area = {0, startY, (float)PAINEL_LARGURA, height};
    DrawRectangleRec(area, COR_PAINEL);
    DrawLine(0, (int)startY, PAINEL_LARGURA, (int)startY, COR_BORDA);

    float contentTop = startY + 4.0f;
    float viewH = height - 8.0f;
    if (viewH < 1.0f)
        viewH = 1.0f;
    if (objectSettingsScroll < 0.0f)
        objectSettingsScroll = 0.0f;
    if (objectSettingsScroll > objectSettingsMaxScroll)
        objectSettingsScroll = objectSettingsMaxScroll;

    BeginScissorMode(0, (int)contentTop, PAINEL_LARGURA, (int)viewH);

    int x = 0;
    int y = (int)(startY + 8.0f - objectSettingsScroll);
    Rectangle objectHeader = {(float)(x + 6), (float)y, (float)(PAINEL_LARGURA - 16), 22.0f};
    DrawOutlinerSectionHeader(objectHeader, "Objeto", 13, &outlinerObjectExpanded, true, mouse);
    y += 24;
    if (!outlinerObjectExpanded)
    {
        EndScissorMode();
        objectSettingsMaxScroll = 0.0f;
        objectSettingsScroll = 0.0f;
        objectSettingsScrollDragging = false;
        return;
    }

    int selecionadoId = ObterObjetoSelecionadoId();
    if (selecionadoId <= 0)
    {
        DrawText("Nenhum objeto selecionado", x + 14, y, 14, secondaryWhite);
        y += 20;
    }
    else
    {
        int idx = BuscarIndicePorId(selecionadoId);
        if (idx == -1)
        {
            DrawText("Selecao invalida", x + 14, y, 14, secondaryWhite);
            y += 20;
        }
        else
        {
            SincronizarNomeSelecionadoProp(selecionadoId, objetos[idx].nome);
            int selectedIds[MAX_OBJETOS] = {0};
            int selectedCount = ColetarIdsSelecionados(selectedIds, MAX_OBJETOS);
            bool selectedHasAnyParent = AlgumObjetoDaListaTemPai(selectedIds, selectedCount);

            DrawText("Nome", x + 14, y, 14, COR_TEXTO);
            y += 18;

            Rectangle propCaixaNome = {(float)(x + 14), (float)y, (float)(PAINEL_LARGURA - 28), 22.0f};
            TextInputConfig nameCfg = {0};
            nameCfg.fontSize = 14;
            nameCfg.padding = 4;
            nameCfg.textColor = propNameInput.active ? COR_ITEM_SEL : COR_ITEM;
            nameCfg.bgColor = COR_EDIT_BG;
            nameCfg.borderColor = COR_BORDA;
            nameCfg.selectionColor = GetUIStyle()->inputSelection;
            nameCfg.caretColor = GetUIStyle()->caret;
            nameCfg.allowInput = true;

            int nameFlags = TextInputDraw(propCaixaNome, propBufferNome, (int)sizeof(propBufferNome), &propNameInput, &nameCfg);
            if (nameFlags & (TEXT_INPUT_CHANGED | TEXT_INPUT_SUBMITTED | TEXT_INPUT_DEACTIVATED))
            {
                if (propBufferNome[0] != '\0')
                {
                    strncpy(objetos[idx].nome, propBufferNome, MAX_NOME);
                    objetos[idx].nome[MAX_NOME] = '\0';
                }
            }

            Vector2 mouse = GetMousePosition();

            y += 30;
            DrawText(TextFormat("ID: %d", objetos[idx].id), x + 14, y, 14, secondaryWhite);
            y += 24;

            const Color activeCheckText = (Color){0, 255, 102, 255}; // #00FF66
            DrawText("Ativo", x + 14, y, 14, objetos[idx].ativo ? activeCheckText : COR_TEXTO);
            Rectangle toggle = {(float)(x + 70), (float)(y - 2), 16.0f, 16.0f};
            DrawRectangleLinesEx(toggle, 1, COR_BORDA);
            if (objetos[idx].ativo)
                DrawRectangle((int)toggle.x + 3, (int)toggle.y + 3, 10, 10, COR_ITEM_SEL);

            if (CheckCollisionPointRec(mouse, toggle) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                objetos[idx].ativo = !objetos[idx].ativo;

            y += 24;
            DrawText("Hierarquia", x + 14, y, 14, COR_TEXTO);
            y += 18;

            if (objetos[idx].paiId == -1)
            {
                DrawText("Pai: (nenhum)", x + 14, y, 14, secondaryWhite);
            }
            else
            {
                const char *nomePai = ObterNomeObjeto(objetos[idx].paiId);
                DrawText(TextFormat("Pai: %s (ID: %d)", nomePai, objetos[idx].paiId),
                         x + 14, y, 14, secondaryWhite);
            }

            if (selectedHasAnyParent)
            {
                y += 22;
                Rectangle btn = {(float)(x + 14), (float)y, (float)(PAINEL_LARGURA - 28), 22.0f};
                Color btnCor = COR_ITEM_SEL;
                DrawRectangleRec(btn, btnCor);
                DrawRectangleLinesEx(btn, 1, COR_BORDA);
                const char *unparentLabel = (selectedCount > 1) ? "Desparentar selecionados" : "Desparentar";
                DrawText(unparentLabel, (int)btn.x + 8, (int)btn.y + 4, 14, UiTextForBackground(btnCor));

                if (CheckCollisionPointRec(mouse, btn) &&
                    IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
                {
                    DesparentarObjetosPorIds(selectedIds, selectedCount);
                }

                y += 30;
            }
            else
            {
                y += 12;
            }
        }
    }

    EndScissorMode();

    float contentHeight = (float)y + objectSettingsScroll - (startY + 8.0f) + 8.0f;
    float maxScroll = contentHeight - viewH;
    if (maxScroll < 0.0f)
        maxScroll = 0.0f;
    objectSettingsMaxScroll = maxScroll;
    if (objectSettingsScroll < 0.0f)
        objectSettingsScroll = 0.0f;
    if (objectSettingsScroll > maxScroll)
        objectSettingsScroll = maxScroll;

    if (maxScroll > 0.0f)
    {
        Vector2 mouse = GetMousePosition();
        float barW = 6.0f;
        float barH = viewH * (viewH / (contentHeight + 1.0f));
        if (barH < 24.0f)
            barH = 24.0f;
        float barX = (float)PAINEL_LARGURA - barW - 2.0f;
        float barY = contentTop + (objectSettingsScroll / maxScroll) * (viewH - barH);
        Rectangle bar = {barX, barY, barW, barH};

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mouse, bar))
        {
            objectSettingsScrollDragging = true;
            objectSettingsScrollDragOffset = mouse.y - barY;
        }
        if (objectSettingsScrollDragging)
        {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
            {
                float newBarY = mouse.y - objectSettingsScrollDragOffset;
                if (newBarY < contentTop)
                    newBarY = contentTop;
                if (newBarY > contentTop + viewH - barH)
                    newBarY = contentTop + viewH - barH;
                objectSettingsScroll = ((newBarY - contentTop) / (viewH - barH)) * maxScroll;
            }
            else
            {
                objectSettingsScrollDragging = false;
            }
        }

        Color barCol = GetUIStyle()->panelBorderSoft;
        barCol.a = 200;
        DrawRectangleRec(bar, barCol);
        DrawRectangleLinesEx(bar, 1, COR_BORDA);
    }
    else
    {
        objectSettingsScrollDragging = false;
    }
}

void DrawOutliner(void)
{
    const UIStyle *style = GetUIStyle();
    DrawRectangle(0, 0, PAINEL_LARGURA, GetScreenHeight(), COR_PAINEL);
    DrawLine(PAINEL_LARGURA, 0, PAINEL_LARGURA, GetScreenHeight(), COR_BORDA);
    Rectangle outlinerHeader = {6.0f, 6.0f, (float)(PAINEL_LARGURA - 12), 22.0f};
    DrawBlender266Header(outlinerHeader, "Outliner", 14);
    Vector2 mouse = GetMousePosition();

    int sceneCount = GetSceneCount();
    int activeScene = GetActiveSceneIndex();
    if (sceneCount > sceneExpandedInitCount)
    {
        for (int i = sceneExpandedInitCount; i < sceneCount && i < MAX_SCENES; i++)
        {
            sceneExpanded[i] = true;
            sceneSelected[i] = false;
        }
        sceneExpandedInitCount = sceneCount;
    }
    for (int i = sceneCount; i < MAX_SCENES; i++)
        sceneSelected[i] = false;
    if (activeScene >= 0 && activeScene < MAX_SCENES)
        sceneSelected[activeScene] = true;
    dropSceneIndex = -1;
    int screenH = GetScreenHeight();
    const int settingsHeight = outlinerObjectExpanded ? 170 : 32;
    float settingsStartY = (float)(screenH - settingsHeight);
    const float outlinerTop = 34.0f;
    float outlinerViewH = settingsStartY - outlinerTop - 2.0f;
    if (outlinerViewH < 1.0f)
        outlinerViewH = 1.0f;

    bool mouseEmSettings = CheckCollisionPointRec(mouse, (Rectangle){0, settingsStartY, (float)PAINEL_LARGURA, (float)settingsHeight});
    bool mouseEmOutliner = CheckCollisionPointRec(mouse, (Rectangle){0, outlinerTop, (float)PAINEL_LARGURA, outlinerViewH});
    if (outlinerScroll < 0.0f)
        outlinerScroll = 0.0f;
    if (outlinerScroll > outlinerMaxScroll)
        outlinerScroll = outlinerMaxScroll;
    float wheel = GetMouseWheelMove();
    if (mouseEmSettings && wheel != 0.0f)
        objectSettingsScroll -= wheel * 24.0f;
    else if (mouseEmOutliner && wheel != 0.0f)
        outlinerScroll -= wheel * 24.0f;

    if (mouseEmSettings || menuAtivo || menuCenaContextoAtivo)
        bloquearCliqueOutliner = true;
    bool mouseSobreCena = false;

    int y = (int)(outlinerTop - outlinerScroll);
    BeginScissorMode(0, (int)outlinerTop, PAINEL_LARGURA, (int)outlinerViewH);
    Rectangle scenesHeader = {6.0f, (float)y, (float)(PAINEL_LARGURA - 16), 18.0f};
    bool scenesClicked = DrawOutlinerSectionHeader(scenesHeader, "Scenes", 12, &outlinerScenesExpanded, true, mouse);
    if (scenesClicked && CtrlHeld() && outlinerScenesExpanded)
        outlinerObjectExpanded = true;
    Rectangle scenesCountBadge = {scenesHeader.x + scenesHeader.width - 28.0f, scenesHeader.y + 2.0f, 22.0f, 14.0f};
    DrawRectangleRec(scenesCountBadge, style->panelBgHover);
    DrawRectangleLinesEx(scenesCountBadge, 1.0f, style->panelBorderSoft);
    DrawText(TextFormat("%d", sceneCount), (int)scenesCountBadge.x + 6, (int)scenesCountBadge.y + 2, 10, style->textPrimary);
    y += 22;

    if (outlinerScenesExpanded)
    {
        for (int i = 0; i < sceneCount; i++)
        {
            Rectangle item = {6, (float)y, (float)(PAINEL_LARGURA - 18), 20.0f};
            bool hover = CheckCollisionPointRec(mouse, item);
            if (hover)
                mouseSobreCena = true;

            bool ativo = (i == activeScene);
            bool selecionada = sceneSelected[i];
            Color fundo = ativo ? style->accent : (selecionada ? style->accentSoft : style->itemBg);
            if (hover && !ativo && !selecionada)
                fundo = style->itemHover;
            if (arrastando && hover)
            {
                fundo = COR_ITEM_DRAG;
                dropSceneIndex = i;
            }
            DrawRectangleRec(item, fundo);
            if (ativo || hover || selecionada)
            {
                Color bordaCena = ativo ? style->panelBorder : style->accentSoft;
                DrawRectangleLinesEx(item, 1, bordaCena);
            }
            if (!ativo)
            {
                Color marcador = selecionada ? style->accent : (hover ? style->accent : style->accentSoft);
                DrawRectangle((int)item.x + 1, (int)item.y + 1, 3, (int)item.height - 2, marcador);
            }

            int textX = (int)item.x + 6;
            int sceneObjCount = GetSceneObjectCount(i);
            char sceneLabel[96] = {0};
            snprintf(sceneLabel, sizeof(sceneLabel), "%s (%d)", GetSceneName(i), sceneObjCount);
            Color textColor = ativo ? style->buttonTextHover : (selecionada ? style->textPrimary : (hover ? style->textPrimary : style->textSecondary));
            if (renomeandoCena && cenaEditandoIndex == i)
            {
                Rectangle sceneNameBox = {item.x + 4.0f, item.y + 1.0f, item.width - 8.0f, item.height - 2.0f};
                TextInputConfig cfg = {0};
                cfg.fontSize = 12;
                cfg.padding = 3;
                cfg.textColor = GetUIStyle()->inputText;
                cfg.bgColor = COR_EDIT_BG;
                cfg.borderColor = COR_BORDA;
                cfg.selectionColor = GetUIStyle()->inputSelection;
                cfg.caretColor = GetUIStyle()->caret;
                cfg.allowInput = true;

                int flags = TextInputDraw(sceneNameBox, bufferCena, (int)sizeof(bufferCena), &sceneNameInput, &cfg);
                if (flags & (TEXT_INPUT_SUBMITTED | TEXT_INPUT_DEACTIVATED))
                {
                    if (bufferCena[0] != '\0')
                    {
                        SwitchScene(i);
                        RenameActiveScene(bufferCena);
                    }
                    renomeandoCena = false;
                }
            }
            else
            {
                DrawText(sceneLabel, textX, (int)item.y + 3, 12, textColor);
            }

            if (!renomeandoCena && hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            {
                if (ShiftHeld())
                {
                    sceneSelected[i] = !sceneSelected[i];
                    renomeandoCena = false;
                }
                else
                {
                    double agora = GetTime();
                    LimparSelecaoCenas();
                    sceneSelected[i] = true;
                    SwitchScene(i);
                    sceneExpanded[i] = true;
                    if (ultimoCliqueCenaIndex == i && (agora - ultimoCliqueCena) < 0.3)
                    {
                        renomeandoCena = true;
                        cenaEditandoIndex = i;
                        strncpy(bufferCena, GetSceneName(i), MAX_NOME);
                        bufferCena[MAX_NOME] = '\0';
                        TextInputInit(&sceneNameInput);
                        sceneNameInput.active = true;
                        sceneNameInput.caret = (int)strlen(bufferCena);
                        sceneNameInput.selStart = 0;
                        sceneNameInput.selEnd = sceneNameInput.caret;
                    }
                    else
                    {
                        renomeandoCena = false;
                    }
                    ultimoCliqueCenaIndex = i;
                    ultimoCliqueCena = agora;
                }
            }

            if (hover && IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
            {
                menuCenaContextoAtivo = true;
                menuCenaPos = mouse;
                menuCenaIndex = i;
            }

            y += 22;

            if (i == activeScene && sceneExpanded[i])
            {
                for (int j = 0; j < totalObjetos; j++)
                {
                    if (objetos[j].paiId == -1)
                        y = DrawOutlinerItem(objetos[j].id, 1, y);
                }
            }
        }
    }
    EndScissorMode();

    float outlinerContentHeight = (float)y + outlinerScroll - outlinerTop + 6.0f;
    float maxScroll = outlinerContentHeight - outlinerViewH;
    if (maxScroll < 0.0f)
        maxScroll = 0.0f;
    outlinerMaxScroll = maxScroll;
    if (outlinerScroll < 0.0f)
        outlinerScroll = 0.0f;
    if (outlinerScroll > maxScroll)
        outlinerScroll = maxScroll;

    if (maxScroll > 0.0f)
    {
        float barW = 6.0f;
        float barH = outlinerViewH * (outlinerViewH / (outlinerContentHeight + 1.0f));
        if (barH < 24.0f)
            barH = 24.0f;
        float barX = (float)PAINEL_LARGURA - barW - 2.0f;
        float barY = outlinerTop + (outlinerScroll / maxScroll) * (outlinerViewH - barH);
        Rectangle bar = {barX, barY, barW, barH};

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mouse, bar))
        {
            outlinerScrollDragging = true;
            outlinerScrollDragOffset = mouse.y - barY;
        }
        if (outlinerScrollDragging)
        {
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
            {
                float newBarY = mouse.y - outlinerScrollDragOffset;
                if (newBarY < outlinerTop)
                    newBarY = outlinerTop;
                if (newBarY > outlinerTop + outlinerViewH - barH)
                    newBarY = outlinerTop + outlinerViewH - barH;
                outlinerScroll = ((newBarY - outlinerTop) / (outlinerViewH - barH)) * maxScroll;
            }
            else
            {
                outlinerScrollDragging = false;
            }
        }

        Color barCol = GetUIStyle()->panelBorderSoft;
        barCol.a = 200;
        DrawRectangleRec(bar, barCol);
        DrawRectangleLinesEx(bar, 1, COR_BORDA);
    }
    else
    {
        outlinerScrollDragging = false;
    }

    if (!mouseEmSettings && !menuAtivo && !menuCenaContextoAtivo)
        bloquearCliqueOutliner = false;

    if (menuCenaContextoAtivo)
    {
        const UIStyle *style = GetUIStyle();
        Rectangle menu = {menuCenaPos.x, menuCenaPos.y, 170.0f, 96.0f};
        Rectangle itemNova = {menu.x, menu.y, menu.width, 24.0f};
        Rectangle itemDuplicar = {menu.x, menu.y + 24.0f, menu.width, 24.0f};
        Rectangle itemRenomear = {menu.x, menu.y + 48.0f, menu.width, 24.0f};
        Rectangle itemExcluir = {menu.x, menu.y + 72.0f, menu.width, 24.0f};

        DrawRectangleRec(menu, COR_MENU);

        bool hoverNova = CheckCollisionPointRec(mouse, itemNova);
        bool hoverDuplicar = CheckCollisionPointRec(mouse, itemDuplicar);
        bool hoverRenomear = CheckCollisionPointRec(mouse, itemRenomear);
        bool hoverExcluir = CheckCollisionPointRec(mouse, itemExcluir);
        if (hoverNova)
            DrawRectangleRec(itemNova, style->accent);
        if (hoverDuplicar)
            DrawRectangleRec(itemDuplicar, style->accent);
        if (hoverRenomear)
            DrawRectangleRec(itemRenomear, style->accent);
        if (hoverExcluir)
            DrawRectangleRec(itemExcluir, style->accent);

        DrawText("Nova Cena", (int)itemNova.x + 8, (int)itemNova.y + 6, 12, hoverNova ? style->buttonTextHover : COR_TEXTO);
        DrawText("Duplicar Cena", (int)itemDuplicar.x + 8, (int)itemDuplicar.y + 6, 12, hoverDuplicar ? style->buttonTextHover : COR_TEXTO);
        DrawText("Renomear", (int)itemRenomear.x + 8, (int)itemRenomear.y + 6, 12, hoverRenomear ? style->buttonTextHover : COR_TEXTO);
        DrawText("Excluir Cena", (int)itemExcluir.x + 8, (int)itemExcluir.y + 6, 12, hoverExcluir ? style->buttonTextHover : COR_TEXTO);

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            if (CheckCollisionPointRec(mouse, itemNova))
            {
                CreateNewScene();
                menuCenaContextoAtivo = false;
            }
            else if (CheckCollisionPointRec(mouse, itemDuplicar))
            {
                if (menuCenaIndex >= 0)
                {
                    SwitchScene(menuCenaIndex);
                    sceneExpanded[menuCenaIndex] = true;
                }
                DuplicateActiveScene();
                menuCenaContextoAtivo = false;
            }
            else if (CheckCollisionPointRec(mouse, itemRenomear))
            {
                if (menuCenaIndex >= 0)
                {
                    SwitchScene(menuCenaIndex);
                    sceneExpanded[menuCenaIndex] = true;
                    renomeandoCena = true;
                    cenaEditandoIndex = menuCenaIndex;
                    strncpy(bufferCena, GetSceneName(menuCenaIndex), MAX_NOME);
                    bufferCena[MAX_NOME] = '\0';
                    TextInputInit(&sceneNameInput);
                    sceneNameInput.active = true;
                    sceneNameInput.caret = (int)strlen(bufferCena);
                    sceneNameInput.selStart = 0;
                    sceneNameInput.selEnd = sceneNameInput.caret;
                }
                menuCenaContextoAtivo = false;
            }
            else if (CheckCollisionPointRec(mouse, itemExcluir))
            {
                if (menuCenaIndex >= 0)
                    DeleteSelectedScenesFromContext(menuCenaIndex);
                menuCenaContextoAtivo = false;
            }
            else if (!CheckCollisionPointRec(mouse, menu))
            {
                menuCenaContextoAtivo = false;
            }
        }

        if (CheckCollisionPointRec(mouse, menu))
            bloquearCliqueOutliner = true;

        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON) && !CheckCollisionPointRec(mouse, menu))
            menuCenaContextoAtivo = false;
    }

    DrawObjectSettings(settingsStartY, (float)settingsHeight);

    if (menuAtivo)
        DrawContextMenu();
}

void ProcessarOutliner(void)
{
    ProcessarDragDrop();
}

const char *ObterNomeObjeto(int id)
{
    int idx = BuscarIndicePorId(id);
    if (idx != -1)
        return objetos[idx].nome;
    return "";
}

bool ObjetoExisteNoOutliner(const char *nome)
{
    if (!nome || strlen(nome) == 0)
        return false;

    for (int i = 0; i < totalObjetos; i++)
    {
        if (strcmp(objetos[i].nome, nome) == 0)
            return true;
    }
    return false;
}

int ObterObjetoSelecionadoId(void)
{
    if (objetoSelecionadoPrincipalId > 0)
    {
        int idxPrincipal = BuscarIndicePorId(objetoSelecionadoPrincipalId);
        if (idxPrincipal != -1 && objetos[idxPrincipal].selecionado)
            return objetoSelecionadoPrincipalId;
    }

    for (int i = 0; i < totalObjetos; i++)
        if (objetos[i].selecionado)
        {
            objetoSelecionadoPrincipalId = objetos[i].id;
            return objetos[i].id;
        }

    objetoSelecionadoPrincipalId = -1;
    return -1;
}

void SelecionarObjetoPorId(int id)
{
    LimparSelecaoObjetos();
    int idx = BuscarIndicePorId(id);
    if (idx != -1)
    {
        objetos[idx].selecionado = true;
        objetoSelecionadoPrincipalId = id;
    }
}

void AdicionarObjetoSelecionadoPorId(int id)
{
    int idx = BuscarIndicePorId(id);
    if (idx == -1)
        return;

    objetos[idx].selecionado = true;
    objetoSelecionadoPrincipalId = id;
}

bool DuplicarObjetosSelecionados(Vector3 deslocamento)
{
    int selectedIds[MAX_OBJETOS] = {0};
    int selectedCount = ColetarIdsSelecionados(selectedIds, MAX_OBJETOS);
    if (selectedCount <= 0)
        return false;

    if (selectedCount > 1)
        return DuplicarObjetosPorLista(selectedIds, selectedCount, selectedIds, selectedCount,
                                       objetoSelecionadoPrincipalId, deslocamento);

    return DuplicarObjetosPorRaizes(selectedIds, selectedCount, deslocamento);
}

int ObterProximoId(void)
{
    return proximoId;
}

void DefinirProximoId(int id)
{
    if (id < 1)
        id = 1;
    proximoId = id;
}

