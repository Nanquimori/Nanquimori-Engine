#include "model_manager.h"
#include "scene/outliner.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

ModelManager modelManager = {0};
static char lastImportedPath[256] = {0};
static double lastImportedAt = -1000.0;
static const double kDuplicateImportGuardSeconds = 0.25;

static const char *kPrimitiveModelPaths[PRIMITIVE_MODEL_COUNT] = {
    "primitive://cube",
    "primitive://sphere",
    "primitive://cylinder",
    "primitive://plane",
    "primitive://torus"};

static const char *kPrimitiveModelNames[PRIMITIVE_MODEL_COUNT] = {
    "Cube",
    "Sphere",
    "Cylinder",
    "Plane",
    "Torus"};

typedef struct
{
    int objectId;
    Color base;
    Color secondary;
    Texture2D texture;
    bool ready;
} ProtoTextureEntry;

static ProtoTextureEntry protoTextures[MAX_OBJETOS] = {0};
static Texture2D protoFallbackTexture = {0};
static bool protoFallbackReady = false;
static Shader protoShader = {0};
static bool protoShaderReady = false;
static bool protoShaderTried = false;
static int protoShaderLocBase = -1;
static int protoShaderLocSecondary = -1;
static int protoShaderLocScale = -1;
static int protoShaderLocContrast = -1;

static const float PROTO_DEFAULT_SCALE = 1.0f;
static const float PROTO_DEFAULT_CONTRAST = 1.2f;
static void EnsureModelTexcoords(Model *model);

bool IsPrimitiveModelPath(const char *path)
{
    return path && strncmp(path, "primitive://", 12) == 0;
}

static int PrimitiveTypeFromPath(const char *path)
{
    if (!path)
        return -1;
    for (int i = 0; i < (int)PRIMITIVE_MODEL_COUNT; i++)
    {
        if (strcmp(path, kPrimitiveModelPaths[i]) == 0)
            return i;
    }
    return -1;
}

static Mesh GeneratePrimitiveMesh(PrimitiveModelType type)
{
    switch (type)
    {
    case PRIMITIVE_MODEL_CUBE:
        return GenMeshCube(1.0f, 1.0f, 1.0f);
    case PRIMITIVE_MODEL_SPHERE:
        return GenMeshSphere(0.6f, 24, 24);
    case PRIMITIVE_MODEL_CYLINDER:
        return GenMeshCylinder(0.5f, 1.2f, 24);
    case PRIMITIVE_MODEL_PLANE:
        return GenMeshPlane(1.4f, 1.4f, 2, 2);
    case PRIMITIVE_MODEL_TORUS:
        return GenMeshTorus(0.45f, 0.15f, 18, 28);
    default:
        return GenMeshCube(1.0f, 1.0f, 1.0f);
    }
}

static void BuildUniqueObjectName(const char *baseName, char *outName, size_t outSize)
{
    if (!outName || outSize == 0)
        return;
    outName[0] = '\0';

    const char *base = (baseName && baseName[0] != '\0') ? baseName : "Object";
    if (!ObjetoExisteNoOutliner(base))
    {
        strncpy(outName, base, outSize - 1);
        outName[outSize - 1] = '\0';
        return;
    }

    for (int i = 1; i < 1000; i++)
    {
        snprintf(outName, outSize, "%s %d", base, i);
        outName[outSize - 1] = '\0';
        if (!ObjetoExisteNoOutliner(outName))
            return;
    }

    strncpy(outName, base, outSize - 1);
    outName[outSize - 1] = '\0';
}

static bool TryPrototypeShaderPath(const char *baseDir, char *vsOut, size_t vsSize, char *fsOut, size_t fsSize)
{
    if (!baseDir || baseDir[0] == '\0')
        return false;
    snprintf(vsOut, vsSize, "%s/shaders/procedural_axis_projection.vs", baseDir);
    snprintf(fsOut, fsSize, "%s/shaders/procedural_axis_projection.fs", baseDir);
    vsOut[vsSize - 1] = '\0';
    fsOut[fsSize - 1] = '\0';
    return FileExists(vsOut) && FileExists(fsOut);
}

