#include "application.h"
#include "editor/viewport/gizmo.h"
#include "editor/viewport/camera_controller.h"
#include "scene/outliner.h"
#include "assets/model_manager.h"
#include "editor/ui/file_explorer.h"
#include "editor/ui/help_panel.h"
#include "editor/ui/splash_screen.h"
#include "editor/ui/info_panel.h"
#include "editor/ui/top_bar.h"
#include "editor/ui/properties_panel.h"
#include "editor/ui/ui_style.h"
#include "scene/scene_manager.h"
#include "window_icon_win32.h"
#include "raymath.h"
#include <float.h>
#include <cmath>
#include <stdio.h>
#include <cstring>

static Camera appCamera = {0};
static Vector3 appRayEndPos = {0};
static bool appRayHit = false;
static float profLogicMs = 0.0f;
static float profPhysicsMs = 0.0f;
static float profRenderMs = 0.0f;

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

static bool ResolveWindowIconPath(char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return false;
    out[0] = '\0';

    const char *relativePath = "icons/N.ico";
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
    if (!ResolveWindowIconPath(iconPath, sizeof(iconPath)))
        return;
    ApplyWin32WindowIcon(GetWindowHandle(), iconPath);
#endif
}

static void SaveProjectIconFromCurrentFrame(const char *iconPath)
{
    if (!iconPath || iconPath[0] == '\0')
        return;

    Image frame = LoadImageFromScreen();
    if (!frame.data || frame.width <= 0 || frame.height <= 0)
        return;

    int size = (frame.width < frame.height) ? frame.width : frame.height;
    if (size <= 0)
    {
        UnloadImage(frame);
        return;
    }

    Rectangle crop = {
        (float)((frame.width - size) / 2),
        (float)((frame.height - size) / 2),
        (float)size,
        (float)size};
    Image icon = ImageFromImage(frame, crop);
    ImageResize(&icon, 128, 128);
    ExportImage(icon, iconPath);
    UnloadImage(icon);
    UnloadImage(frame);
}

static bool IsMouseOverUIRoot(void)
{
    Vector2 mouse = GetMousePosition();
    int screenW = GetScreenWidth();

    if (HelpPanelShouldShow() || SplashScreenShouldShow() || SplashScreenIsInputBlocked())
        return true;
    if (fileExplorer.aberto || IsFileMenuOpen() || IsTopBarMenuOpen())
        return true;
    if (PropertiesIsBlockingViewport())
        return true;
    if (mouse.x < (float)PAINEL_LARGURA)
        return true;
    if (mouse.x > (float)(screenW - PROPERTIES_PAINEL_LARGURA))
        return true;
    if (mouse.y < 24.0f)
        return true;
    if (IsMouseOverInfoPanel(mouse))
        return true;
    return false;
}


typedef struct
{
    int id;
    Vector3 center;
    Vector3 axis[3];
    Vector3 half;
} OBB;

typedef struct
{
    int id;
    OBB obb;
} StaticCollider;

static Vector3 physAngVel[MAX_OBJETOS] = {0};

static float Minf(float a, float b)
{
    return (a < b) ? a : b;
}

static float Maxf(float a, float b)
{
    return (a > b) ? a : b;
}

static Matrix RotationFromEuler(Vector3 e)
{
    return MatrixRotateXYZ((Vector3){e.x, e.y, e.z});
}

static Vector3 MatGetColumn(const Matrix *m, int col)
{
    switch (col)
    {
    case 0:
        return (Vector3){m->m0, m->m1, m->m2};
    case 1:
        return (Vector3){m->m4, m->m5, m->m6};
    default:
        return (Vector3){m->m8, m->m9, m->m10};
    }
}

static bool GetModelWorldBoundsByIndex(int index, BoundingBox *out)
{
    if (index < 0 || index >= totalObjetos)
        return false;

    ObjetoCena *obj = &objetos[index];
    if (!obj->ativo)
        return false;

    for (int i = 0; i < modelManager.modelCount; i++)
    {
        LoadedModel *lm = &modelManager.models[i];
        if (!lm->loaded || lm->idObjeto != obj->id)
            continue;

        if (lm->cacheIndex < 0 || lm->cacheIndex >= modelManager.cacheCount)
            continue;
        if (!modelManager.cache[lm->cacheIndex].loaded)
            continue;

        Model model = modelManager.cache[lm->cacheIndex].model;
        BoundingBox bb = GetModelBoundingBox(model);
        bb.min.x += obj->posicao.x;
        bb.min.y += obj->posicao.y;
        bb.min.z += obj->posicao.z;
        bb.max.x += obj->posicao.x;
        bb.max.y += obj->posicao.y;
        bb.max.z += obj->posicao.z;
        if (out)
            *out = bb;
        return true;
    }

    return false;
}

