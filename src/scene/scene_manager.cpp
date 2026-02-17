#include "scene_manager.h"
#include "assets/model_manager.h"
#include "editor/ui/properties_panel.h"
#include "editor/ui/info_panel.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <limits.h>
#endif

typedef struct
{
    char nome[32];
    ObjetoCena objetosSnapshot[MAX_OBJETOS];
    int totalObjetosSnapshot;
    int proximoIdSnapshot;
    int selectedIdSnapshot;
} CenaSnapshot;

static CenaSnapshot cenas[MAX_SCENES];
static int totalCenas = 0;
static int cenaAtiva = -1;
static char projectPath[256] = {0};
static char projectDir[256] = {0};
static char recentProjects[MAX_RECENT_PROJECTS][512] = {0};
static int recentProjectCount = 0;
static bool recentProjectsLoaded = false;
static char pendingProjectIconPath[512] = {0};
static Vector3 projectCameraPos = {6.0f, 6.0f, 6.0f};
static Vector3 projectCameraTarget = {0.0f, 0.0f, 0.0f};
static bool loadedProjectCameraPending = false;

static void EnsureProjectPaths(void);
static bool IsPathAbsolute(const char *path);
static void LoadRecentProjects(void);
static void SaveRecentProjects(void);
static void AddRecentProject(const char *path);