static bool TryPrototypeShaderPathChain(const char *startDir, char *vsOut, size_t vsSize, char *fsOut, size_t fsSize)
{
    if (!startDir || startDir[0] == '\0')
        return false;

    char current[512];
    strncpy(current, startDir, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';

    for (int i = 0; i < 6; i++)
    {
        if (TryPrototypeShaderPath(current, vsOut, vsSize, fsOut, fsSize))
            return true;

        char next[512];
        snprintf(next, sizeof(next), "%s/..", current);
        next[sizeof(next) - 1] = '\0';
        if (strcmp(next, current) == 0)
            break;
        strncpy(current, next, sizeof(current) - 1);
        current[sizeof(current) - 1] = '\0';
    }

    return false;
}

static bool ResolvePrototypeShaderPaths(char *vsOut, size_t vsSize, char *fsOut, size_t fsSize)
{
    const char *cwd = GetWorkingDirectory();
    const char *appDir = GetApplicationDirectory();
    if (TryPrototypeShaderPathChain(cwd, vsOut, vsSize, fsOut, fsSize))
        return true;
    if (TryPrototypeShaderPathChain(appDir, vsOut, vsSize, fsOut, fsSize))
        return true;

    return false;
}

static int EncontrarModeloPorIdObjeto(int idObjeto)
{
    for (int i = 0; i < modelManager.modelCount; i++)
        if (modelManager.models[i].idObjeto == idObjeto)
            return i;
    return -1;
}

static int EncontrarCachePorFilepath(const char *filepath)
{
    if (!filepath)
        return -1;
    for (int i = 0; i < modelManager.cacheCount; i++)
    {
        if (modelManager.cache[i].loaded && strcmp(modelManager.cache[i].filepath, filepath) == 0)
            return i;
    }
    return -1;
}

static int AdquirirModeloCache(const char *filepath)
{
    if (!filepath)
        return -1;

    int idx = EncontrarCachePorFilepath(filepath);
    if (idx != -1)
    {
        modelManager.cache[idx].refCount++;
        return idx;
    }

    int slot = -1;
    bool newSlot = false;
    if (modelManager.cacheCount < MAX_MODEL_CACHE)
    {
        slot = modelManager.cacheCount;
        newSlot = true;
    }
    else
    {
        for (int i = 0; i < modelManager.cacheCount; i++)
        {
            if (modelManager.cache[i].loaded && modelManager.cache[i].refCount == 0)
            {
                UnloadModel(modelManager.cache[i].model);
                modelManager.cache[i].loaded = false;
                modelManager.cache[i].refCount = 0;
                slot = i;
                break;
            }
        }
    }

    if (slot == -1)
        return -1;

    int primitiveType = PrimitiveTypeFromPath(filepath);
    Model newModel = {0};
    if (primitiveType >= 0)
    {
        Mesh primitiveMesh = GeneratePrimitiveMesh((PrimitiveModelType)primitiveType);
        newModel = LoadModelFromMesh(primitiveMesh);
    }
    else
    {
        newModel = LoadModel(filepath);
    }

    if (newModel.meshCount <= 0)
    {
        TraceLog(LOG_WARNING, "Falha ao carregar modelo: %s", filepath);
        return -1;
    }
    EnsureModelTexcoords(&newModel);

    if (newSlot)
        modelManager.cacheCount++;

    CachedModel *cm = &modelManager.cache[slot];
    cm->model = newModel;
    cm->loaded = true;
    cm->refCount = 1;
    strncpy(cm->filepath, filepath, 255);
    cm->filepath[255] = '\0';
    return slot;
}

static void LiberarModeloCache(int cacheIndex)
{
    if (cacheIndex < 0 || cacheIndex >= modelManager.cacheCount)
        return;
    if (modelManager.cache[cacheIndex].refCount > 0)
        modelManager.cache[cacheIndex].refCount--;
}

static Color Opaque(Color c)
{
    c.a = 255;
    return c;
}

static bool SameColor(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void ClearProtoTextureEntry(ProtoTextureEntry *entry)
{
    if (!entry)
        return;
    if (entry->ready)
        UnloadTexture(entry->texture);
    *entry = (ProtoTextureEntry){0};
}

static void InitPrototypeShader(void)
{
    if (protoShaderReady)
        return;

    char vsPath[512];
    char fsPath[512];
    if (ResolvePrototypeShaderPaths(vsPath, sizeof(vsPath), fsPath, sizeof(fsPath)))
    {
        if (!protoShaderTried)
            TraceLog(LOG_INFO, "Prototype shader: %s | %s", vsPath, fsPath);
        protoShaderTried = true;
        protoShader = LoadShader(vsPath, fsPath);
    }
    else
        return;
    if (protoShader.id == 0)
    {
        TraceLog(LOG_WARNING, "Prototype shader load failed.");
        protoShader = (Shader){0};
        return;
    }

    unsigned int defaultId = rlGetShaderIdDefault();
    if (protoShader.id == defaultId)
    {
        TraceLog(LOG_WARNING, "Prototype shader compile failed (default shader returned).");
        protoShader = (Shader){0};
        return;
    }

    protoShaderLocBase = GetShaderLocation(protoShader, "cor_base");
    protoShaderLocSecondary = GetShaderLocation(protoShader, "cor_secundaria");
    protoShaderLocScale = GetShaderLocation(protoShader, "escala");
    protoShaderLocContrast = GetShaderLocation(protoShader, "contraste");
    if (protoShaderLocBase < 0 || protoShaderLocSecondary < 0 || protoShaderLocScale < 0 || protoShaderLocContrast < 0)
    {
        TraceLog(LOG_WARNING, "Prototype shader uniforms missing.");
        protoShader = (Shader){0};
        return;
    }
    protoShaderReady = true;
}

static bool MeshHasValidTexcoords(const Mesh *mesh)
{
    if (!mesh || !mesh->texcoords || mesh->vertexCount <= 0)
        return false;
    int count = mesh->vertexCount * 2;
    float minU = mesh->texcoords[0];
    float maxU = mesh->texcoords[0];
    float minV = mesh->texcoords[1];
    float maxV = mesh->texcoords[1];

    for (int i = 0; i < count; i += 2)
    {
        float u = mesh->texcoords[i];
        float v = mesh->texcoords[i + 1];
        if (!(u == u) || !(v == v))
            return false;
        if (u < minU)
            minU = u;
        if (u > maxU)
            maxU = u;
        if (v < minV)
            minV = v;
        if (v > maxV)
            maxV = v;
    }

    const float epsilon = 0.0001f;
    if ((maxU - minU) < epsilon || (maxV - minV) < epsilon)
        return false;

    return true;
}

static void GeneratePlanarTexcoords(Mesh *mesh)
{
    if (!mesh || !mesh->vertices || mesh->vertexCount <= 0)
        return;

    if (!mesh->texcoords)
        mesh->texcoords = (float *)MemAlloc(sizeof(float) * mesh->vertexCount * 2);
    if (!mesh->texcoords)
        return;

    float minX = mesh->vertices[0];
    float maxX = mesh->vertices[0];
    float minY = mesh->vertices[1];
    float maxY = mesh->vertices[1];
    float minZ = mesh->vertices[2];
    float maxZ = mesh->vertices[2];

    for (int i = 0; i < mesh->vertexCount; i++)
    {
        float x = mesh->vertices[i * 3 + 0];
        float y = mesh->vertices[i * 3 + 1];
        float z = mesh->vertices[i * 3 + 2];
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        if (z < minZ) minZ = z;
        if (z > maxZ) maxZ = z;
    }

    float sizeX = maxX - minX;
    float sizeY = maxY - minY;
    float sizeZ = maxZ - minZ;
    float areaXY = sizeX * sizeY;
    float areaXZ = sizeX * sizeZ;
    float areaYZ = sizeY * sizeZ;

    int axisU = 0; // 0 = X, 1 = Y, 2 = Z
    int axisV = 2;
    if (areaXY >= areaXZ && areaXY >= areaYZ)
    {
        axisU = 0;
        axisV = 1;
    }
    else if (areaXZ >= areaXY && areaXZ >= areaYZ)
    {
        axisU = 0;
        axisV = 2;
    }
    else
    {
        axisU = 1;
        axisV = 2;
    }

    // Xadrez em escala "real": 1 unidade do modelo ~= 1 metro.
    // A textura tem 8 checks por 1.0 de UV (GenImageChecked 64x64, 8x8).
    // Para 1 check por metro: UV = metros * (1 / 8).
    const float checkSizeMeters = 1.0f;
    const float checksPerTexture = 8.0f;
    float scale = 1.0f / (checkSizeMeters * checksPerTexture);

    bool hasNormals = (mesh->normals != NULL);
    for (int i = 0; i < mesh->vertexCount; i++)
    {
        float x = mesh->vertices[i * 3 + 0];
        float y = mesh->vertices[i * 3 + 1];
        float z = mesh->vertices[i * 3 + 2];
        float u = 0.0f;
        float v = 0.0f;

        if (hasNormals)
        {
            float nx = mesh->normals[i * 3 + 0];
            float ny = mesh->normals[i * 3 + 1];
            float nz = mesh->normals[i * 3 + 2];
            float ax = fabsf(nx);
            float ay = fabsf(ny);
            float az = fabsf(nz);

            if (ax >= ay && ax >= az)
            {
                u = (y - minY);
                v = (z - minZ);
            }
            else if (ay >= ax && ay >= az)
            {
                u = (x - minX);
                v = (z - minZ);
            }
            else
            {
                u = (x - minX);
                v = (y - minY);
            }
        }
        else
        {
            if (axisU == 0) u = x - minX;
            else if (axisU == 1) u = y - minY;
            else u = z - minZ;

            if (axisV == 0) v = x - minX;
            else if (axisV == 1) v = y - minY;
            else v = z - minZ;
        }

        mesh->texcoords[i * 2 + 0] = u * scale;
        mesh->texcoords[i * 2 + 1] = v * scale;
    }

    if (mesh->vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD] != 0)
    {
        UpdateMeshBuffer(*mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_TEXCOORD,
                         mesh->texcoords, mesh->vertexCount * 2 * sizeof(float), 0);
    }
}

static void EnsureModelTexcoords(Model *model)
{
    if (!model || !model->meshes || model->meshCount <= 0)
        return;
    for (int i = 0; i < model->meshCount; i++)
    {
        Mesh *mesh = &model->meshes[i];
        if (!MeshHasValidTexcoords(mesh))
            GeneratePlanarTexcoords(mesh);
    }
}

static void ResetPrototypeTextures(void)
{
    for (int i = 0; i < MAX_OBJETOS; i++)
        ClearProtoTextureEntry(&protoTextures[i]);
}

static Texture2D GetPrototypeFallbackTexture(void)
{
    if (!protoFallbackReady)
    {
        Image img = GenImageColor(1, 1, WHITE);
        protoFallbackTexture = LoadTextureFromImage(img);
        if (protoFallbackTexture.id != 0)
        {
            SetTextureWrap(protoFallbackTexture, RL_TEXTURE_WRAP_REPEAT);
            SetTextureFilter(protoFallbackTexture, RL_TEXTURE_FILTER_POINT);
        }
        UnloadImage(img);
        protoFallbackReady = (protoFallbackTexture.id != 0);
    }
    return protoFallbackTexture;
}

static void UnloadPrototypeTextures(void)
{
    ResetPrototypeTextures();
    protoShaderTried = false;
    if (protoFallbackReady)
    {
        UnloadTexture(protoFallbackTexture);
        protoFallbackTexture = (Texture2D){0};
        protoFallbackReady = false;
    }
    if (protoShaderReady)
    {
        UnloadShader(protoShader);
        protoShader = (Shader){0};
        protoShaderReady = false;
        protoShaderTried = false;
        protoShaderLocBase = -1;
        protoShaderLocSecondary = -1;
        protoShaderLocScale = -1;
        protoShaderLocContrast = -1;
    }
}

static int FindProtoTextureSlot(int objectId)
{
    int freeSlot = -1;
    for (int i = 0; i < MAX_OBJETOS; i++)
    {
        if (protoTextures[i].objectId == objectId)
            return i;
        if (freeSlot == -1 && protoTextures[i].objectId == 0)
            freeSlot = i;
    }
    if (freeSlot != -1)
        return freeSlot;
    return 0;
}

static void ReleasePrototypeTextureForObject(int objectId)
{
    if (objectId <= 0)
        return;
    for (int i = 0; i < MAX_OBJETOS; i++)
    {
        if (protoTextures[i].objectId == objectId)
        {
            ClearProtoTextureEntry(&protoTextures[i]);
            return;
        }
    }
}

static bool GetPrototypeCheckerTexture(int objectId, Color base, Color secondary, Texture2D *outTexture)
{
    if (!outTexture)
        return false;

    *outTexture = GetPrototypeFallbackTexture();

    if (objectId <= 0)
        return false;

    Color baseOpaque = Opaque(base);
    Color secondaryOpaque = Opaque(secondary);

    int slot = FindProtoTextureSlot(objectId);
    if (slot < 0 || slot >= MAX_OBJETOS)
        return false;

    ProtoTextureEntry *entry = &protoTextures[slot];
    if (entry->objectId != objectId)
    {
        ClearProtoTextureEntry(entry);
        entry->objectId = objectId;
    }

    if (entry->ready && SameColor(entry->base, baseOpaque) && SameColor(entry->secondary, secondaryOpaque))
    {
        *outTexture = entry->texture;
        return true;
    }

    if (entry->ready)
    {
        UnloadTexture(entry->texture);
        entry->ready = false;
    }

    Image img = GenImageChecked(64, 64, 8, 8, baseOpaque, secondaryOpaque);
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);

    if (tex.id != 0)
    {
        SetTextureWrap(tex, RL_TEXTURE_WRAP_REPEAT);
        SetTextureFilter(tex, RL_TEXTURE_FILTER_POINT);
        entry->texture = tex;
        entry->base = baseOpaque;
        entry->secondary = secondaryOpaque;
        entry->ready = true;
        *outTexture = tex;
        return true;
    }

    return false;
}