static bool GetObjectModelTransformByIndex(int index, Model **outModel, Matrix *outTransform)
{
    if (index < 0 || index >= totalObjetos)
        return false;
    ObjetoCena *obj = &objetos[index];
    if (!obj->ativo)
        return false;

    for (int i = 0; i < modelManager.modelCount; i++)
    {
        LoadedModel *lm = &modelManager.models[i];
        if (!lm->loaded || lm->idObjeto != obj->id)
            continue;

        if (lm->cacheIndex < 0 || lm->cacheIndex >= modelManager.cacheCount)
            continue;
        if (!modelManager.cache[lm->cacheIndex].loaded)
            continue;

        Model *model = &modelManager.cache[lm->cacheIndex].model;
        if (outModel)
            *outModel = model;
        if (outTransform)
            *outTransform = MatrixMultiply(MatrixTranslate(obj->posicao.x, obj->posicao.y, obj->posicao.z), model->transform);
        return true;
    }

    return false;
}

static bool GetObjectCollisionOBBByIndex(int index, OBB *out)
{
    if (index < 0 || index >= totalObjetos || !out)
        return false;

    ObjetoCena *obj = &objetos[index];
    if (!obj->ativo)
        return false;

    Vector3 size = PropertiesGetCollisionSize(obj->id);
    BoundingBox bb;
    if (!GetModelWorldBoundsByIndex(index, &bb))
        return false;

    Vector3 center = {(bb.min.x + bb.max.x) * 0.5f, (bb.min.y + bb.max.y) * 0.5f, (bb.min.z + bb.max.z) * 0.5f};
    Vector3 half = {(bb.max.x - bb.min.x) * 0.5f, (bb.max.y - bb.min.y) * 0.5f, (bb.max.z - bb.min.z) * 0.5f};

    half.x *= (size.x > 0.0f) ? size.x : 1.0f;
    half.y *= (size.y > 0.0f) ? size.y : 1.0f;
    half.z *= (size.z > 0.0f) ? size.z : 1.0f;

    out->id = obj->id;
    out->center = center;
    // Para física estável, mantemos colisores alinhados ao mundo (sem rotação)
    out->axis[0] = (Vector3){1.0f, 0.0f, 0.0f};
    out->axis[1] = (Vector3){0.0f, 1.0f, 0.0f};
    out->axis[2] = (Vector3){0.0f, 0.0f, 1.0f};
    out->half = (Vector3){
        Maxf(0.0005f, half.x),
        Maxf(0.0005f, half.y),
        Maxf(0.0005f, half.z)};
    return true;
}

static BoundingBox OBBToAABB(const OBB *obb)
{
    Vector3 a = Vector3Scale(obb->axis[0], obb->half.x);
    Vector3 b = Vector3Scale(obb->axis[1], obb->half.y);
    Vector3 c = Vector3Scale(obb->axis[2], obb->half.z);
    Vector3 corners[8];
    int idx = 0;
    for (int x = -1; x <= 1; x += 2)
        for (int y = -1; y <= 1; y += 2)
            for (int z = -1; z <= 1; z += 2)
            {
                Vector3 p = obb->center;
                p = Vector3Add(p, Vector3Scale(a, x));
                p = Vector3Add(p, Vector3Scale(b, y));
                p = Vector3Add(p, Vector3Scale(c, z));
                corners[idx++] = p;
            }
    BoundingBox bb = {corners[0], corners[0]};
    for (int i = 1; i < 8; i++)
    {
        bb.min.x = Minf(bb.min.x, corners[i].x);
        bb.min.y = Minf(bb.min.y, corners[i].y);
        bb.min.z = Minf(bb.min.z, corners[i].z);
        bb.max.x = Maxf(bb.max.x, corners[i].x);
        bb.max.y = Maxf(bb.max.y, corners[i].y);
        bb.max.z = Maxf(bb.max.z, corners[i].z);
    }
    return bb;
}

static Vector3 ProjectVectorOnPlane(Vector3 v, Vector3 n)
{
    float dn = Vector3DotProduct(v, n);
    return Vector3Subtract(v, Vector3Scale(n, dn));
}