static bool NormalizeRecentProjectPath(const char *in, char *out, size_t outSize)
{
    if (!in || !out || outSize == 0)
        return false;
    out[0] = '\0';

    while (*in != '\0' && isspace((unsigned char)*in))
        in++;

    char temp[512] = {0};
    strncpy(temp, in, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    int end = (int)strlen(temp) - 1;
    while (end >= 0 && isspace((unsigned char)temp[end]))
        temp[end--] = '\0';
    if (temp[0] == '\0')
        return false;

#ifdef _WIN32
    for (int i = 0; temp[i] != '\0'; i++)
        if (temp[i] == '/')
            temp[i] = '\\';
#else
    for (int i = 0; temp[i] != '\0'; i++)
        if (temp[i] == '\\')
            temp[i] = '/';
#endif

    char candidate[512] = {0};
    if (IsPathAbsolute(temp))
    {
        strncpy(candidate, temp, sizeof(candidate) - 1);
    }
    else
    {
        const char *cwd = GetWorkingDirectory();
        if (!cwd || cwd[0] == '\0')
            return false;
        snprintf(candidate, sizeof(candidate), "%s/%s", cwd, temp);
    }
    candidate[sizeof(candidate) - 1] = '\0';

#ifdef _WIN32
    char absPath[512] = {0};
    if (_fullpath(absPath, candidate, sizeof(absPath)))
    {
        strncpy(out, absPath, outSize - 1);
        out[outSize - 1] = '\0';
    }
    else
    {
        strncpy(out, candidate, outSize - 1);
        out[outSize - 1] = '\0';
    }
    for (size_t i = 0; out[i] != '\0'; i++)
        if (out[i] == '/')
            out[i] = '\\';
#else
    char absPath[PATH_MAX] = {0};
    if (realpath(candidate, absPath))
    {
        strncpy(out, absPath, outSize - 1);
        out[outSize - 1] = '\0';
    }
    else
    {
        strncpy(out, candidate, outSize - 1);
        out[outSize - 1] = '\0';
    }
#endif

    return true;
}

static void GetRecentProjectsFilePath(char *out, size_t outSize)
{
    const char *cwd = GetWorkingDirectory();
    if (!cwd || cwd[0] == '\0' || !out || outSize == 0)
        return;
    snprintf(out, outSize, "%s/recent_projects.txt", cwd);
    out[outSize - 1] = '\0';
}

static bool PathEquals(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    char na[512] = {0};
    char nb[512] = {0};
    if (!NormalizeRecentProjectPath(a, na, sizeof(na)))
        return false;
    if (!NormalizeRecentProjectPath(b, nb, sizeof(nb)))
        return false;
#ifdef _WIN32
    return _stricmp(na, nb) == 0;
#else
    return strcmp(na, nb) == 0;
#endif
}

static void SaveRecentProjects(void)
{
    char path[512] = {0};
    GetRecentProjectsFilePath(path, sizeof(path));
    if (path[0] == '\0')
        return;

    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    for (int i = 0; i < recentProjectCount; i++)
    {
        if (recentProjects[i][0] == '\0')
            continue;
        fprintf(f, "%s\n", recentProjects[i]);
    }
    fclose(f);
}

static void LoadRecentProjects(void)
{
    if (recentProjectsLoaded)
        return;
    recentProjectsLoaded = true;
    recentProjectCount = 0;

    char path[512] = {0};
    GetRecentProjectsFilePath(path, sizeof(path));
    if (path[0] == '\0')
        return;

    FILE *f = fopen(path, "rb");
    if (!f)
        return;

    char line[512];
    while (fgets(line, (int)sizeof(line), f) && recentProjectCount < MAX_RECENT_PROJECTS)
    {
        line[strcspn(line, "\r\n")] = '\0';
        char normalized[512] = {0};
        if (!NormalizeRecentProjectPath(line, normalized, sizeof(normalized)))
            continue;
        if (!FileExists(normalized))
            continue;

        bool dup = false;
        for (int i = 0; i < recentProjectCount; i++)
        {
            if (PathEquals(recentProjects[i], normalized))
            {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;

        strncpy(recentProjects[recentProjectCount], normalized, sizeof(recentProjects[recentProjectCount]) - 1);
        recentProjects[recentProjectCount][sizeof(recentProjects[recentProjectCount]) - 1] = '\0';
        recentProjectCount++;
    }
    fclose(f);
}

static void AddRecentProject(const char *path)
{
    if (!path || path[0] == '\0')
        return;
    char normalized[512] = {0};
    if (!NormalizeRecentProjectPath(path, normalized, sizeof(normalized)))
        return;
    LoadRecentProjects();

    int existing = -1;
    for (int i = 0; i < recentProjectCount; i++)
    {
        if (PathEquals(recentProjects[i], normalized))
        {
            existing = i;
            break;
        }
    }

    if (existing > 0)
    {
        char tmp[512];
        strncpy(tmp, recentProjects[existing], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        for (int i = existing; i > 0; i--)
        {
            strncpy(recentProjects[i], recentProjects[i - 1], sizeof(recentProjects[i]) - 1);
            recentProjects[i][sizeof(recentProjects[i]) - 1] = '\0';
        }
        strncpy(recentProjects[0], tmp, sizeof(recentProjects[0]) - 1);
        recentProjects[0][sizeof(recentProjects[0]) - 1] = '\0';
    }
    else if (existing < 0)
    {
        if (recentProjectCount < MAX_RECENT_PROJECTS)
            recentProjectCount++;
        for (int i = recentProjectCount - 1; i > 0; i--)
        {
            strncpy(recentProjects[i], recentProjects[i - 1], sizeof(recentProjects[i]) - 1);
            recentProjects[i][sizeof(recentProjects[i]) - 1] = '\0';
        }
        strncpy(recentProjects[0], normalized, sizeof(recentProjects[0]) - 1);
        recentProjects[0][sizeof(recentProjects[0]) - 1] = '\0';
    }

    SaveRecentProjects();
}
static void SalvarCenaAtiva(void)
{
    if (cenaAtiva < 0 || cenaAtiva >= totalCenas)
        return;

    CenaSnapshot *cena = &cenas[cenaAtiva];
    cena->totalObjetosSnapshot = totalObjetos;
    for (int i = 0; i < totalObjetos; i++)
    {
        PropertiesSyncToObjeto(&objetos[i]);
        cena->objetosSnapshot[i] = objetos[i];
    }
    cena->proximoIdSnapshot = ObterProximoId();
    cena->selectedIdSnapshot = ObterObjetoSelecionadoId();
}

static int IndexInList(int id, const int *ids, int count)
{
    for (int i = 0; i < count; i++)
        if (ids[i] == id)
            return i;
    return -1;
}

static void ColetarSubarvoreIds(int id, int *ids, int *count, int maxCount)
{
    if (*count >= maxCount)
        return;
    if (IndexInList(id, ids, *count) != -1)
        return;

    ids[(*count)++] = id;
    for (int i = 0; i < totalObjetos; i++)
        if (objetos[i].paiId == id)
            ColetarSubarvoreIds(objetos[i].id, ids, count, maxCount);
}

static void RemoverObjetoPorIndice(int index)
{
    for (int i = index; i < totalObjetos - 1; i++)
        objetos[i] = objetos[i + 1];
    totalObjetos--;
}

static int MapearId(int oldId, const int *oldIds, const int *newIds, int count)
{
    for (int i = 0; i < count; i++)
        if (oldIds[i] == oldId)
            return newIds[i];
    return -1;
}

static void CarregarCena(int index)
{
    if (index < 0 || index >= totalCenas)
        return;

    InitOutliner();
    ClearActiveModels();

    CenaSnapshot *cena = &cenas[index];
    int count = cena->totalObjetosSnapshot;

    int oldIds[MAX_OBJETOS] = {0};
    int newIds[MAX_OBJETOS] = {0};
    int maxNewId = 0;
    int oldSelectedId = cena->selectedIdSnapshot;
    int newSelectedId = -1;

    for (int i = 0; i < count; i++)
    {
        ObjetoCena *src = &cena->objetosSnapshot[i];
        int novoId = RegistrarObjeto(src->nome, src->posicao, -1);
        oldIds[i] = src->id;
        newIds[i] = novoId;
        if (novoId > maxNewId)
            maxNewId = novoId;

        int idx = BuscarIndicePorId(novoId);
        if (idx != -1)
        {
            objetos[idx].ativo = src->ativo;
            objetos[idx].selecionado = false;
            strncpy(objetos[idx].caminhoModelo, src->caminhoModelo, 255);
            objetos[idx].caminhoModelo[255] = '\0';
            objetos[idx].protoEnabled = src->protoEnabled;
            objetos[idx].protoBaseColor = src->protoBaseColor;
            objetos[idx].protoSecondaryColor = src->protoSecondaryColor;
            objetos[idx].protoPack = src->protoPack;
            strncpy(objetos[idx].protoCustomName, src->protoCustomName, sizeof(objetos[idx].protoCustomName) - 1);
            objetos[idx].protoCustomName[sizeof(objetos[idx].protoCustomName) - 1] = '\0';
            objetos[idx].protoCustomBase = src->protoCustomBase;
            objetos[idx].protoCustomSecondary = src->protoCustomSecondary;
            objetos[idx].protoCustomCount = src->protoCustomCount;
            for (int c = 0; c < src->protoCustomCount && c < MAX_PROTO_CUSTOM; c++)
                objetos[idx].protoCustomEntries[c] = src->protoCustomEntries[c];
            objetos[idx].physStatic = src->physStatic;
            objetos[idx].physRigidbody = src->physRigidbody;
            objetos[idx].physCollider = src->physCollider;
            objetos[idx].physGravity = src->physGravity;
            objetos[idx].physTerrain = src->physTerrain;
            objetos[idx].physMass = src->physMass;
            objetos[idx].physShape = src->physShape;
            objetos[idx].physSize = src->physSize;
            PropertiesSyncFromObjeto(&objetos[idx]);
        }
    }

    for (int i = 0; i < count; i++)
    {
        ObjetoCena *src = &cena->objetosSnapshot[i];
        if (src->paiId == -1)
            continue;

        int novoId = newIds[i];
        int novoPai = MapearId(src->paiId, oldIds, newIds, count);
        int idx = BuscarIndicePorId(novoId);
        if (idx != -1)
            objetos[idx].paiId = novoPai;
    }

    if (oldSelectedId > 0)
        newSelectedId = MapearId(oldSelectedId, oldIds, newIds, count);
    if (newSelectedId > 0)
        SelecionarObjetoPorId(newSelectedId);

    DefinirProximoId(maxNewId + 1);

    for (int i = 0; i < count; i++)
    {
        ObjetoCena *src = &cena->objetosSnapshot[i];
        if (src->caminhoModelo[0] == '\0')
            continue;

        int novoId = newIds[i];
        if (!FileExists(src->caminhoModelo))
            TraceLog(LOG_WARNING, "Modelo nao encontrado: %s", src->caminhoModelo);
        else
            TraceLog(LOG_INFO, "Carregando modelo: %s", src->caminhoModelo);
        CarregarModeloParaObjeto(src->caminhoModelo, src->nome, novoId);
    }

    SetSelectedModelByObjetoId(ObterObjetoSelecionadoId());
}

void InitSceneManager(void)
{
    totalCenas = 0;
    cenaAtiva = -1;
    projectPath[0] = '\0';
    projectDir[0] = '\0';
    projectCameraPos = (Vector3){6.0f, 6.0f, 6.0f};
    projectCameraTarget = (Vector3){0.0f, 0.0f, 0.0f};
    loadedProjectCameraPending = false;

    if (LoadProject("projects/Project 1/project.json"))
        return;

    CreateNewScene();
}

int GetSceneCount(void)
{
    return totalCenas;
}

int GetActiveSceneIndex(void)
{
    return cenaAtiva;
}

const char *GetActiveSceneName(void)
{
    if (cenaAtiva < 0 || cenaAtiva >= totalCenas)
        return "Sem cena";
    return cenas[cenaAtiva].nome;
}

const char *GetSceneName(int index)
{
    if (index < 0 || index >= totalCenas)
        return "";
    return cenas[index].nome;
}

int GetSceneObjectCount(int index)
{
    if (index < 0 || index >= totalCenas)
        return 0;
    if (index == cenaAtiva)
        return totalObjetos;
    return cenas[index].totalObjetosSnapshot;
}

void CreateNewScene(void)
{
    if (totalCenas >= MAX_SCENES)
        return;

    if (cenaAtiva != -1)
        SalvarCenaAtiva();

    CenaSnapshot *nova = &cenas[totalCenas];
    int numero = totalCenas + 1;
    snprintf(nova->nome, sizeof(nova->nome), "Cena %d", numero);
    nova->totalObjetosSnapshot = 0;
    nova->proximoIdSnapshot = 1;
    nova->selectedIdSnapshot = -1;

    totalCenas++;
    cenaAtiva = totalCenas - 1;

    CarregarCena(cenaAtiva);
}

void SwitchScene(int index)
{
    if (index < 0 || index >= totalCenas)
        return;
    if (index == cenaAtiva)
        return;

    SalvarCenaAtiva();
    cenaAtiva = index;
    CarregarCena(cenaAtiva);
}

void DuplicateActiveScene(void)
{
    if (cenaAtiva < 0 || cenaAtiva >= totalCenas)
        return;
    if (totalCenas >= MAX_SCENES)
        return;

    SalvarCenaAtiva();

    CenaSnapshot *orig = &cenas[cenaAtiva];
    CenaSnapshot *nova = &cenas[totalCenas];
    *nova = *orig;

    int numero = totalCenas + 1;
    snprintf(nova->nome, sizeof(nova->nome), "Cena %d (Copia)", numero);

    totalCenas++;
    cenaAtiva = totalCenas - 1;
    CarregarCena(cenaAtiva);
}

void RenameActiveScene(const char *nome)
{
    if (cenaAtiva < 0 || cenaAtiva >= totalCenas)
        return;
    if (!nome || nome[0] == '\0')
        return;

    strncpy(cenas[cenaAtiva].nome, nome, sizeof(cenas[cenaAtiva].nome) - 1);
    cenas[cenaAtiva].nome[sizeof(cenas[cenaAtiva].nome) - 1] = '\0';
}

void DeleteScene(int index)
{
    if (index < 0 || index >= totalCenas)
        return;
    if (totalCenas <= 1)
    {
        // Mantem pelo menos uma cena, mas limpa seu conteudo para o usuario
        // perceber que a acao de excluir foi aplicada.
        InitOutliner();
        ClearActiveModels();
        CenaSnapshot *c = &cenas[index];
        c->totalObjetosSnapshot = 0;
        c->proximoIdSnapshot = 1;
        c->selectedIdSnapshot = -1;
        if (c->nome[0] == '\0')
            snprintf(c->nome, sizeof(c->nome), "Cena 1");
        cenaAtiva = index;
        return;
    }

    SalvarCenaAtiva();

    bool deletingActive = (index == cenaAtiva);
    int newActive = cenaAtiva;
    if (deletingActive)
        newActive = (index == 0) ? 1 : 0;

    for (int i = index; i < totalCenas - 1; i++)
        cenas[i] = cenas[i + 1];

    totalCenas--;

    if (deletingActive)
    {
        if (newActive > index)
            newActive--;
        cenaAtiva = newActive;
        CarregarCena(cenaAtiva);
    }
    else
    {
        if (index < cenaAtiva)
            cenaAtiva--;
    }
}

bool MoveObjetoParaCena(int objetoId, int cenaIndex)
{
    if (cenaIndex < 0 || cenaIndex >= totalCenas)
        return false;
    if (cenaIndex == cenaAtiva)
        return false;

    int rootIdx = BuscarIndicePorId(objetoId);
    if (rootIdx == -1)
        return false;

    int moveIds[MAX_OBJETOS] = {0};
    int moveCount = 0;
    ColetarSubarvoreIds(objetoId, moveIds, &moveCount, MAX_OBJETOS);
    if (moveCount <= 0)
        return false;

    CenaSnapshot *target = &cenas[cenaIndex];
    if (target->totalObjetosSnapshot + moveCount > MAX_OBJETOS)
        return false;

    int newIds[MAX_OBJETOS] = {0};
    int nextId = target->proximoIdSnapshot > 0 ? target->proximoIdSnapshot : 1;
    for (int i = 0; i < moveCount; i++)
        newIds[i] = nextId++;

    for (int i = 0; i < moveCount; i++)
    {
        int idx = BuscarIndicePorId(moveIds[i]);
        if (idx == -1)
            continue;

        ObjetoCena obj = objetos[idx];
        obj.id = newIds[i];
        int parentIdx = IndexInList(obj.paiId, moveIds, moveCount);
        if (parentIdx == -1)
            obj.paiId = -1;
        else
            obj.paiId = newIds[parentIdx];
        obj.selecionado = false;

        target->objetosSnapshot[target->totalObjetosSnapshot++] = obj;
    }

    target->proximoIdSnapshot = nextId;

    for (int i = totalObjetos - 1; i >= 0; i--)
    {
        if (IndexInList(objetos[i].id, moveIds, moveCount) != -1)
        {
            RemoverModeloPorIdObjeto(objetos[i].id);
            RemoverObjetoPorIndice(i);
        }
    }

    if (IndexInList(ObterObjetoSelecionadoId(), moveIds, moveCount) != -1)
        SelecionarObjetoPorId(-1);

    SetSelectedModelByObjetoId(ObterObjetoSelecionadoId());
    SalvarCenaAtiva();
    return true;
}

const char *GetProjectPath(void)
{
    return projectPath;
}

void ReloadActiveScene(void)
{
    if (cenaAtiva < 0 || cenaAtiva >= totalCenas)
        return;
    CarregarCena(cenaAtiva);
}

void SaveActiveSceneSnapshot(void)
{
    SalvarCenaAtiva();
}

const char *GetProjectDir(void)
{
    return projectDir;
}

void SetProjectPath(const char *path)
{
    if (!path)
        return;
    strncpy(projectPath, path, sizeof(projectPath) - 1);
    projectPath[sizeof(projectPath) - 1] = '\0';
}

static void EscapeJsonString(const char *src, char *dst, size_t dstSize)
{
    size_t di = 0;
    for (size_t i = 0; src[i] != '\0' && di + 2 < dstSize; i++)
    {
        char c = src[i];
        if (c == '\\' || c == '"')
        {
            dst[di++] = '\\';
            dst[di++] = c;
        }
        else
        {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

static void UnescapeJsonString(const char *src, char *dst, size_t dstSize)
{
    size_t di = 0;
    for (size_t i = 0; src[i] != '\0' && di + 1 < dstSize; i++)
    {
        if (src[i] == '\\' && src[i + 1] != '\0')
        {
            i++;
            dst[di++] = src[i];
        }
        else
        {
            dst[di++] = src[i];
        }
    }
    dst[di] = '\0';
}

static unsigned char ClampByte(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (unsigned char)v;
}


static void GetDirectoryFromPath(const char *path, char *out, size_t outSize)
{
    if (!path || outSize == 0)
        return;
    const char *lastSlash = strrchr(path, '/');
    const char *lastBack = strrchr(path, '\\');
    const char *last = lastSlash;
    if (lastBack && (!last || lastBack > last))
        last = lastBack;

    if (!last)
    {
        strncpy(out, ".", outSize - 1);
        out[outSize - 1] = '\0';
        return;
    }

    size_t len = (size_t)(last - path);
    if (len >= outSize)
        len = outSize - 1;
    strncpy(out, path, len);
    out[len] = '\0';
}

static void EnsureProjectPaths(void)
{
    if (projectPath[0] != '\0' && projectDir[0] != '\0')
        return;
}

static bool EnsureProjectsRoot(char *outRoot, size_t outSize)
{
    const char *cwd = GetWorkingDirectory();
    if (!cwd || cwd[0] == '\0')
        return false;

    snprintf(outRoot, outSize, "%s/projects", cwd);
#ifdef _WIN32
    if (!DirectoryExists(outRoot) && _mkdir(outRoot) != 0)
        return false;
#else
    if (!DirectoryExists(outRoot) && mkdir(outRoot, 0755) != 0)
        return false;
#endif
    return true;
}

static bool IsPathAbsolute(const char *path)
{
    if (!path || path[0] == '\0')
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return true;
    if (path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
        return true;
    return false;
}

static void SanitizeName(const char *src, char *dst, size_t dstSize)
{
    size_t di = 0;
    for (size_t i = 0; src[i] != '\0' && di + 1 < dstSize; i++)
    {
        char c = src[i];
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
        dst[di++] = c;
    }
    dst[di] = '\0';
}

static bool CopyFileBinary(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in)
        return false;
    FILE *out = fopen(dst, "wb");
    if (!out)
    {
        fclose(in);
        return false;
    }

    char buffer[4096];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0)
        fwrite(buffer, 1, n, out);

    fclose(in);
    fclose(out);
    return true;
}

static bool EnsureAssetsDirAt(const char *baseDir)
{
    char assetsDir[512];
    if (baseDir && baseDir[0] != '\0' && strcmp(baseDir, ".") != 0)
        snprintf(assetsDir, sizeof(assetsDir), "%s/assets", baseDir);
    else
        snprintf(assetsDir, sizeof(assetsDir), "assets");

    if (DirectoryExists(assetsDir))
        return true;

#ifdef _WIN32
    if (baseDir && baseDir[0] != '\0' && strcmp(baseDir, ".") != 0)
    {
        if (!DirectoryExists(baseDir) && _mkdir(baseDir) != 0)
            return false;
    }
    return _mkdir(assetsDir) == 0;
#else
    if (baseDir && baseDir[0] != '\0' && strcmp(baseDir, ".") != 0)
    {
        if (!DirectoryExists(baseDir) && mkdir(baseDir, 0755) != 0)
            return false;
    }
    return mkdir(assetsDir, 0755) == 0;
#endif
}

static void ResolveProjectPath(const char *relative, char *out, size_t outSize)
{
    if (!relative || relative[0] == '\0')
    {
        if (outSize > 0)
            out[0] = '\0';
        return;
    }

    if (IsPathAbsolute(relative))
    {
        strncpy(out, relative, outSize - 1);
        out[outSize - 1] = '\0';
#ifdef _WIN32
        for (size_t i = 0; out[i] != '\0'; i++)
            if (out[i] == '/')
                out[i] = '\\';
#endif
        return;
    }

    if (projectDir[0] == '\0')
        EnsureProjectPaths();

    if (projectDir[0] == '\0')
    {
        strncpy(out, relative, outSize - 1);
        out[outSize - 1] = '\0';
#ifdef _WIN32
        for (size_t i = 0; out[i] != '\0'; i++)
            if (out[i] == '/')
                out[i] = '\\';
#endif
        return;
    }

    snprintf(out, outSize, "%s/%s", projectDir, relative);
#ifdef _WIN32
    for (size_t i = 0; out[i] != '\0'; i++)
        if (out[i] == '/')
            out[i] = '\\';
#endif
}



bool SaveProject(void)
{
    SalvarCenaAtiva();

    if (projectPath[0] == '\0' || projectDir[0] == '\0')
        return false;

    if (!EnsureAssetsDirAt(projectDir))
        return false;

    FILE *f = fopen(projectPath, "wb");
    if (!f)
        return false;

    fprintf(f, "{\n");
    fprintf(f, "\"active\":%d,\n", cenaAtiva < 0 ? 0 : cenaAtiva);
    fprintf(f, "\"camera\":{\"pos\":[%.4f,%.4f,%.4f],\"target\":[%.4f,%.4f,%.4f]},\n",
            projectCameraPos.x, projectCameraPos.y, projectCameraPos.z,
            projectCameraTarget.x, projectCameraTarget.y, projectCameraTarget.z);
    fprintf(f, "\"editor\":{\"showCollisions\":%d,\"ray2D\":%d,\"ray3D\":%d},\n",
            PropertiesShowCollisions() ? 1 : 0,
            IsRaycast2DVisible() ? 1 : 0,
            IsRaycast3DVisible() ? 1 : 0);
    fprintf(f, "\"scenes\":[\n");

    for (int s = 0; s < totalCenas; s++)
    {
        CenaSnapshot *c = &cenas[s];
        char nomeEsc[128];
        EscapeJsonString(c->nome, nomeEsc, sizeof(nomeEsc));

        fprintf(f, "{\"name\":\"%s\",\"selected\":%d,\"objects\":[\n",
                nomeEsc, c->selectedIdSnapshot);

        for (int i = 0; i < c->totalObjetosSnapshot; i++)
        {
            ObjetoCena *o = &c->objetosSnapshot[i];
            char nomeObjEsc[128];
            char pathEsc[512];
            char modelPathToSave[512] = {0};
            EscapeJsonString(o->nome, nomeObjEsc, sizeof(nomeObjEsc));
            if (o->caminhoModelo[0] != '\0')
            {
                if (IsPrimitiveModelPath(o->caminhoModelo))
                {
                    strncpy(modelPathToSave, o->caminhoModelo, sizeof(modelPathToSave) - 1);
                    modelPathToSave[sizeof(modelPathToSave) - 1] = '\0';
                }
                else
                {
                    char safeName[64];
                    SanitizeName(o->nome, safeName, sizeof(safeName));
                    const char *ext = strrchr(o->caminhoModelo, '.');
                    if (!ext)
                        ext = "";

                    snprintf(modelPathToSave, sizeof(modelPathToSave), "assets/%s_%d%s",
                             safeName[0] ? safeName : "modelo", o->id, ext);

                    char assetAbs[512];
                    ResolveProjectPath(modelPathToSave, assetAbs, sizeof(assetAbs));

                    if (!FileExists(assetAbs))
                    {
                        if (!CopyFileBinary(o->caminhoModelo, assetAbs))
                        {
                            strncpy(modelPathToSave, o->caminhoModelo, sizeof(modelPathToSave) - 1);
                            modelPathToSave[sizeof(modelPathToSave) - 1] = '\0';
                        }
                    }
                }
            }

            EscapeJsonString(modelPathToSave, pathEsc, sizeof(pathEsc));
            char pcustomEsc[128];
            EscapeJsonString(o->protoCustomName, pcustomEsc, sizeof(pcustomEsc));
            char pcustoms[512];
            pcustoms[0] = '\0';
            for (int c = 0; c < o->protoCustomCount && c < MAX_PROTO_CUSTOM; c++)
            {
                ProtoCustomEntry *e = &o->protoCustomEntries[c];
                char nameSan[64];
                strncpy(nameSan, e->name, sizeof(nameSan) - 1);
                nameSan[sizeof(nameSan) - 1] = '\0';
                for (size_t k = 0; nameSan[k] != '\0'; k++)
                {
                    if (nameSan[k] == '|' || nameSan[k] == ';')
                        nameSan[k] = '_';
                }
                char chunk[96];
                snprintf(chunk, sizeof(chunk), "%s|%d,%d,%d|%d,%d,%d",
                         nameSan,
                         (int)e->base.r, (int)e->base.g, (int)e->base.b,
                         (int)e->secondary.r, (int)e->secondary.g, (int)e->secondary.b);
                if (pcustoms[0] != '\0')
                    strncat(pcustoms, ";", sizeof(pcustoms) - strlen(pcustoms) - 1);
                strncat(pcustoms, chunk, sizeof(pcustoms) - strlen(pcustoms) - 1);
            }
            char pcustomsEsc[640];
            EscapeJsonString(pcustoms, pcustomsEsc, sizeof(pcustomsEsc));

            fprintf(f,
                    "{\"id\":%d,\"name\":\"%s\",\"pos\":[%.4f,%.4f,%.4f],\"rot\":[%.4f,%.4f,%.4f],\"parent\":%d,\"active\":%d,\"model\":\"%s\","
                    "\"proto\":%d,\"pbase\":[%d,%d,%d],\"psec\":[%d,%d,%d],\"ppack\":%d,\"pcustom\":\"%s\","
                    "\"pcustombase\":[%d,%d,%d],\"pcustomsec\":[%d,%d,%d],\"pcustoms\":\"%s\","
                    "\"pstatic\":%d,\"prb\":%d,\"pcollider\":%d,\"pgravity\":%d,\"pmass\":%.3f,\"pshape\":%d,\"psize\":[%.3f,%.3f,%.3f],\"pterrain\":%d}%s\n",
                    o->id,
                    nomeObjEsc,
                    o->posicao.x, o->posicao.y, o->posicao.z,
                    o->rotacao.x, o->rotacao.y, o->rotacao.z,
                    o->paiId,
                    o->ativo ? 1 : 0,
                    pathEsc,
                    o->protoEnabled ? 1 : 0,
                    (int)o->protoBaseColor.r, (int)o->protoBaseColor.g, (int)o->protoBaseColor.b,
                    (int)o->protoSecondaryColor.r, (int)o->protoSecondaryColor.g, (int)o->protoSecondaryColor.b,
                    o->protoPack,
                    pcustomEsc,
                    (int)o->protoCustomBase.r, (int)o->protoCustomBase.g, (int)o->protoCustomBase.b,
                    (int)o->protoCustomSecondary.r, (int)o->protoCustomSecondary.g, (int)o->protoCustomSecondary.b,
                    pcustomsEsc,
                    o->physStatic ? 1 : 0,
                    o->physRigidbody ? 1 : 0,
                    o->physCollider ? 1 : 0,
                    o->physGravity ? 1 : 0,
                    o->physMass,
                    o->physShape,
                    o->physSize.x, o->physSize.y, o->physSize.z,
                    o->physTerrain ? 1 : 0,
                    (i == c->totalObjetosSnapshot - 1) ? "" : ",");
        }

        fprintf(f, "]}%s\n", (s == totalCenas - 1) ? "" : ",");
    }

    fprintf(f, "]\n}\n");
    fclose(f);

    if (projectDir[0] != '\0')
    {
        snprintf(pendingProjectIconPath, sizeof(pendingProjectIconPath), "%s/icon.png", projectDir);
        pendingProjectIconPath[sizeof(pendingProjectIconPath) - 1] = '\0';
    }

    return true;
}

bool SaveProjectAs(const char *name)
{
    if (!name || name[0] == '\0')
        return false;

    char root[256];
    if (!EnsureProjectsRoot(root, sizeof(root)))
        return false;

    char safeName[64];
    SanitizeName(name, safeName, sizeof(safeName));
    if (safeName[0] == '\0')
        return false;

    snprintf(projectDir, sizeof(projectDir), "%s/%s", root, safeName);
#ifdef _WIN32
    for (size_t i = 0; projectDir[i] != '\0'; i++)
        if (projectDir[i] == '/')
            projectDir[i] = '\\';
#endif

    if (!EnsureAssetsDirAt(projectDir))
        return false;

    snprintf(projectPath, sizeof(projectPath), "%s/project.json", projectDir);
    projectPath[sizeof(projectPath) - 1] = '\0';

    bool ok = SaveProject();
    if (ok)
        AddRecentProject(projectPath);
    return ok;
}

bool OpenProject(const char *path)
{
    if (!path || path[0] == '\0')
        return false;
    return LoadProject(path);
}

static bool ExtractJsonString(const char *line, const char *key, char *out, size_t outSize)
{
    const char *p = strstr(line, key);
    if (!p)
        return false;
    p += strlen(key);
    const char *end = strchr(p, '"');
    if (!end)
        return false;

    size_t len = (size_t)(end - p);
    if (len >= outSize)
        len = outSize - 1;
    strncpy(out, p, len);
    out[len] = '\0';
    return true;
}

bool LoadProject(const char *path)
{
    if (!path || path[0] == '\0')
        return false;

    FILE *f = fopen(path, "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0)
    {
        fclose(f);
        return false;
    }

    char *buffer = (char *)malloc((size_t)size + 1);
    if (!buffer)
    {
        fclose(f);
        return false;
    }

    size_t read = fread(buffer, 1, (size_t)size, f);
    buffer[read] = '\0';
    fclose(f);

    totalCenas = 0;
    cenaAtiva = -1;

    int activeFromFile = 0;
    loadedProjectCameraPending = false;

    strncpy(projectPath, path, sizeof(projectPath) - 1);
    projectPath[sizeof(projectPath) - 1] = '\0';
    GetDirectoryFromPath(projectPath, projectDir, sizeof(projectDir));
    TraceLog(LOG_INFO, "Project path: %s", projectPath);
    TraceLog(LOG_INFO, "Project dir: %s", projectDir);

    // Ler cena ativa
    const char *activePtr = strstr(buffer, "\"active\":");
    if (activePtr)
        sscanf(activePtr, "\"active\":%d", &activeFromFile);

    // Ler camera do projeto (compativel: pode nao existir em projetos antigos)
    const char *cameraPtr = strstr(buffer, "\"camera\":{");
    if (cameraPtr)
    {
        const char *camPosPtr = strstr(cameraPtr, "\"pos\":[");
        const char *camTargetPtr = strstr(cameraPtr, "\"target\":[");
        Vector3 pos = projectCameraPos;
        Vector3 target = projectCameraTarget;
        bool okPos = false;
        bool okTarget = false;
        if (camPosPtr)
            okPos = (sscanf(camPosPtr, "\"pos\":[%f,%f,%f]", &pos.x, &pos.y, &pos.z) == 3);
        if (camTargetPtr)
            okTarget = (sscanf(camTargetPtr, "\"target\":[%f,%f,%f]", &target.x, &target.y, &target.z) == 3);
        if (okPos && okTarget)
        {
            projectCameraPos = pos;
            projectCameraTarget = target;
            loadedProjectCameraPending = true;
        }
    }

    // Ler configuracoes de editor (compativel: pode nao existir em projetos antigos)
    const char *editorPtr = strstr(buffer, "\"editor\":{");
    if (editorPtr)
    {
        int showCollisions = PropertiesShowCollisions() ? 1 : 0;
        int ray2D = IsRaycast2DVisible() ? 1 : 0;
        int ray3D = IsRaycast3DVisible() ? 1 : 0;

        const char *showPtr = strstr(editorPtr, "\"showCollisions\":");
        const char *ray2DPtr = strstr(editorPtr, "\"ray2D\":");
        const char *ray3DPtr = strstr(editorPtr, "\"ray3D\":");

        if (showPtr)
            sscanf(showPtr, "\"showCollisions\":%d", &showCollisions);
        if (ray2DPtr)
            sscanf(ray2DPtr, "\"ray2D\":%d", &ray2D);
        if (ray3DPtr)
            sscanf(ray3DPtr, "\"ray3D\":%d", &ray3D);

        SetPropertiesShowCollisions(showCollisions != 0);
        SetRaycast2DVisible(ray2D != 0);
        SetRaycast3DVisible(ray3D != 0);
    }

    // Parse de cenas e objetos
    const char *cur = buffer;
    while (cur)
    {
        const char *namePtr = strstr(cur, "\"name\":\"");
        if (!namePtr)
            break;
        const char *objectsPtr = strstr(namePtr, "\"objects\":[");
        if (!objectsPtr)
            break;

        if (totalCenas >= MAX_SCENES)
            break;

        // Nome da cena
        char nomeEsc[128] = {0};
        ExtractJsonString(namePtr, "\"name\":\"", nomeEsc, sizeof(nomeEsc));

        CenaSnapshot *c = &cenas[totalCenas];
        UnescapeJsonString(nomeEsc, c->nome, sizeof(c->nome));
        c->totalObjetosSnapshot = 0;
        c->proximoIdSnapshot = 1;
        c->selectedIdSnapshot = -1;

        // Selected
        const char *selPtr = strstr(namePtr, "\"selected\":");
        if (selPtr && selPtr < objectsPtr)
        {
            int sel = -1;
            if (sscanf(selPtr, "\"selected\":%d", &sel) == 1)
                c->selectedIdSnapshot = sel;
        }
        int currentSceneIndex = totalCenas;
        totalCenas++;
        TraceLog(LOG_INFO, "Scene loaded: %s (index %d)", c->nome, currentSceneIndex);

        // Objetos da cena
        const char *objCur = objectsPtr + strlen("\"objects\":[");
        while (objCur && *objCur)
        {
            while (*objCur == ' ' || *objCur == '\t' || *objCur == '\n' || *objCur == '\r' || *objCur == ',')
                objCur++;

            if (*objCur == ']')
                break;

            const char *objStart = strchr(objCur, '{');
            if (!objStart)
                break;

            const char *objEnd = strchr(objStart, '}');
            if (!objEnd)
                break;

            size_t len = (size_t)(objEnd - objStart + 1);
            char objLine[1024];
            if (len >= sizeof(objLine))
                len = sizeof(objLine) - 1;
            strncpy(objLine, objStart, len);
            objLine[len] = '\0';

            TraceLog(LOG_INFO, "Object line: %s", objLine);

            CenaSnapshot *cs = &cenas[currentSceneIndex];
            if (cs->totalObjetosSnapshot < MAX_OBJETOS)
            {
                int id = -1;
                int parent = -1;
                int active = 1;
                float px = 0, py = 0, pz = 0;
                float rx = 0, ry = 0, rz = 0;
                char nomeObjEsc[128] = {0};
                char modelEsc[512] = {0};
                char pcustomEsc[128] = {0};
                char pcustomsEsc[512] = {0};
                int proto = 0;
                int pbr = 130, pbg = 130, pbb = 130;
                int psr = 80, psg = 80, psb = 80;
                int pcbr = -1, pcbg = -1, pcbb = -1;
                int pcsr = -1, pcsg = -1, pcsb = -1;
                int ppack = 0;
                int pstatic = 1, prb = 0, pcollider = 1, pgravity = 1, pterrain = 0;
                float pmass = 1.0f;
                float psx = 1.0f, psy = 1.0f, psz = 1.0f;
                int pshape = COLLISION_SHAPE_MESH_BOUNDS;

                if (sscanf(objLine, "{\"id\":%d", &id) == 1)
                {
                    ExtractJsonString(objLine, "\"name\":\"", nomeObjEsc, sizeof(nomeObjEsc));

                    const char *posPtr = strstr(objLine, "\"pos\":[");
                    if (posPtr)
                        sscanf(posPtr, "\"pos\":[%f,%f,%f]", &px, &py, &pz);
                    const char *rotPtr = strstr(objLine, "\"rot\":[");
                    if (rotPtr)
                        sscanf(rotPtr, "\"rot\":[%f,%f,%f]", &rx, &ry, &rz);

                    const char *parentPtr = strstr(objLine, "\"parent\":");
                    if (parentPtr)
                        sscanf(parentPtr, "\"parent\":%d", &parent);

                    const char *activePtr2 = strstr(objLine, "\"active\":");
                    if (activePtr2)
                        sscanf(activePtr2, "\"active\":%d", &active);

                    ExtractJsonString(objLine, "\"model\":\"", modelEsc, sizeof(modelEsc));
                    const char *protoPtr = strstr(objLine, "\"proto\":");
                    if (protoPtr)
                        sscanf(protoPtr, "\"proto\":%d", &proto);

                    const char *pbasePtr = strstr(objLine, "\"pbase\":[");
                    if (pbasePtr)
                        sscanf(pbasePtr, "\"pbase\":[%d,%d,%d]", &pbr, &pbg, &pbb);

                    const char *psecPtr = strstr(objLine, "\"psec\":[");
                    if (psecPtr)
                        sscanf(psecPtr, "\"psec\":[%d,%d,%d]", &psr, &psg, &psb);

                    const char *ppackPtr = strstr(objLine, "\"ppack\":");
                    if (ppackPtr)
                        sscanf(ppackPtr, "\"ppack\":%d", &ppack);
                    ExtractJsonString(objLine, "\"pcustom\":\"", pcustomEsc, sizeof(pcustomEsc));
                    ExtractJsonString(objLine, "\"pcustoms\":\"", pcustomsEsc, sizeof(pcustomsEsc));
                    const char *pcbasePtr = strstr(objLine, "\"pcustombase\":[");
                    if (pcbasePtr)
                        sscanf(pcbasePtr, "\"pcustombase\":[%d,%d,%d]", &pcbr, &pcbg, &pcbb);
                    const char *pcsecPtr = strstr(objLine, "\"pcustomsec\":[");
                    if (pcsecPtr)
                        sscanf(pcsecPtr, "\"pcustomsec\":[%d,%d,%d]", &pcsr, &pcsg, &pcsb);
                    const char *pstaticPtr = strstr(objLine, "\"pstatic\":");
                    if (pstaticPtr)
                        sscanf(pstaticPtr, "\"pstatic\":%d", &pstatic);
                    const char *prbPtr = strstr(objLine, "\"prb\":");
                    if (prbPtr)
                        sscanf(prbPtr, "\"prb\":%d", &prb);
                    const char *pcollPtr = strstr(objLine, "\"pcollider\":");
                    if (pcollPtr)
                        sscanf(pcollPtr, "\"pcollider\":%d", &pcollider);
                    const char *pgravPtr = strstr(objLine, "\"pgravity\":");
                    if (pgravPtr)
                        sscanf(pgravPtr, "\"pgravity\":%d", &pgravity);
                    const char *pmassPtr = strstr(objLine, "\"pmass\":");
                    if (pmassPtr)
                        sscanf(pmassPtr, "\"pmass\":%f", &pmass);
                    const char *pshapePtr = strstr(objLine, "\"pshape\":");
                    if (pshapePtr)
                        sscanf(pshapePtr, "\"pshape\":%d", &pshape);
                    const char *psizePtr = strstr(objLine, "\"psize\":[");
                    if (psizePtr)
                        sscanf(psizePtr, "\"psize\":[%f,%f,%f]", &psx, &psy, &psz);
                    const char *pterrPtr = strstr(objLine, "\"pterrain\":");
                    if (pterrPtr)
                        sscanf(pterrPtr, "\"pterrain\":%d", &pterrain);

                    ObjetoCena *o = &cs->objetosSnapshot[cs->totalObjetosSnapshot++];
                    memset(o, 0, sizeof(*o));
                    o->id = id;
                    UnescapeJsonString(nomeObjEsc, o->nome, sizeof(o->nome));
                    o->posicao = (Vector3){px, py, pz};
                    o->rotacao = (Vector3){rx, ry, rz};
                    o->paiId = parent;
                    o->ativo = active != 0;
                    o->selecionado = false;
                    if (ppack < 0)
                        ppack = 0;
                    o->protoEnabled = proto != 0;
                    o->protoBaseColor = (Color){ClampByte(pbr), ClampByte(pbg), ClampByte(pbb), 255};
                    o->protoSecondaryColor = (Color){ClampByte(psr), ClampByte(psg), ClampByte(psb), 255};
                    o->protoPack = ppack;
                    if (pcustomEsc[0] != '\0')
                        UnescapeJsonString(pcustomEsc, o->protoCustomName, sizeof(o->protoCustomName));
                    else
                    {
                        strncpy(o->protoCustomName, "Custom", sizeof(o->protoCustomName) - 1);
                        o->protoCustomName[sizeof(o->protoCustomName) - 1] = '\0';
                    }
                    if (pcbr >= 0 && pcbg >= 0 && pcbb >= 0)
                        o->protoCustomBase = (Color){ClampByte(pcbr), ClampByte(pcbg), ClampByte(pcbb), 255};
                    else
                        o->protoCustomBase = (Color){168, 36, 36, 255};
                    if (pcsr >= 0 && pcsg >= 0 && pcsb >= 0)
                        o->protoCustomSecondary = (Color){ClampByte(pcsr), ClampByte(pcsg), ClampByte(pcsb), 255};
                    else
                        o->protoCustomSecondary = (Color){88, 88, 88, 255};
                    o->physStatic = pstatic != 0;
                    o->physRigidbody = prb != 0;
                    o->physCollider = pcollider != 0;
                    o->physGravity = pgravity != 0;
                    o->physMass = pmass;
                    o->physShape = pshape;
                    o->physSize = (Vector3){psx, psy, psz};
                    o->physTerrain = pterrain != 0;
                    o->protoCustomCount = 0;
                    if (pcustomsEsc[0] != '\0')
                    {
                        char list[512];
                        UnescapeJsonString(pcustomsEsc, list, sizeof(list));
                        char *ctx = nullptr;
                        char *entry = strtok_s(list, ";", &ctx);
                        while (entry && o->protoCustomCount < MAX_PROTO_CUSTOM)
                        {
                            char name[32] = {0};
                            int br = -1, bg = -1, bb = -1;
                            int sr = -1, sg = -1, sb = -1;
                            if (sscanf(entry, "%31[^|]|%d,%d,%d|%d,%d,%d", name, &br, &bg, &bb, &sr, &sg, &sb) == 7)
                            {
                                ProtoCustomEntry *e = &o->protoCustomEntries[o->protoCustomCount++];
                                strncpy(e->name, name, sizeof(e->name) - 1);
                                e->name[sizeof(e->name) - 1] = '\0';
                                e->base = (Color){ClampByte(br), ClampByte(bg), ClampByte(bb), 255};
                                e->secondary = (Color){ClampByte(sr), ClampByte(sg), ClampByte(sb), 255};
                            }
                            entry = strtok_s(nullptr, ";", &ctx);
                        }
                    }
                    TraceLog(LOG_INFO, "Object parsed: id=%d name=%s", o->id, o->nome);

                    if (modelEsc[0] != '\0')
                    {
                        char rel[512] = {0};
                        UnescapeJsonString(modelEsc, rel, sizeof(rel));
                        if (IsPrimitiveModelPath(rel))
                        {
                            strncpy(o->caminhoModelo, rel, sizeof(o->caminhoModelo) - 1);
                            o->caminhoModelo[sizeof(o->caminhoModelo) - 1] = '\0';
                        }
                        else
                        {
                            ResolveProjectPath(rel, o->caminhoModelo, sizeof(o->caminhoModelo));
                        }
                        TraceLog(LOG_INFO, "Model rel: %s", rel);
                        TraceLog(LOG_INFO, "Model abs: %s", o->caminhoModelo);
                    }
                }
            }

            objCur = objEnd + 1;
        }

        cur = objCur;
    }

    free(buffer);

    TraceLog(LOG_INFO, "Total scenes loaded: %d", totalCenas);

    if (totalCenas <= 0)
    {
        CreateNewScene();
        return false;
    }

    if (activeFromFile < 0 || activeFromFile >= totalCenas)
        activeFromFile = 0;
    cenaAtiva = activeFromFile;

    CarregarCena(cenaAtiva);
    AddRecentProject(projectPath);
    return true;
}

int GetRecentProjectCount(void)
{
    LoadRecentProjects();
    return recentProjectCount;
}

const char *GetRecentProjectPath(int index)
{
    LoadRecentProjects();
    if (index < 0 || index >= recentProjectCount)
        return "";
    return recentProjects[index];
}

bool ConsumePendingProjectIconPath(char *out, int outSize)
{
    if (!out || outSize <= 0)
        return false;
    if (pendingProjectIconPath[0] == '\0')
        return false;

    strncpy(out, pendingProjectIconPath, (size_t)outSize - 1);
    out[outSize - 1] = '\0';
    pendingProjectIconPath[0] = '\0';
    return true;
}

void SetProjectCameraState(Vector3 position, Vector3 target)
{
    projectCameraPos = position;
    projectCameraTarget = target;
}

bool ConsumeLoadedProjectCameraState(Vector3 *position, Vector3 *target)
{
    if (!loadedProjectCameraPending)
        return false;
    if (position)
        *position = projectCameraPos;
    if (target)
        *target = projectCameraTarget;
    loadedProjectCameraPending = false;
    return true;
}