static void DrawModelPrototype(Model *model, int objectId, Color baseColor, Color secondaryColor)
{
    if (!model)
        return;

    (void)objectId;
    InitPrototypeShader();

    const int materialCount = model->materialCount;
    if (materialCount <= 0 || !model->materials)
    {
        if (protoShaderReady)
        {
            float base[4] = {(float)baseColor.r / 255.0f, (float)baseColor.g / 255.0f, (float)baseColor.b / 255.0f, 1.0f};
            float secondary[4] = {(float)secondaryColor.r / 255.0f, (float)secondaryColor.g / 255.0f, (float)secondaryColor.b / 255.0f, 1.0f};
            float scale = PROTO_DEFAULT_SCALE;
            float contrast = PROTO_DEFAULT_CONTRAST;
            if (protoShaderLocBase >= 0)
                SetShaderValue(protoShader, protoShaderLocBase, base, SHADER_UNIFORM_VEC4);
            if (protoShaderLocSecondary >= 0)
                SetShaderValue(protoShader, protoShaderLocSecondary, secondary, SHADER_UNIFORM_VEC4);
            if (protoShaderLocScale >= 0)
                SetShaderValue(protoShader, protoShaderLocScale, &scale, SHADER_UNIFORM_FLOAT);
            if (protoShaderLocContrast >= 0)
                SetShaderValue(protoShader, protoShaderLocContrast, &contrast, SHADER_UNIFORM_FLOAT);

            BeginShaderMode(protoShader);
            DrawModel(*model, (Vector3){0, 0, 0}, 1.0f, WHITE);
            EndShaderMode();
        }
        else
        {
            DrawModel(*model, (Vector3){0, 0, 0}, 1.0f, baseColor);
            DrawModelWires(*model, (Vector3){0, 0, 0}, 1.0f, secondaryColor);
        }
        return;
    }

    Color *oldColors = (Color *)malloc(sizeof(Color) * materialCount);
    Texture2D *oldTextures = (Texture2D *)malloc(sizeof(Texture2D) * materialCount);
    Shader *oldShaders = (Shader *)malloc(sizeof(Shader) * materialCount);
    if (!oldColors || !oldTextures || !oldShaders)
    {
        if (oldColors) free(oldColors);
        if (oldTextures) free(oldTextures);
        if (oldShaders) free(oldShaders);
        if (protoShaderReady)
        {
            float base[4] = {(float)baseColor.r / 255.0f, (float)baseColor.g / 255.0f, (float)baseColor.b / 255.0f, 1.0f};
            float secondary[4] = {(float)secondaryColor.r / 255.0f, (float)secondaryColor.g / 255.0f, (float)secondaryColor.b / 255.0f, 1.0f};
            float scale = PROTO_DEFAULT_SCALE;
            float contrast = PROTO_DEFAULT_CONTRAST;
            if (protoShaderLocBase >= 0)
                SetShaderValue(protoShader, protoShaderLocBase, base, SHADER_UNIFORM_VEC4);
            if (protoShaderLocSecondary >= 0)
                SetShaderValue(protoShader, protoShaderLocSecondary, secondary, SHADER_UNIFORM_VEC4);
            if (protoShaderLocScale >= 0)
                SetShaderValue(protoShader, protoShaderLocScale, &scale, SHADER_UNIFORM_FLOAT);
            if (protoShaderLocContrast >= 0)
                SetShaderValue(protoShader, protoShaderLocContrast, &contrast, SHADER_UNIFORM_FLOAT);

            BeginShaderMode(protoShader);
            DrawModel(*model, (Vector3){0, 0, 0}, 1.0f, WHITE);
            EndShaderMode();
        }
        else
        {
            DrawModel(*model, (Vector3){0, 0, 0}, 1.0f, baseColor);
            DrawModelWires(*model, (Vector3){0, 0, 0}, 1.0f, secondaryColor);
        }
        return;
    }

    float base[4] = {(float)baseColor.r / 255.0f, (float)baseColor.g / 255.0f, (float)baseColor.b / 255.0f, 1.0f};
    float secondary[4] = {(float)secondaryColor.r / 255.0f, (float)secondaryColor.g / 255.0f, (float)secondaryColor.b / 255.0f, 1.0f};
    float scale = PROTO_DEFAULT_SCALE;
    float contrast = PROTO_DEFAULT_CONTRAST;
    bool useShader = protoShaderReady;
    if (!useShader)
    {
        DrawModel(*model, (Vector3){0, 0, 0}, 1.0f, baseColor);
        DrawModelWires(*model, (Vector3){0, 0, 0}, 1.0f, secondaryColor);
        return;
    }
    if (useShader)
    {
        if (protoShaderLocBase >= 0)
            SetShaderValue(protoShader, protoShaderLocBase, base, SHADER_UNIFORM_VEC4);
        if (protoShaderLocSecondary >= 0)
            SetShaderValue(protoShader, protoShaderLocSecondary, secondary, SHADER_UNIFORM_VEC4);
        if (protoShaderLocScale >= 0)
            SetShaderValue(protoShader, protoShaderLocScale, &scale, SHADER_UNIFORM_FLOAT);
        if (protoShaderLocContrast >= 0)
            SetShaderValue(protoShader, protoShaderLocContrast, &contrast, SHADER_UNIFORM_FLOAT);
    }

    for (int i = 0; i < materialCount; i++)
    {
        Material *mat = &model->materials[i];
        oldColors[i] = mat->maps[MATERIAL_MAP_ALBEDO].color;
        oldTextures[i] = mat->maps[MATERIAL_MAP_ALBEDO].texture;
        oldShaders[i] = mat->shader;
        mat->maps[MATERIAL_MAP_ALBEDO].color = WHITE;
        mat->maps[MATERIAL_MAP_ALBEDO].texture = (Texture2D){0};
        if (useShader)
            mat->shader = protoShader;
    }

    DrawModel(*model, (Vector3){0, 0, 0}, 1.0f, WHITE);

    for (int i = 0; i < materialCount; i++)
    {
        Material *mat = &model->materials[i];
        mat->maps[MATERIAL_MAP_ALBEDO].color = oldColors[i];
        mat->maps[MATERIAL_MAP_ALBEDO].texture = oldTextures[i];
        mat->shader = oldShaders[i];
    }

    free(oldColors);
    free(oldTextures);
    free(oldShaders);
}