static void BuildOBB(int index, Vector3 size, OBB *out)
{
    if (!out || index < 0 || index >= totalObjetos)
        return;
    ObjetoCena *obj = &objetos[index];
    Matrix rot = RotationFromEuler(obj->rotacao);
    out->center = obj->posicao;
    out->axis[0] = Vector3Normalize(MatGetColumn(&rot, 0));
    out->axis[1] = Vector3Normalize(MatGetColumn(&rot, 1));
    out->axis[2] = Vector3Normalize(MatGetColumn(&rot, 2));
    out->half = (Vector3){Maxf(0.0005f, size.x * 0.5f), Maxf(0.0005f, size.y * 0.5f), Maxf(0.0005f, size.z * 0.5f)};
}

static float ProjectHalfExtent(const OBB *b, Vector3 axis)
{
    return fabsf(Vector3DotProduct(b->axis[0], axis)) * b->half.x +
           fabsf(Vector3DotProduct(b->axis[1], axis)) * b->half.y +
           fabsf(Vector3DotProduct(b->axis[2], axis)) * b->half.z;
}

static bool OBBIntersect(const OBB *a, const OBB *b, Vector3 *axisOut, float *depthOut)
{
    // SAT 15 axes
    Vector3 axes[15];
    int count = 0;
    axes[count++] = a->axis[0];
    axes[count++] = a->axis[1];
    axes[count++] = a->axis[2];
    axes[count++] = b->axis[0];
    axes[count++] = b->axis[1];
    axes[count++] = b->axis[2];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
        {
            Vector3 c = Vector3CrossProduct(a->axis[i], b->axis[j]);
            float len = Vector3Length(c);
            if (len > 1e-5f)
                axes[count++] = Vector3Scale(c, 1.0f / len);
        }

    Vector3 t = Vector3Subtract(b->center, a->center);
    float minDepth = FLT_MAX;
    Vector3 minAxis = {0};

    for (int i = 0; i < count; i++)
    {
        Vector3 axis = axes[i];
        float ra = ProjectHalfExtent(a, axis);
        float rb = ProjectHalfExtent(b, axis);
        float dist = fabsf(Vector3DotProduct(t, axis));
        float overlap = ra + rb - dist;
        if (overlap < 0.0f)
            return false;
        if (overlap < minDepth)
        {
            minDepth = overlap;
            minAxis = axis;
        }
    }

    if (axisOut)
    {
        if (Vector3DotProduct(t, minAxis) < 0.0f)
            minAxis = Vector3Negate(minAxis);
        *axisOut = minAxis;
    }
    if (depthOut)
        *depthOut = minDepth;
    return true;
}

static void DrawOBB(const OBB *obb, Color c)
{
    if (!obb)
        return;
    Vector3 a = obb->axis[0];
    Vector3 b = obb->axis[1];
    Vector3 d = obb->axis[2];
    Vector3 h = obb->half;
    Vector3 corners[8];
    int idx = 0;
    for (int x = -1; x <= 1; x += 2)
        for (int y = -1; y <= 1; y += 2)
            for (int z = -1; z <= 1; z += 2)
            {
                Vector3 p = obb->center;
                p = Vector3Add(p, Vector3Scale(a, h.x * x));
                p = Vector3Add(p, Vector3Scale(b, h.y * y));
                p = Vector3Add(p, Vector3Scale(d, h.z * z));
                corners[idx++] = p;
            }
    int edges[12][2] = {{0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3}, {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}};
    for (int i = 0; i < 12; i++)
        DrawLine3D(corners[edges[i][0]], corners[edges[i][1]], c);
}

static void DrawCollisionShapeForObject(int index, Color color)
{
    if (index < 0 || index >= totalObjetos)
        return;
    ObjetoCena *obj = &objetos[index];
    if (!obj->ativo)
        return;
    if (!PropertiesHasCollider(obj->id))
        return;

    int shape = PropertiesGetCollisionShape(obj->id);
    shape = COLLISION_SHAPE_MESH_BOUNDS;

    Vector3 pos = obj->posicao;
    Vector3 size = PropertiesGetCollisionSize(obj->id);
    float sx = (size.x > 0.01f) ? size.x : 0.01f;
    float sy = (size.y > 0.01f) ? size.y : 0.01f;
    float sz = (size.z > 0.01f) ? size.z : 0.01f;

    if (PropertiesIsTerrain(obj->id))
    {
        Model *terrainModel = nullptr;
        Matrix transform = {0};
        if (GetObjectModelTransformByIndex(index, &terrainModel, &transform) && terrainModel)
        {
            DrawModelWires(*terrainModel, pos, 1.0f, color);
            return;
        }
    }

    OBB obb;
    if (GetObjectCollisionOBBByIndex(index, &obb))
        DrawOBB(&obb, color);
}

