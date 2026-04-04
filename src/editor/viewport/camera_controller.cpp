#include "camera_controller.h"
#include <math.h>

static bool mouseUIEnabled = false;

typedef struct
{
    float yaw;
    float pitch;
} CameraRotation;

static CameraRotation cameraRot = {0, 0};
static CameraRotation flyRot = {0, 0};
static float cameraDistance = 10.0f;
static float flyMoveSpeed = 6.0f;
static float flyLookSensitivity = 0.0035f;
static bool flyModeWasActive = false;
static bool flyCaptureActive = false;
static Camera mainCamera = {0};

static float ClampFloat(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static bool CtrlHeld(void)
{
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}

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

static Vector3 GetForwardFromRotation(float yaw, float pitch)
{
    return (Vector3){
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)};
}

static Vector3 EulerToOrbitalPosition(Vector3 target, float yaw, float pitch, float distance)
{
    Vector3 pos;
    pos.x = target.x + sinf(yaw) * cosf(pitch) * distance;
    pos.y = target.y + sinf(pitch) * distance;
    pos.z = target.z + cosf(yaw) * cosf(pitch) * distance;
    return pos;
}

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

static void SyncFlyStateFromCamera(const Camera *cam)
{
    if (!cam)
        return;

    Vector3 forward = Vec3Normalize(Vec3Sub(cam->target, cam->position));
    if (Vec3Length(forward) < 0.0001f)
        forward = (Vector3){0.0f, 0.0f, -1.0f};

    flyRot.yaw = atan2f(forward.x, forward.z);

    float s = forward.y;
    if (s > 1.0f)
        s = 1.0f;
    if (s < -1.0f)
        s = -1.0f;
    flyRot.pitch = asinf(s);
}

void SyncCameraControllerToCamera(const Camera *cam)
{
    SyncOrbitStateFromCamera(cam);
    SyncFlyStateFromCamera(cam);
}

static void ReleaseFlyCapture(void)
{
    if (!flyCaptureActive)
        return;

    if (IsCursorHidden())
        EnableCursor();
    flyCaptureActive = false;
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

static void UpdateEditorOrbitCamera(Camera *cam)
{
    if (!cam || mouseUIEnabled)
        return;

    Vector2 mouseDelta = GetMouseDelta();
    float scrollDelta = GetMouseWheelMove();

    Vector3 direction = Vec3Sub(cam->position, cam->target);
    cameraDistance = Vec3Length(direction);
    if (cameraDistance < 0.1f)
        cameraDistance = 0.1f;

    if (scrollDelta != 0.0f)
    {
        cameraDistance -= scrollDelta * 1.0f;
        if (cameraDistance < 0.5f)
            cameraDistance = 0.5f;
        if (cameraDistance > 100.0f)
            cameraDistance = 100.0f;
    }

    if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON) &&
        !IsKeyDown(KEY_LEFT_SHIFT) && !IsKeyDown(KEY_RIGHT_SHIFT))
    {
        float rotationSensitivity = 0.01f;
        cameraRot.yaw -= mouseDelta.x * rotationSensitivity;
        cameraRot.pitch += mouseDelta.y * rotationSensitivity;

        float pitchLimit = PI / 2.0f - 0.1f;
        if (cameraRot.pitch > pitchLimit)
            cameraRot.pitch = pitchLimit;
        if (cameraRot.pitch < -pitchLimit)
            cameraRot.pitch = -pitchLimit;

        while (cameraRot.yaw > PI)
            cameraRot.yaw -= 2.0f * PI;
        while (cameraRot.yaw < -PI)
            cameraRot.yaw += 2.0f * PI;
    }

    if (IsMouseButtonDown(MOUSE_MIDDLE_BUTTON) &&
        (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)))
    {
        if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)
        {
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

    cam->position = EulerToOrbitalPosition(cam->target, cameraRot.yaw, cameraRot.pitch, cameraDistance);
    cam->up = (Vector3){0, 1, 0};
}