static void DrawModelViewportWireframe(Model *model, Color wireColor)
{
    if (!model)
        return;

    DrawModelWires(*model, (Vector3){0, 0, 0}, 1.0f, wireColor);
}

static BoundingBox TransformBoundingBox(BoundingBox localBox, Matrix transform)
{
    Vector3 corners[8] = {
        {localBox.min.x, localBox.min.y, localBox.min.z},
        {localBox.max.x, localBox.min.y, localBox.min.z},
        {localBox.min.x, localBox.max.y, localBox.min.z},
        {localBox.max.x, localBox.max.y, localBox.min.z},
        {localBox.min.x, localBox.min.y, localBox.max.z},
        {localBox.max.x, localBox.min.y, localBox.max.z},
        {localBox.min.x, localBox.max.y, localBox.max.z},
        {localBox.max.x, localBox.max.y, localBox.max.z}};

    Vector3 first = Vector3Transform(corners[0], transform);
    BoundingBox worldBox = {first, first};

    for (int i = 1; i < 8; i++)
    {
        Vector3 point = Vector3Transform(corners[i], transform);
        worldBox.min.x = fminf(worldBox.min.x, point.x);
        worldBox.min.y = fminf(worldBox.min.y, point.y);
        worldBox.min.z = fminf(worldBox.min.z, point.z);
        worldBox.max.x = fmaxf(worldBox.max.x, point.x);
        worldBox.max.y = fmaxf(worldBox.max.y, point.y);
        worldBox.max.z = fmaxf(worldBox.max.z, point.z);
    }

    return worldBox;
}