static void DrawCollisionDebug(void)
{
    int selId = ObterObjetoSelecionadoId();
    for (int i = 0; i < totalObjetos; i++)
    {
        if (!objetos[i].ativo)
            continue;
        if (!PropertiesHasCollider(objetos[i].id))
            continue;
        bool isSelected = (selId > 0 && objetos[i].id == selId);
        Color col = isSelected ? (Color){184, 38, 36, 255}
                               : (PropertiesIsStatic(objetos[i].id) ? (Color){108, 108, 108, 255}
                                                                    : (Color){130, 46, 42, 255});
        DrawCollisionShapeForObject(i, col);
    }
}

void InitializeApplication()
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_MAXIMIZED);
    InitWindow(1280, 720, "Nanquimori Engine");
    ApplyRuntimeWindowIcon();
    MaximizeWindow();
    SetExitKey(0);
    SetTargetFPS(0);

    appCamera = InitCamera();
    EnableCursor();

    InitTopBar();
    InitOutliner();
    InitInfoPanel();
    InitHelpPanel();
    InitSplashScreen();
    InitPropertiesPanel();

    InitModelManager();
    InitSceneManager();
    InitFileExplorer();
}

void ShutdownApplication()
{
    UnloadTopBar();
    UnloadSplashScreen();

    UnloadAllModels();
    UnloadFileExplorer();

    ReleaseWin32WindowIcon();

    CloseWindow();
}

