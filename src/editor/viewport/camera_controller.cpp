#include "camera_controller.h"
#include <math.h>

static bool mouseUIEnabled = false;

// Estrutura para armazenar ângulos de Euler (em radianos)
typedef struct
{
    float yaw;   // Rotação horizontal (eixo Y)
    float pitch; // Rotação vertical (eixo X)
} CameraRotation;

static CameraRotation cameraRot = {0, 0};
static float cameraDistance = 10.0f;
static Camera mainCamera = {0};

static void SyncOrbitStateFromCamera(const Camera *cam)
{
    if (!cam)
        return;

    Vector3 d = (Vector3){
        cam->position.x - cam->target.x,
        cam->position.y - cam->target.y,
        cam->position.z - cam->target.z};
    float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dist < 0.0001f)
        dist = 0.0001f;

    cameraDistance = dist;
    cameraRot.yaw = atan2f(d.x, d.z);

    float s = d.y / dist;
    if (s > 1.0f)
        s = 1.0f;
    if (s < -1.0f)
        s = -1.0f;
    cameraRot.pitch = asinf(s);
}

void SyncCameraControllerToCamera(const Camera *cam)
{
    SyncOrbitStateFromCamera(cam);
}

// Funções auxiliares para vetores
static Vector3 Vec3Sub(Vector3 a, Vector3 b)
{
    return (Vector3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static Vector3 Vec3Add(Vector3 a, Vector3 b)
{
    return (Vector3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vector3 Vec3Scale(Vector3 v, float scale)
{
    return (Vector3){v.x * scale, v.y * scale, v.z * scale};
}

static Vector3 Vec3Normalize(Vector3 v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 0.0001f)
        return (Vector3){0, 0, 0};
    return (Vector3){v.x / len, v.y / len, v.z / len};
}

static Vector3 Vec3Cross(Vector3 a, Vector3 b)
{
    return (Vector3){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x};
}

static float Vec3Length(Vector3 v)
{
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Converte ângulos de Euler para posição de câmera em órbita
static Vector3 EulerToOrbitalPosition(Vector3 target, float yaw, float pitch, float distance)
{
    Vector3 pos;
    pos.x = target.x + sinf(yaw) * cosf(pitch) * distance;
    pos.y = target.y + sinf(pitch) * distance;
    pos.z = target.z + cosf(yaw) * cosf(pitch) * distance;
    return pos;
}

void EnableMouseForUI(void)
{
    mouseUIEnabled = true;
}

void DisableMouseForUI(void)
{
    mouseUIEnabled = false;
}

bool IsMouseEnabledForUI(void)
{
    return mouseUIEnabled;
}

void UpdateEditorOrbitCamera(Camera *cam)
{
    if (mouseUIEnabled)
        return;

    Vector2 mouseDelta = GetMouseDelta();
    float scrollDelta = GetMouseWheelMove();

    // Calcula a distância atual da câmera até o alvo
    Vector3 direction = Vec3Sub(cam->position, cam->target);
    cameraDistance = Vec3Length(direction);

    if (cameraDistance < 0.1f)
        cameraDistance = 0.1f;

    // Zoom com scroll do mouse
    if (scrollDelta != 0.0f)
    {
        cameraDistance -= scrollDelta * 1.0f;
        if (cameraDistance < 0.5f)
            cameraDistance = 0.5f;
        if (cameraDistance > 100.0f)
            cameraDistance = 100.0f;
    }

    // Rotação com botão do meio do mouse (sem Shift)
    if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON) &&
        !IsKeyDown(KEY_LEFT_SHIFT) && !IsKeyDown(KEY_RIGHT_SHIFT))
    {
        // Sensibilidade da rotação
        float rotationSensitivity = 0.01f;

        // Rotação horizontal (yaw) - movimento do mouse em X
        cameraRot.yaw -= mouseDelta.x * rotationSensitivity;

        // Rotação vertical (pitch) - movimento do mouse em Y
        cameraRot.pitch += mouseDelta.y * rotationSensitivity;

        // Limita o pitch para evitar inversão (gimbal lock)
        float pitchLimit = PI / 2.0f - 0.1f;
        if (cameraRot.pitch > pitchLimit)
            cameraRot.pitch = pitchLimit;
        if (cameraRot.pitch < -pitchLimit)
            cameraRot.pitch = -pitchLimit;

        // Mantém o yaw normalizado entre -PI e PI
        while (cameraRot.yaw > PI)
            cameraRot.yaw -= 2.0f * PI;
        while (cameraRot.yaw < -PI)
            cameraRot.yaw += 2.0f * PI;
    }

    // Pan com Shift + botão do meio do mouse
    if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON) &&
        (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)))
    {
        if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)
        {
            // Calcula os vetores right e up baseado na câmera atual
            Vector3 forward = Vec3Normalize(Vec3Sub(cam->target, cam->position));
            Vector3 right = Vec3Normalize(Vec3Cross(forward, cam->up));
            Vector3 up = Vec3Normalize(Vec3Cross(right, forward));

            float panSpeed = 0.05f;
            Vector3 panOffset = Vec3Add(
                Vec3Scale(right, -mouseDelta.x * panSpeed),
                Vec3Scale(up, mouseDelta.y * panSpeed));

            cam->position = Vec3Add(cam->position, panOffset);
            cam->target = Vec3Add(cam->target, panOffset);
        }
    }

    // Atualiza a posição da câmera baseado nos ângulos Euler
    cam->position = EulerToOrbitalPosition(cam->target, cameraRot.yaw, cameraRot.pitch, cameraDistance);

    // Garante que o vetor up está correto
    cam->up = (Vector3){0, 1, 0};
}

Camera InitCamera()
{
    mainCamera.position = (Vector3){6, 6, 6};
    mainCamera.target = (Vector3){0, 0, 0};
    mainCamera.up = (Vector3){0, 1, 0};
    mainCamera.fovy = 45.0f;
    mainCamera.projection = CAMERA_PERSPECTIVE;
    SyncOrbitStateFromCamera(&mainCamera);
    
    return mainCamera;
}