static Matrix BuildObjectWorldMatrix(Vector3 pos, Vector3 rot, Vector3 escala)
{
    Matrix world = MatrixTranslate(pos.x, pos.y, pos.z);
    if (rot.x != 0.0f || rot.y != 0.0f || rot.z != 0.0f)
        world = MatrixMultiply(world, MatrixRotateXYZ(rot));
    if (escala.x != 1.0f || escala.y != 1.0f || escala.z != 1.0f)
        world = MatrixMultiply(world, MatrixScale(escala.x, escala.y, escala.z));
    return world;
}

void InitModelManager(void)
{
    modelManager.modelCount = 0;
    modelManager.cacheCount = 0;
    modelManager.selectedModel = -1;
    ResetPrototypeTextures();
    protoShaderReady = false;
    protoShaderTried = false;
    InitPrototypeShader();
}

bool IsValidModelFile(const char *filepath)
{
    if (!filepath)
        return false;
    if (IsPrimitiveModelPath(filepath))
        return true;

    const char *ext = strrchr(filepath, '.');
    if (!ext)
        return false;

    ext++;

    if (strcmp(ext, "obj") == 0 || strcmp(ext, "OBJ") == 0 ||
        strcmp(ext, "glb") == 0 || strcmp(ext, "GLB") == 0 ||
        strcmp(ext, "gltf") == 0 || strcmp(ext, "GLTF") == 0 ||
        strcmp(ext, "fbx") == 0 || strcmp(ext, "FBX") == 0 ||
        strcmp(ext, "iqm") == 0 || strcmp(ext, "IQM") == 0)
    {
        return true;
    }

    return false;
}