void UpdateApplication()
{
    double updateStart = GetTime();
    double physicsStart = 0.0;
    static bool physInit = false;
    static int physLastTotal = -1;
    static Vector3 physVel[MAX_OBJETOS] = {0};
    static bool playSessionPrev = false;

    Vector3 loadedCamPos = {0};
    Vector3 loadedCamTarget = {0};
    if (ConsumeLoadedProjectCameraState(&loadedCamPos, &loadedCamTarget))
    {
        appCamera.position = loadedCamPos;
        appCamera.target = loadedCamTarget;
        appCamera.up = (Vector3){0.0f, 1.0f, 0.0f};
        SyncCameraControllerToCamera(&appCamera);
    }

    bool handledTransformShortcut = PropertiesHandleTransformShortcuts();
    if (!handledTransformShortcut && IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_Z))
        Undo();

    if (IsMouseOverUIRoot())
        EnableMouseForUI();
    else
        DisableMouseForUI();

    UpdateCameraBlender(&appCamera);
    SetProjectCameraState(appCamera.position, appCamera.target);

    bool ctrlPressed = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrlPressed && IsKeyPressed(KEY_S))
    {
        if (!SaveProject())
            OpenProjectSaveAs();
    }

    UpdateTopBar();
    bool playSession = IsPlayModeActive();
    bool playPaused = IsPlayPaused();
    bool playMode = playSession && !playPaused;
    bool stopRequested = ConsumePlayStopRequested();
    bool restartRequested = ConsumePlayRestartRequested();

    if (stopRequested)
    {
        SetPlayModeActive(false);
        ReloadActiveScene();
        playMode = false;
        playSession = false;
    }
    else if (restartRequested)
    {
        SetPlayModeActive(true);
        SetPlayPaused(false);
        ReloadActiveScene();
        playMode = true;
    }

    if (!playSessionPrev && playSession)
    {
        SaveActiveSceneSnapshot();
    }
    playSessionPrev = playSession;

    if (!playSession)
    {
        ProcessarOutliner();

        UpdateFileExplorer();

        char arquivoSelecionado[256] = {0};
        if (FileExplorerArquivoSelecionado(arquivoSelecionado))
        {
            LoadModelFromFile(arquivoSelecionado);
        }
    }

    UpdateMoveGizmo(appCamera);

    if (!physInit || physLastTotal != totalObjetos)
    {
        for (int i = 0; i < MAX_OBJETOS; i++)
        {
            physVel[i] = (Vector3){0};
            physAngVel[i] = (Vector3){0};
        }
        physInit = true;
        physLastTotal = totalObjetos;
    }

    if (playMode)
    {
        physicsStart = GetTime();
        float dt = GetFrameTime();
        const float gravity = 9.8f;
        const float restitution = 0.1f;
        const float friction = 0.2f;
        const float damping = 0.02f;
        StaticCollider staticCols[MAX_OBJETOS];
        int staticCount = 0;
        int terrainIds[MAX_OBJETOS];
        int terrainCount = 0;
        for (int i = 0; i < totalObjetos; i++)
        {
            if (!objetos[i].ativo)
                continue;
            if (!PropertiesIsStatic(objetos[i].id))
                continue;
            if (!PropertiesHasCollider(objetos[i].id))
                continue;
            if (PropertiesIsTerrain(objetos[i].id))
            {
                if (terrainCount < MAX_OBJETOS)
                {
                    terrainIds[terrainCount++] = i;
                }
                continue;
            }
            OBB obb;
            if (GetObjectCollisionOBBByIndex(i, &obb) && staticCount < MAX_OBJETOS)
            {
                staticCols[staticCount].id = objetos[i].id;
                staticCols[staticCount].obb = obb;
                staticCount++;
            }
        }

        for (int i = 0; i < totalObjetos; i++)
        {
            if (!objetos[i].ativo)
                continue;

            if (PropertiesIsStatic(objetos[i].id))
            {
                physVel[i] = (Vector3){0};
                physAngVel[i] = (Vector3){0};
                continue;
            }

            if (PropertiesIsRigidbody(objetos[i].id))
            {
                if (PropertiesHasGravity(objetos[i].id))
                {
                    physVel[i].y -= gravity * dt;
                }

                physVel[i].x *= (1.0f - damping);
                physVel[i].z *= (1.0f - damping);
                // Bloqueia rotação: zera velocidade angular.
                physAngVel[i] = (Vector3){0};

                objetos[i].posicao.x += physVel[i].x * dt;
                objetos[i].posicao.y += physVel[i].y * dt;
                objetos[i].posicao.z += physVel[i].z * dt;

                OBB dynObb;
                bool hasObb = GetObjectCollisionOBBByIndex(i, &dynObb);

                if (hasObb && staticCount > 0)
                {
                    for (int s = 0; s < staticCount; s++)
                    {
                        if (staticCols[s].id == objetos[i].id)
                            continue;

                        Vector3 axis;
                        float depth;
                        if (!OBBIntersect(&dynObb, &staticCols[s].obb, &axis, &depth))
                            continue;

                        Vector3 delta = Vector3Scale(axis, depth);
                        objetos[i].posicao = Vector3Add(objetos[i].posicao, delta);
                        dynObb.center = Vector3Add(dynObb.center, delta);

                        // Resolve linear velocity along collision normal
                        float vn = Vector3DotProduct(physVel[i], axis);
                        if (vn < 0.0f)
                        {
                            Vector3 vnVec = Vector3Scale(axis, vn);
                            Vector3 vtVec = Vector3Subtract(physVel[i], vnVec);
                            vnVec = Vector3Scale(vnVec, -restitution);
                            vtVec = Vector3Scale(vtVec, (1.0f - friction));
                            physVel[i] = Vector3Add(vnVec, vtVec);
                        }
                    }
                }

                if (hasObb && terrainCount > 0)
                {
                    BoundingBox dynBox = OBBToAABB(&dynObb);
                    float height = dynBox.max.y - dynBox.min.y;
                    float margin = (height > 1.0f) ? height : 1.0f;
                    Vector3 rayOrigin = {objetos[i].posicao.x, dynBox.max.y + margin, objetos[i].posicao.z};
                    Ray ray = {rayOrigin, (Vector3){0.0f, -1.0f, 0.0f}};
                    bool hitAny = false;
                    RayCollision best = {0};

                    for (int t = 0; t < terrainCount; t++)
                    {
                        int terrainIndex = terrainIds[t];
                        Model *terrainModel = nullptr;
                        Matrix transform = {0};
                        if (!GetObjectModelTransformByIndex(terrainIndex, &terrainModel, &transform))
                            continue;

                        for (int m = 0; m < terrainModel->meshCount; m++)
                        {
                            RayCollision rc = GetRayCollisionMesh(ray, terrainModel->meshes[m], transform);
                            if (rc.hit)
                            {
                                if (!hitAny || rc.distance < best.distance)
                                {
                                    hitAny = true;
                                    best = rc;
                                }
                            }
                        }
                    }

                    if (hitAny)
                    {
                        float bottom = dynBox.min.y;
                        float targetY = best.point.y;
                            float penetration = targetY - bottom;
                            if (penetration > 0.0f)
                            {
                                objetos[i].posicao.y += penetration;
                                dynObb.center.y += penetration;
                                Vector3 n = Vector3Normalize(best.normal);
                                physVel[i] = ProjectVectorOnPlane(physVel[i], n);
                                Vector3 g = (Vector3){0.0f, -gravity, 0.0f};
                                Vector3 gT = ProjectVectorOnPlane(g, n);
                                // Deslizamento em declive: acelera ao longo do plano
                                physVel[i].x += gT.x * dt;
                                physVel[i].y += gT.y * dt;
                                physVel[i].z += gT.z * dt;
                                const float groundFriction = 0.05f; // menor atrito em terreno para permitir escorregar
                                physVel[i].x *= (1.0f - groundFriction);
                                physVel[i].z *= (1.0f - groundFriction);
                            }
                        }
                    }
                }
            else
            {
                physVel[i] = (Vector3){0};
            }
        }
        profPhysicsMs = (float)((GetTime() - physicsStart) * 1000.0);
    }
    else
    {
        profPhysicsMs = 0.0f;
    }

    const float MAX_DIST = 1000.0f;
    Vector3 dir = {
        appCamera.target.x - appCamera.position.x,
        appCamera.target.y - appCamera.position.y,
        appCamera.target.z - appCamera.position.z};
    float dirLen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dirLen < 0.0001f)
        dirLen = 1.0f;
    dir.x /= dirLen;
    dir.y /= dirLen;
    dir.z /= dirLen;

    Ray ray = {appCamera.position, dir};
    Vector3 hitPos = {0};
    float hitDist = MAX_DIST;
    bool hit = RaycastModels(ray, &hitPos, &hitDist);
    if (!hit)
        hitDist = MAX_DIST;

    appRayHit = hit;
    if (hit)
    {
        appRayEndPos = hitPos;
    }
    else
    {
        appRayEndPos = (Vector3){
            appCamera.position.x + dir.x * MAX_DIST,
            appCamera.position.y + dir.y * MAX_DIST,
            appCamera.position.z + dir.z * MAX_DIST};
    }

    UpdateInfoPanel(appCamera.position, appCamera.target, hitDist, hit);
    if (!playMode)
    {
        UpdateFileMenu();
        UpdateSplashScreen();
    }

    profLogicMs = (float)((GetTime() - updateStart) * 1000.0);
}

