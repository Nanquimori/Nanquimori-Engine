#ifndef MODEL_MANAGER_H
#define MODEL_MANAGER_H

#include "raylib.h"
#include "scene/outliner.h"

#define MAX_MODELS MAX_OBJETOS
#define MAX_MODEL_CACHE 512

typedef struct
{
    int cacheIndex;
    char nomeObjeto[32];
    int idObjeto;
    bool loaded;
} LoadedModel;

typedef struct
{
    Model model;
    char filepath[256];
    int refCount;
    bool loaded;
} CachedModel;

typedef struct
{
    LoadedModel models[MAX_MODELS];
    CachedModel cache[MAX_MODEL_CACHE];
    int modelCount;
    int cacheCount;
    int selectedModel;
} ModelManager;

typedef enum
{
    PRIMITIVE_MODEL_CUBE = 0,
    PRIMITIVE_MODEL_SPHERE,
    PRIMITIVE_MODEL_CYLINDER,
    PRIMITIVE_MODEL_PLANE,
    PRIMITIVE_MODEL_TORUS,
    PRIMITIVE_MODEL_COUNT
} PrimitiveModelType;

extern ModelManager modelManager;

void InitModelManager(void);
void LoadModelFromFile(const char *filepath);
void RemoverModeloPorNome(const char *nome);
void RemoverModeloPorIdObjeto(int idObjeto);
void RestaurarModeloPorFilepath(const char *filepath, const char *nome);
void RenderModels(void);
void DrawSelectedObjectOrigins(Camera camera);
void UnloadAllModels(void);
void ClearActiveModels(void);
bool RaycastModels(Ray ray, Vector3 *hitPos, float *hitDist);
bool RaycastModelsEx(Ray ray, Vector3 *hitPos, float *hitDist, int *hitObjectId);
void SetSelectedModelByObjetoId(int idObjeto);
void CarregarModeloParaObjeto(const char *filepath, const char *nome, int idObjeto);
void RenderPrototypePreview(const ObjetoCena *obj, RenderTexture2D *target);
int AddPrimitiveObject(PrimitiveModelType type);
bool IsPrimitiveModelPath(const char *path);

#endif // MODEL_MANAGER_H