void LoadModelFromFile(const char *filepath)
{
    double now = GetTime();
    if (filepath && lastImportedPath[0] != '\0' && strcmp(lastImportedPath, filepath) == 0)
    {
        if ((now - lastImportedAt) < kDuplicateImportGuardSeconds)
            return;
    }

    if (!filepath || modelManager.modelCount >= MAX_MODELS)
        return;

    if (!IsValidModelFile(filepath))
    {
        fprintf(stderr, "Formato de arquivo não suportado: %s\n", filepath);
        return;
    }

    int cacheIndex = AdquirirModeloCache(filepath);
    if (cacheIndex == -1)
        return;

    LoadedModel *lm = &modelManager.models[modelManager.modelCount];
    lm->cacheIndex = cacheIndex;
    lm->loaded = true;

    // Extrair nome do arquivo
    const char *nomeArquivo = strrchr(filepath, '/');
    if (!nomeArquivo)
        nomeArquivo = strrchr(filepath, '\\');
    if (nomeArquivo)
        nomeArquivo++;
    else
        nomeArquivo = filepath;

    // Remover extensão do nome
    char nomeSemExtensao[32] = {0};
    strncpy(nomeSemExtensao, nomeArquivo, 31);
    nomeSemExtensao[31] = '\0';

    // Procurar pelo ponto para remover extensão
    char *ponto = strrchr(nomeSemExtensao, '.');
    if (ponto)
        *ponto = '\0';

    // Armazenar nome do objeto
    strncpy(lm->nomeObjeto, nomeSemExtensao, 31);
    lm->nomeObjeto[31] = '\0';

    // Registrar no outliner com filepath
    int idObjeto = RegistrarObjeto(nomeSemExtensao, (Vector3){0, 0, 0}, -1);
    lm->idObjeto = idObjeto; // Armazenar o ID do objeto

    // Armazenar filepath no objeto do outliner
    int idxObjeto = BuscarIndicePorId(idObjeto);
    if (idxObjeto != -1)
    {
        strncpy(objetos[idxObjeto].caminhoModelo, filepath, 255);
        objetos[idxObjeto].caminhoModelo[255] = '\0';
    }

    modelManager.modelCount++;
    strncpy(lastImportedPath, filepath, sizeof(lastImportedPath) - 1);
    lastImportedPath[sizeof(lastImportedPath) - 1] = '\0';
    lastImportedAt = now;

    if (idObjeto > 0)
    {
        SelecionarObjetoPorId(idObjeto);
        SetSelectedModelByObjetoId(idObjeto);
    }

    TraceLog(LOG_INFO, "Modelo carregado com sucesso: %s", filepath);
}

void RemoverModeloPorNome(const char *nome)
{
    if (!nome || strlen(nome) == 0)
        return;

    for (int i = 0; i < modelManager.modelCount; i++)
    {
        if (strcmp(modelManager.models[i].nomeObjeto, nome) == 0)
        {
            if (modelManager.models[i].loaded)
                LiberarModeloCache(modelManager.models[i].cacheIndex);

            // Remover e reorganizar array
            for (int j = i; j < modelManager.modelCount - 1; j++)
            {
                modelManager.models[j] = modelManager.models[j + 1];
            }

            modelManager.modelCount--;

            SetSelectedModelByObjetoId(ObterObjetoSelecionadoId());

            TraceLog(LOG_INFO, "Modelo removido: %s", nome);
            return;
        }
    }
}

void RemoverModeloPorIdObjeto(int idObjeto)
{
    if (idObjeto <= 0)
        return;

    for (int i = 0; i < modelManager.modelCount; i++)
    {
        if (modelManager.models[i].idObjeto == idObjeto)
        {
            if (modelManager.models[i].loaded)
                LiberarModeloCache(modelManager.models[i].cacheIndex);

            // Remover e reorganizar array
            for (int j = i; j < modelManager.modelCount - 1; j++)
            {
                modelManager.models[j] = modelManager.models[j + 1];
            }

            modelManager.modelCount--;

            SetSelectedModelByObjetoId(ObterObjetoSelecionadoId());

            TraceLog(LOG_INFO, "Modelo removido (ID: %d)", idObjeto);
            ReleasePrototypeTextureForObject(idObjeto);
            return;
        }
    }
}

void RenderModels(bool wireframeMode)
{
    if (wireframeMode)
        rlDisableBackfaceCulling();

    for (int i = 0; i < modelManager.modelCount; i++)
    {
        LoadedModel *lm = &modelManager.models[i];
        if (!lm->loaded)
            continue;

        if (lm->cacheIndex < 0 || lm->cacheIndex >= modelManager.cacheCount)
            continue;
        if (!modelManager.cache[lm->cacheIndex].loaded)
            continue;

        CachedModel *cm = &modelManager.cache[lm->cacheIndex];
        Model *model = &cm->model;
        Vector3 pos = {0, 0, 0};
        Vector3 escala = {1.0f, 1.0f, 1.0f};
        bool ativo = true;
        ObjetoCena *obj = NULL;

        if (lm->idObjeto > 0)
        {
            int idx = BuscarIndicePorId(lm->idObjeto);
            if (idx != -1)
            {
                pos = objetos[idx].posicao;
                escala = objetos[idx].escala;
                ativo = objetos[idx].ativo;
                obj = &objetos[idx];
            }
        }

        if (ativo)
        {
            Color wireColor = (obj && obj->selecionado) ? (Color){255, 219, 84, 255} : (Color){236, 221, 188, 255};

            rlPushMatrix();
            rlTranslatef(pos.x, pos.y, pos.z);
            if (obj)
            {
                rlRotatef(obj->rotacao.x * RAD2DEG, 1, 0, 0);
                rlRotatef(obj->rotacao.y * RAD2DEG, 0, 1, 0);
                rlRotatef(obj->rotacao.z * RAD2DEG, 0, 0, 1);
                rlScalef(escala.x, escala.y, escala.z);
            }
            if (wireframeMode)
            {
                DrawModelViewportWireframe(model, wireColor);
            }
            else if (obj && obj->protoEnabled)
            {
                DrawModelPrototype(model, obj->id, obj->protoBaseColor, obj->protoSecondaryColor);
            }
            else
            {
                DrawModel(*model, (Vector3){0, 0, 0}, 1.0f, (Color){112, 112, 112, 255});
            }
            rlPopMatrix();
        }
    }

    if (wireframeMode)
        rlEnableBackfaceCulling();
}