void RenderApplication()
{
    double renderStart = GetTime();
    BeginDrawing();
    ClearBackground((Color){24, 26, 32, 255});

    BeginMode3D(appCamera);

    RenderModels();
    if (PropertiesShowCollisions())
        DrawCollisionDebug();

    DrawGrid(10, 1.0f);
    DrawMoveGizmo(appCamera);

    if (IsRaycastLineVisible())
    {
        Color rayColor = appRayHit ? GREEN : RED;
        DrawLine3D(appCamera.position, appRayEndPos, rayColor);
        if (IsRaycast3DVisible())
            DrawSphere(appRayEndPos, 0.08f, rayColor);
    }

    EndMode3D();

    char pendingIconPath[512] = {0};
    if (ConsumePendingProjectIconPath(pendingIconPath, (int)sizeof(pendingIconPath)))
        SaveProjectIconFromCurrentFrame(pendingIconPath);

    if (IsRaycast2DVisible())
    {
        Color rayColor = appRayHit ? GREEN : RED;
        Vector2 screenPos = GetWorldToScreen(appRayEndPos, appCamera);
        DrawCircleV(screenPos, 5.0f, rayColor);
        DrawCircleLines((int)screenPos.x, (int)screenPos.y, 6.0f, BLACK);
    }

    DrawGizmo(appCamera, GetScreenWidth());
    DrawInfoPanel();
    DrawOutliner();
    DrawPropertiesPanel();
    DrawTopBar();
    DrawFileMenu();
    DrawFileExplorer();
    DrawHelpPanel();
    DrawSplashScreen();

    EndDrawing();

    profRenderMs = (float)((GetTime() - renderStart) * 1000.0);
    UpdateInfoPanelProfile(profLogicMs, profPhysicsMs, profRenderMs);
}


