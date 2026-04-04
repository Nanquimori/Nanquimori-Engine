#include "scene_camera.h"

#include "assets/model_manager.h"
#include "rlgl.h"
#include "raymath.h"
#include <cmath>
#include <cstring>
#include <cstdio>

static bool managedModeClipOverride = false;
static double managedModeClipRestoreNear = 0.01;
static double managedModeClipRestoreFar = 1000.0;

static float ClampCameraPerspectiveFov(float value)
{
    if (value < 10.0f)
        return 10.0f;
    if (value > 140.0f)
        return 140.0f;
    return value;
}

static float ClampCameraOrthoSize(float value)
{
    if (value < 0.1f)
        return 0.1f;
    if (value > 200.0f)
        return 200.0f;
    return value;
}

static float ClampCameraNearClip(float value)
{
    if (value < 0.01f)
        return 0.01f;
    if (value > 500.0f)
        return 500.0f;
    return value;
}

static float ClampCameraFarClip(float nearClip, float value)
{
    if (value < nearClip + 0.1f)
        return nearClip + 0.1f;
    if (value > 5000.0f)
        return 5000.0f;
    return value;
}

static void BuildUniqueObjectName(const char *baseName, char *outName, size_t outSize)
{
    if (!outName || outSize == 0)
        return;
    outName[0] = '\0';

    const char *base = (baseName && baseName[0] != '\0') ? baseName : "Camera";
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

static Vector3 SafeNormalizeOrFallback(Vector3 value, Vector3 fallback)
{
    float len = Vector3Length(value);
    if (len <= 0.0001f)
        return fallback;
    return Vector3Scale(value, 1.0f / len);
}

static Vector3 RotateAroundAxis(Vector3 value, Vector3 axis, float angle)
{
    axis = SafeNormalizeOrFallback(axis, (Vector3){0.0f, 0.0f, 1.0f});
    float c = cosf(angle);
    float s = sinf(angle);
    Vector3 term1 = Vector3Scale(value, c);
    Vector3 term2 = Vector3Scale(Vector3CrossProduct(axis, value), s);
    Vector3 term3 = Vector3Scale(axis, Vector3DotProduct(axis, value) * (1.0f - c));
    return Vector3Add(Vector3Add(term1, term2), term3);
}

static Vector3 GetObjectCameraForward(const ObjetoCena *obj)
{
    if (!obj)
        return (Vector3){0.0f, 0.0f, -1.0f};

    float pitch = obj->rotacao.x;
    float yaw = obj->rotacao.y;
    Vector3 forward = {
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        -cosf(yaw) * cosf(pitch)};
    return SafeNormalizeOrFallback(forward, (Vector3){0.0f, 0.0f, -1.0f});
}

static void GetObjectCameraBasis(const ObjetoCena *obj, Vector3 *forwardOut, Vector3 *rightOut, Vector3 *upOut)
{
    Vector3 forward = GetObjectCameraForward(obj);
    Vector3 worldUp = {0.0f, 1.0f, 0.0f};
    Vector3 right = Vector3CrossProduct(forward, worldUp);
    if (Vector3Length(right) <= 0.0001f)
        right = (Vector3){1.0f, 0.0f, 0.0f};
    right = SafeNormalizeOrFallback(right, (Vector3){1.0f, 0.0f, 0.0f});

    Vector3 up = SafeNormalizeOrFallback(Vector3CrossProduct(right, forward), worldUp);
    if (obj)
        up = SafeNormalizeOrFallback(RotateAroundAxis(up, forward, obj->rotacao.z), worldUp);
    right = SafeNormalizeOrFallback(Vector3CrossProduct(forward, up), right);
    up = SafeNormalizeOrFallback(Vector3CrossProduct(right, forward), up);

    if (forwardOut)
        *forwardOut = forward;
    if (rightOut)
        *rightOut = right;
    if (upOut)
        *upOut = up;
}

void ConfigureObjetoComoCamera(ObjetoCena *obj)
{
    if (!obj)
        return;

    obj->tipo = OBJETO_TIPO_CAMERA;
    obj->cameraPrincipal = false;
    obj->cameraProjection = CAMERA_PERSPECTIVE;
    obj->cameraPerspectiveFov = 60.0f;
    obj->cameraOrthoSize = 5.0f;
    obj->cameraFocusDistance = 10.0f;
    obj->cameraNearClip = 0.1f;
    obj->cameraFarClip = 1000.0f;
    obj->caminhoModelo[0] = '\0';
    obj->protoEnabled = false;
    obj->physStatic = true;
    obj->physRigidbody = false;
    obj->physCollider = false;
    obj->physGravity = false;
    obj->physTerrain = false;
}

bool ObjetoEhCamera(const ObjetoCena *obj)
{
    return obj && obj->tipo == OBJETO_TIPO_CAMERA;
}

void SetSceneRenderCameraObjectId(int objetoId)
{
    for (int i = 0; i < totalObjetos; i++)
        objetos[i].cameraPrincipal = false;

    int idx = BuscarIndicePorId(objetoId);
    if (idx != -1 && ObjetoEhCamera(&objetos[idx]))
        objetos[idx].cameraPrincipal = true;
}

int GetSceneRenderCameraObjectId(void)
{
    for (int i = 0; i < totalObjetos; i++)
        if (ObjetoEhCamera(&objetos[i]) && objetos[i].cameraPrincipal)
            return objetos[i].id;
    return -1;
}

bool BuildSceneCameraFromObject(const ObjetoCena *obj, Camera *out)
{
    if (!obj || !out || !ObjetoEhCamera(obj))
        return false;

    Vector3 forward = {0};
    Vector3 up = {0};
    GetObjectCameraBasis(obj, &forward, nullptr, &up);

    Camera camera = {0};
    camera.position = obj->posicao;
    float focusDistance = obj->cameraFocusDistance;
    if (focusDistance < 0.1f)
        focusDistance = 0.1f;
    camera.target = Vector3Add(obj->posicao, Vector3Scale(forward, focusDistance));
    camera.up = up;
    camera.projection = (obj->cameraProjection == CAMERA_ORTHOGRAPHIC) ? CAMERA_ORTHOGRAPHIC : CAMERA_PERSPECTIVE;
    camera.fovy = (camera.projection == CAMERA_ORTHOGRAPHIC)
                      ? ClampCameraOrthoSize(obj->cameraOrthoSize) * 2.0f
                      : ClampCameraPerspectiveFov(obj->cameraPerspectiveFov);
    *out = camera;
    return true;
}

bool GetSceneCameraClipPlanes(const ObjetoCena *obj, float *nearClip, float *farClip)
{
    if (!obj || !ObjetoEhCamera(obj))
        return false;

    float nearValue = ClampCameraNearClip(obj->cameraNearClip);
    float farValue = ClampCameraFarClip(nearValue, obj->cameraFarClip);

    if (nearClip)
        *nearClip = nearValue;
    if (farClip)
        *farClip = farValue;
    return true;
}

bool CopySceneObjectFromCameraView(ObjetoCena *obj, const Camera *camera)
{
    if (!obj || !camera || !ObjetoEhCamera(obj))
        return false;

    Vector3 forward = SafeNormalizeOrFallback(Vector3Subtract(camera->target, camera->position), (Vector3){0.0f, 0.0f, -1.0f});
    obj->posicao = camera->position;
    obj->rotacao.x = asinf(Clamp(forward.y, -1.0f, 1.0f));
    obj->rotacao.y = atan2f(forward.x, -forward.z);
    obj->rotacao.z = 0.0f;
    obj->cameraFocusDistance = Vector3Distance(camera->position, camera->target);
    if (obj->cameraFocusDistance < 0.1f)
        obj->cameraFocusDistance = 0.1f;

    if (camera->projection == CAMERA_ORTHOGRAPHIC)
    {
        obj->cameraProjection = CAMERA_ORTHOGRAPHIC;
        obj->cameraOrthoSize = ClampCameraOrthoSize(camera->fovy * 0.5f);
    }
    else
    {
        obj->cameraProjection = CAMERA_PERSPECTIVE;
        obj->cameraPerspectiveFov = ClampCameraPerspectiveFov(camera->fovy);
    }

    return true;
}

void BeginManagedMode3D(Camera camera, const ObjetoCena *sceneCameraObject)
{
    managedModeClipOverride = false;
    if (sceneCameraObject && ObjetoEhCamera(sceneCameraObject))
    {
        float nearClip = 0.1f;
        float farClip = 1000.0f;
        if (GetSceneCameraClipPlanes(sceneCameraObject, &nearClip, &farClip))
        {
            managedModeClipRestoreNear = rlGetCullDistanceNear();
            managedModeClipRestoreFar = rlGetCullDistanceFar();
            rlSetClipPlanes((double)nearClip, (double)farClip);
            managedModeClipOverride = true;
        }
    }

    BeginMode3D(camera);
}

void EndManagedMode3D(void)
{
    EndMode3D();

    if (managedModeClipOverride)
    {
        rlSetClipPlanes(managedModeClipRestoreNear, managedModeClipRestoreFar);
        managedModeClipOverride = false;
    }
}

int AddCameraObjectFromView(const Camera *viewCamera)
{
    (void)viewCamera;
    char objectName[32] = {0};
    BuildUniqueObjectName("Camera", objectName, sizeof(objectName));

    int id = RegistrarObjeto(objectName, (Vector3){0.0f, 0.0f, 0.0f}, -1);
    if (id <= 0)
        return -1;

    int idx = BuscarIndicePorId(id);
    if (idx == -1)
        return id;

    ConfigureObjetoComoCamera(&objetos[idx]);

    if (GetSceneRenderCameraObjectId() <= 0)
        SetSceneRenderCameraObjectId(id);

    SelecionarObjetoPorId(id);
    SetSelectedModelByObjetoId(id);
    return id;
}

bool GetSceneRenderCamera(Camera *out, int *objectId)
{
    for (int i = 0; i < totalObjetos; i++)
    {
        ObjetoCena *obj = &objetos[i];
        if (!obj->ativo || !ObjetoEhCamera(obj))
            continue;

        if (!obj->cameraPrincipal)
            continue;

        if (objectId)
            *objectId = obj->id;
        return BuildSceneCameraFromObject(obj, out);
    }

    if (objectId)
        *objectId = -1;
    return false;
}

static float GetCameraHelperScale(Camera viewCamera, Vector3 position)
{
    float distance = Vector3Distance(viewCamera.position, position);
    float scale = distance * 0.08f;
    if (scale < 0.35f)
        scale = 0.35f;
    if (scale > 2.2f)
        scale = 2.2f;
    return scale;
}

static void DrawCameraHelperBody(Vector3 position, Vector3 forward, Vector3 right, Vector3 up, float scale, Color color)
{
    float bodyHalfWidth = scale * 0.16f;
    float bodyHalfHeight = scale * 0.11f;
    float backOffset = scale * 0.18f;
    float frontOffset = scale * 0.06f;

    Vector3 centerBack = Vector3Subtract(position, Vector3Scale(forward, backOffset));
    Vector3 centerFront = Vector3Add(position, Vector3Scale(forward, frontOffset));

    Vector3 backTopLeft = Vector3Add(Vector3Add(centerBack, Vector3Scale(up, bodyHalfHeight)), Vector3Scale(right, -bodyHalfWidth));
    Vector3 backTopRight = Vector3Add(Vector3Add(centerBack, Vector3Scale(up, bodyHalfHeight)), Vector3Scale(right, bodyHalfWidth));
    Vector3 backBottomLeft = Vector3Add(Vector3Add(centerBack, Vector3Scale(up, -bodyHalfHeight)), Vector3Scale(right, -bodyHalfWidth));
    Vector3 backBottomRight = Vector3Add(Vector3Add(centerBack, Vector3Scale(up, -bodyHalfHeight)), Vector3Scale(right, bodyHalfWidth));
    Vector3 frontTopLeft = Vector3Add(Vector3Add(centerFront, Vector3Scale(up, bodyHalfHeight)), Vector3Scale(right, -bodyHalfWidth));
    Vector3 frontTopRight = Vector3Add(Vector3Add(centerFront, Vector3Scale(up, bodyHalfHeight)), Vector3Scale(right, bodyHalfWidth));
    Vector3 frontBottomLeft = Vector3Add(Vector3Add(centerFront, Vector3Scale(up, -bodyHalfHeight)), Vector3Scale(right, -bodyHalfWidth));
    Vector3 frontBottomRight = Vector3Add(Vector3Add(centerFront, Vector3Scale(up, -bodyHalfHeight)), Vector3Scale(right, bodyHalfWidth));

    DrawLine3D(backTopLeft, backTopRight, color);
    DrawLine3D(backTopRight, backBottomRight, color);
    DrawLine3D(backBottomRight, backBottomLeft, color);
    DrawLine3D(backBottomLeft, backTopLeft, color);

    DrawLine3D(frontTopLeft, frontTopRight, color);
    DrawLine3D(frontTopRight, frontBottomRight, color);
    DrawLine3D(frontBottomRight, frontBottomLeft, color);
    DrawLine3D(frontBottomLeft, frontTopLeft, color);

    DrawLine3D(backTopLeft, frontTopLeft, color);
    DrawLine3D(backTopRight, frontTopRight, color);
    DrawLine3D(backBottomLeft, frontBottomLeft, color);
    DrawLine3D(backBottomRight, frontBottomRight, color);
}

void DrawSceneCameraHelpers(Camera viewCamera)
{
    int primaryCameraId = GetSceneRenderCameraObjectId();

    for (int i = 0; i < totalObjetos; i++)
    {
        ObjetoCena *obj = &objetos[i];
        if (!obj->ativo || !ObjetoEhCamera(obj))
            continue;

        Vector3 forward = {0};
        Vector3 right = {0};
        Vector3 up = {0};
        GetObjectCameraBasis(obj, &forward, &right, &up);

        float scale = GetCameraHelperScale(viewCamera, obj->posicao);
        float aspect = 16.0f / 9.0f;
        float frustumDistance = scale * 0.9f;
        float halfHeight = 0.0f;
        if (obj->cameraProjection == CAMERA_ORTHOGRAPHIC)
            halfHeight = ClampCameraOrthoSize(obj->cameraOrthoSize) * 0.12f;
        else
            halfHeight = tanf(ClampCameraPerspectiveFov(obj->cameraPerspectiveFov) * DEG2RAD * 0.5f) * frustumDistance;
        if (halfHeight < scale * 0.14f)
            halfHeight = scale * 0.14f;
        if (halfHeight > scale * 0.75f)
            halfHeight = scale * 0.75f;
        float halfWidth = halfHeight * aspect;

        Vector3 frustumCenter = Vector3Add(obj->posicao, Vector3Scale(forward, frustumDistance));
        Vector3 top = Vector3Scale(up, halfHeight);
        Vector3 side = Vector3Scale(right, halfWidth);
        Vector3 topLeft = Vector3Add(Vector3Add(frustumCenter, top), Vector3Negate(side));
        Vector3 topRight = Vector3Add(Vector3Add(frustumCenter, top), side);
        Vector3 bottomLeft = Vector3Add(Vector3Subtract(frustumCenter, top), Vector3Negate(side));
        Vector3 bottomRight = Vector3Subtract(Vector3Subtract(frustumCenter, top), Vector3Negate(side));

        bool selected = obj->selecionado;
        bool primary = obj->cameraPrincipal || obj->id == primaryCameraId;
        Color color = primary ? (Color){102, 214, 255, 255} : (Color){214, 164, 92, 255};
        if (selected)
            color = (Color){255, 222, 132, 255};

        DrawCameraHelperBody(obj->posicao, forward, right, up, scale, color);
        DrawLine3D(obj->posicao, topLeft, color);
        DrawLine3D(obj->posicao, topRight, color);
        DrawLine3D(obj->posicao, bottomLeft, color);
        DrawLine3D(obj->posicao, bottomRight, color);
        DrawLine3D(topLeft, topRight, color);
        DrawLine3D(topRight, bottomRight, color);
        DrawLine3D(bottomRight, bottomLeft, color);
        DrawLine3D(bottomLeft, topLeft, color);
        DrawLine3D(obj->posicao, Vector3Add(obj->posicao, Vector3Scale(up, scale * 0.45f)), color);
    }
}

bool RaycastSceneCameraHelpers(Ray ray, Vector3 *hitPos, float *hitDist, int *hitObjectId)
{
    bool hitAny = false;
    float bestDist = 0.0f;
    Vector3 bestPos = {0};
    int bestObjectId = -1;

    for (int i = 0; i < totalObjetos; i++)
    {
        ObjetoCena *obj = &objetos[i];
        if (!obj->ativo || !ObjetoEhCamera(obj))
            continue;

        float helperRadius = Vector3Distance(ray.position, obj->posicao) * 0.05f;
        if (helperRadius < 0.35f)
            helperRadius = 0.35f;
        if (helperRadius > 1.2f)
            helperRadius = 1.2f;
        RayCollision hit = GetRayCollisionSphere(ray, obj->posicao, helperRadius);
        if (!hit.hit)
            continue;

        if (!hitAny || hit.distance < bestDist)
        {
            hitAny = true;
            bestDist = hit.distance;
            bestPos = hit.point;
            bestObjectId = obj->id;
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