void DrawSelectedObjectOrigins(Camera camera)
{
    const int primarySelectedId = ObterObjetoSelecionadoId();
    const Color activeColor = (Color){255, 219, 84, 255};
    const Color selectedColor = (Color){242, 132, 34, 255};
    const Color borderColor = (Color){30, 30, 30, 230};

    Vector3 forward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));

    for (int i = 0; i < totalObjetos; i++)
    {
        ObjetoCena *obj = &objetos[i];
        if (!obj->ativo || !obj->selecionado)
            continue;

        Vector3 toObj = Vector3Subtract(obj->posicao, camera.position);
        if (Vector3DotProduct(toObj, forward) <= 0.0f)
            continue;

        Vector2 screen = GetWorldToScreen(obj->posicao, camera);
        if (screen.x < 0.0f || screen.x > (float)GetScreenWidth() ||
            screen.y < 0.0f || screen.y > (float)GetScreenHeight())
            continue;

        bool active = (obj->id == primarySelectedId);
        float radius = active ? 6.5f : 5.0f;
        Color fill = active ? activeColor : selectedColor;

        DrawCircleV(screen, radius + 2.0f, borderColor);
        DrawCircleV(screen, radius, fill);
        DrawCircleLines((int)screen.x, (int)screen.y, radius + 2.0f, Fade(BLACK, 0.65f));
    }
}


void RenderPrototypePreview(const ObjetoCena *obj, RenderTexture2D *target)
{
    if (!obj || !target || target->id == 0)
        return;

    Camera3D cam = {0};
    cam.position = (Vector3){2.2f, 2.2f, 2.2f};
    cam.target = (Vector3){0.0f, 0.0f, 0.0f};
    cam.up = (Vector3){0.0f, 1.0f, 0.0f};
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    BeginTextureMode(*target);
    ClearBackground((Color){12, 12, 12, 255});
    BeginMode3D(cam);

    DrawCube((Vector3){0.0f, 0.0f, 0.0f}, 1.6f, 1.6f, 1.6f, (Color){112, 112, 112, 255});

    DrawCubeWires((Vector3){0.0f, 0.0f, 0.0f}, 1.6f, 1.6f, 1.6f, (Color){30, 30, 30, 255});

    EndMode3D();
    EndTextureMode();
}

void UnloadAllModels(void)
{
    modelManager.modelCount = 0;
    modelManager.selectedModel = -1;

    for (int i = 0; i < modelManager.cacheCount; i++)
    {
        if (modelManager.cache[i].loaded)
        {
            UnloadModel(modelManager.cache[i].model);
            modelManager.cache[i].loaded = false;
        }
        modelManager.cache[i].refCount = 0;
        modelManager.cache[i].filepath[0] = '\0';
    }
    modelManager.cacheCount = 0;
    UnloadPrototypeTextures();
}

void ClearActiveModels(void)
{
    for (int i = 0; i < modelManager.modelCount; i++)
    {
        if (modelManager.models[i].loaded)
            LiberarModeloCache(modelManager.models[i].cacheIndex);
    }
    modelManager.modelCount = 0;
    modelManager.selectedModel = -1;
    UnloadPrototypeTextures();
}

void RestaurarModeloPorFilepath(const char *filepath, const char *nome)
{
    if (!filepath || strlen(filepath) == 0)
        return;
    if (!IsValidModelFile(filepath))
        return;

    // Verificar se o modelo já está carregado
    for (int i = 0; i < modelManager.modelCount; i++)
    {
        if (strcmp(modelManager.models[i].nomeObjeto, nome) == 0)
        {
            return; // Modelo já existe
        }
    }

    int cacheIndex = AdquirirModeloCache(filepath);
    if (cacheIndex == -1)
        return;

    int idObjeto = -1;
    for (int i = 0; i < totalObjetos; i++)
    {
        if (strcmp(objetos[i].nome, nome) == 0 &&
            strcmp(objetos[i].caminhoModelo, filepath) == 0)
        {
            idObjeto = objetos[i].id;
            break;
        }
    }

    if (modelManager.modelCount < MAX_MODELS)
    {
        LoadedModel *lm = &modelManager.models[modelManager.modelCount];
        lm->cacheIndex = cacheIndex;
        strncpy(lm->nomeObjeto, nome, 31);
        lm->nomeObjeto[31] = '\0';
        lm->loaded = true;
        lm->idObjeto = idObjeto;

        modelManager.modelCount++;
        SetSelectedModelByObjetoId(ObterObjetoSelecionadoId());
    }
}