static void UpdateEditorFlyCamera(Camera *cam)
{
    if (!cam)
        return;

    if (mouseUIEnabled)
    {
        ReleaseFlyCapture();
        return;
    }

    float scrollDelta = GetMouseWheelMove();
    if (scrollDelta != 0.0f)
    {
        if (CtrlHeld())
        {
            cam->fovy -= scrollDelta * 2.0f;
            if (cam->projection == CAMERA_ORTHOGRAPHIC)
                cam->fovy = ClampFloat(cam->fovy, 0.2f, 200.0f);
            else
                cam->fovy = ClampFloat(cam->fovy, 10.0f, 140.0f);
        }
        else
        {
            float speedScale = powf(1.18f, scrollDelta);
            float lookScale = powf(1.10f, scrollDelta);
            flyMoveSpeed = ClampFloat(flyMoveSpeed * speedScale, 0.5f, 180.0f);
            flyLookSensitivity = ClampFloat(flyLookSensitivity * lookScale, 0.0010f, 0.03f);
        }
    }

    bool wantsCapture = IsMouseButtonDown(MOUSE_RIGHT_BUTTON);
    if (wantsCapture && !flyCaptureActive)
    {
        SyncFlyStateFromCamera(cam);
        DisableCursor();
        flyCaptureActive = true;
    }
    else if (!wantsCapture && flyCaptureActive)
    {
        ReleaseFlyCapture();
        SyncOrbitStateFromCamera(cam);
    }

    if (!flyCaptureActive)
        return;

    Vector2 mouseDelta = GetMouseDelta();
    flyRot.yaw -= mouseDelta.x * flyLookSensitivity;
    flyRot.pitch -= mouseDelta.y * flyLookSensitivity;

    float pitchLimit = PI / 2.0f - 0.02f;
    if (flyRot.pitch > pitchLimit)
        flyRot.pitch = pitchLimit;
    if (flyRot.pitch < -pitchLimit)
        flyRot.pitch = -pitchLimit;

    while (flyRot.yaw > PI)
        flyRot.yaw -= 2.0f * PI;
    while (flyRot.yaw < -PI)
        flyRot.yaw += 2.0f * PI;

    Vector3 forward = Vec3Normalize(GetForwardFromRotation(flyRot.yaw, flyRot.pitch));
    Vector3 worldUp = (Vector3){0.0f, 1.0f, 0.0f};
    Vector3 right = Vec3Normalize(Vec3Cross(forward, worldUp));
    if (Vec3Length(right) < 0.0001f)
        right = (Vector3){1.0f, 0.0f, 0.0f};
    Vector3 up = Vec3Normalize(Vec3Cross(right, forward));

    Vector3 move = {0};
    if (IsKeyDown(KEY_W))
        move = Vec3Add(move, forward);
    if (IsKeyDown(KEY_S))
        move = Vec3Sub(move, forward);
    if (IsKeyDown(KEY_D))
        move = Vec3Add(move, right);
    if (IsKeyDown(KEY_A))
        move = Vec3Sub(move, right);

    float moveLen = Vec3Length(move);
    if (moveLen > 0.0001f)
    {
        float moveScale = flyMoveSpeed * GetFrameTime();
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
            moveScale *= 2.5f;
        move = Vec3Scale(move, 1.0f / moveLen);
        move = Vec3Scale(move, moveScale);
        cam->position = Vec3Add(cam->position, move);
    }

    cam->target = Vec3Add(cam->position, forward);
    cam->up = up;
    SyncOrbitStateFromCamera(cam);
}

void UpdateEditorViewportCamera(Camera *cam, bool flyModeActive)
{
    if (!cam)
        return;

    if (flyModeActive != flyModeWasActive)
    {
        ReleaseFlyCapture();
        if (flyModeActive)
            SyncFlyStateFromCamera(cam);
        else
            SyncOrbitStateFromCamera(cam);
        flyModeWasActive = flyModeActive;
    }

    if (flyModeActive)
        UpdateEditorFlyCamera(cam);
    else
        UpdateEditorOrbitCamera(cam);
}

Camera InitCamera()
{
    mainCamera.position = (Vector3){6, 6, 6};
    mainCamera.target = (Vector3){0, 0, 0};
    mainCamera.up = (Vector3){0, 1, 0};
    mainCamera.fovy = 45.0f;
    mainCamera.projection = CAMERA_PERSPECTIVE;
    SyncCameraControllerToCamera(&mainCamera);

    return mainCamera;
}