void SetSelectedModelByObjetoId(int idObjeto)
{
    if (idObjeto <= 0)
    {
        modelManager.selectedModel = -1;
        return;
    }

    int idx = EncontrarModeloPorIdObjeto(idObjeto);
    modelManager.selectedModel = idx;
}

void CarregarModeloParaObjeto(const char *filepath, const char *nome, int idObjeto)
{
    if (!filepath || modelManager.modelCount >= MAX_MODELS)
        return;
    if (!IsValidModelFile(filepath))
        return;

    int cacheIndex = AdquirirModeloCache(filepath);
    if (cacheIndex == -1)
        return;

    LoadedModel *lm = &modelManager.models[modelManager.modelCount];
    lm->cacheIndex = cacheIndex;
    lm->loaded = true;

    if (nome && nome[0] != '\0')
    {
        strncpy(lm->nomeObjeto, nome, 31);
        lm->nomeObjeto[31] = '\0';
    }
    else
    {
        const char *nomeArquivo = strrchr(filepath, '/');
        if (!nomeArquivo)
            nomeArquivo = strrchr(filepath, '\\');
        if (nomeArquivo)
            nomeArquivo++;
        else
            nomeArquivo = filepath;

        char nomeSemExtensao[32] = {0};
        strncpy(nomeSemExtensao, nomeArquivo, 31);
        nomeSemExtensao[31] = '\0';

        char *ponto = strrchr(nomeSemExtensao, '.');
        if (ponto)
            *ponto = '\0';

        strncpy(lm->nomeObjeto, nomeSemExtensao, 31);
        lm->nomeObjeto[31] = '\0';
    }

    lm->idObjeto = idObjeto;
    modelManager.modelCount++;
}

int AddPrimitiveObject(PrimitiveModelType type)
{
    if (type < 0 || type >= PRIMITIVE_MODEL_COUNT)
        return -1;
    if (modelManager.modelCount >= MAX_MODELS)
        return -1;

    char objectName[32] = {0};
    BuildUniqueObjectName(kPrimitiveModelNames[type], objectName, sizeof(objectName));

    int idObjeto = RegistrarObjeto(objectName, (Vector3){0, 0, 0}, -1);
    if (idObjeto <= 0)
        return -1;

    int idxObjeto = BuscarIndicePorId(idObjeto);
    if (idxObjeto != -1)
    {
        strncpy(objetos[idxObjeto].caminhoModelo, kPrimitiveModelPaths[type], sizeof(objetos[idxObjeto].caminhoModelo) - 1);
        objetos[idxObjeto].caminhoModelo[sizeof(objetos[idxObjeto].caminhoModelo) - 1] = '\0';
    }

    CarregarModeloParaObjeto(kPrimitiveModelPaths[type], objectName, idObjeto);
    SelecionarObjetoPorId(idObjeto);
    SetSelectedModelByObjetoId(idObjeto);
    return idObjeto;
}

static bool RaycastModelsInternal(Ray ray, Vector3 *hitPos, float *hitDist, int *hitObjectId)
{
    bool hitAny = false;
    float bestDist = 0.0f;
    Vector3 bestPos = {0};
    int bestObjectId = -1;

    for (int i = 0; i < modelManager.modelCount; i++)
    {
        LoadedModel *lm = &modelManager.models[i];
        if (!lm->loaded)
            continue;

        if (lm->cacheIndex < 0 || lm->cacheIndex >= modelManager.cacheCount)
            continue;
        if (!modelManager.cache[lm->cacheIndex].loaded)
            continue;

        Model model = modelManager.cache[lm->cacheIndex].model;
        Vector3 pos = {0, 0, 0};
        Vector3 rot = {0, 0, 0};
        Vector3 escala = {1.0f, 1.0f, 1.0f};
        bool ativo = true;

        if (lm->idObjeto > 0)
        {
            int idx = BuscarIndicePorId(lm->idObjeto);
            if (idx == -1)
                continue;
            pos = objetos[idx].posicao;
            rot = objetos[idx].rotacao;
            escala = objetos[idx].escala;
            ativo = objetos[idx].ativo;
        }

        if (!ativo)
            continue;

        Matrix world = BuildObjectWorldMatrix(pos, rot, escala);
        Matrix transform = MatrixMultiply(world, model.transform);
        BoundingBox localBounds = GetModelBoundingBox(model);
        BoundingBox worldBounds = TransformBoundingBox(localBounds, transform);
        RayCollision boxHit = GetRayCollisionBox(ray, worldBounds);
        if (!boxHit.hit)
            continue;

        for (int m = 0; m < model.meshCount; m++)
        {
            RayCollision rc = GetRayCollisionMesh(ray, model.meshes[m], transform);
            if (rc.hit)
            {
                if (!hitAny || rc.distance < bestDist)
                {
                    hitAny = true;
                    bestDist = rc.distance;
                    bestPos = rc.point;
                    bestObjectId = lm->idObjeto;
                }
            }
        }
    }

    if (hitAny)
    {
        if (hitPos)
            *hitPos = bestPos;
        if (hitDist)
            *hitDist = bestDist;
        if (hitObjectId)
            *hitObjectId = bestObjectId;
    }
    else if (hitObjectId)
    {
        *hitObjectId = -1;
    }

    return hitAny;
}

bool RaycastModels(Ray ray, Vector3 *hitPos, float *hitDist)
{
    return RaycastModelsInternal(ray, hitPos, hitDist, nullptr);
}

bool RaycastModelsEx(Ray ray, Vector3 *hitPos, float *hitDist, int *hitObjectId)
{
    return RaycastModelsInternal(ray, hitPos, hitDist, hitObjectId);
}
