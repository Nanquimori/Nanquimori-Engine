#include "gizmo.h"
#include "scene/outliner.h"
#include "editor/ui/properties_panel.h"
#include "editor/viewport/camera_controller.h"
#include "raymath.h"
#include <math.h>

static Vector2 Projetar(Camera cam, Vector3 dir)
{
    Matrix m = GetCameraMatrix(cam);
    return (Vector2){
        dir.x * m.m0 + dir.y * m.m4 + dir.z * m.m8,
        -(dir.x * m.m1 + dir.y * m.m5 + dir.z * m.m9)};
}

static Vector2 Normalizar2D(Vector2 v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y);
    if (len < 0.0001f)
        return (Vector2){0, 0};
    return (Vector2){v.x / len, v.y / len};
}

static void DrawTrianguloCima(Vector2 p, Color cor)
{
    DrawTriangle(
        (Vector2){p.x, p.y - 7},
        (Vector2){p.x - 6, p.y + 5},
        (Vector2){p.x + 6, p.y + 5},
        cor);
}

static void DrawTrianguloBaixo(Vector2 p, Color cor)
{
    DrawTriangle(
        (Vector2){p.x, p.y + 7},
        (Vector2){p.x - 6, p.y - 5},
        (Vector2){p.x + 6, p.y - 5},
        cor);
}

static void DrawTexto(const char *txt, Vector2 base, Vector2 off, Color cor)
{
    DrawText(txt, (int)(base.x + off.x), (int)(base.y + off.y), 10, cor);
}

static Color GizmoOverlayColor(Color c, float alpha)
{
    return Fade(c, alpha);
}

static void DrawRing(Vector3 center, Vector3 normal, float radius, Color cor)
{
    normal = Vector3Normalize(normal);
    Vector3 ref = (fabsf(normal.y) < 0.99f) ? (Vector3){0, 1, 0} : (Vector3){1, 0, 0};
    Vector3 b1 = Vector3Normalize(Vector3CrossProduct(ref, normal));
    Vector3 b2 = Vector3CrossProduct(normal, b1);

    const int SEG = 64;
    Vector3 prev = {0};
    for (int i = 0; i <= SEG; i++)
    {
        float t = (float)i / (float)SEG * 2.0f * PI;
        Vector3 p = Vector3Add(center,
                               Vector3Add(Vector3Scale(b1, cosf(t) * radius),
                                          Vector3Scale(b2, sinf(t) * radius)));
        if (i > 0)
            DrawLine3D(prev, p, cor);
        prev = p;
    }
}

static bool RayPlaneUnitVector(Camera cam, Vector3 center, Vector3 planeNormal, Vector3 *outUnit, float *outRadius, float *outDenomAbs)
{
    if (!outUnit)
        return false;
    planeNormal = Vector3Normalize(planeNormal);
    Ray ray = GetMouseRay(GetMousePosition(), cam);
    float denom = Vector3DotProduct(planeNormal, ray.direction);
    if (fabsf(denom) < 1e-6f)
        return false;
    if (outDenomAbs)
        *outDenomAbs = fabsf(denom);
    float t = Vector3DotProduct(Vector3Subtract(center, ray.position), planeNormal) / denom;
    if (t < 0.0f)
        return false;
    Vector3 hit = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
    Vector3 v = Vector3Subtract(hit, center);
    // Numerical safety: project onto plane.
    v = Vector3Subtract(v, Vector3Scale(planeNormal, Vector3DotProduct(v, planeNormal)));
    float r = Vector3Length(v);
    if (r < 1e-4f)
        return false;
    *outUnit = Vector3Scale(v, 1.0f / r);
    if (outRadius)
        *outRadius = r;
    return true;
}

void DrawGizmo(Camera cam, int largura)
{
    const float topBarH = 24.0f;
    const float edgePadding = 20.0f;
    const float outerRadius = GIZMO_RAIO + 4.0f;
    const float viewportRight = (float)largura - (float)PROPERTIES_PAINEL_LARGURA;
    Vector2 centro = {
        viewportRight - outerRadius - edgePadding,
        topBarH + outerRadius + edgePadding};

    const float alpha = 0.24f;

    DrawCircleV(centro, GIZMO_RAIO + 4, GizmoOverlayColor(COR_FUNDO, alpha));
    DrawCircleV(centro, GIZMO_RAIO, GizmoOverlayColor(COR_PAINEL, alpha));
    DrawCircleLinesV(centro, GIZMO_RAIO, GizmoOverlayColor(COR_BORDA, alpha));

    Vector3 dirN = {0, 0, -1};
    Vector3 dirS = {0, 0, 1};
    Vector3 dirL = {1, 0, 0};
    Vector3 dirO = {-1, 0, 0};
    Vector3 dirY = {0, 1, 0};

    Vector2 pN = Projetar(cam, dirN);
    Vector2 pS = Projetar(cam, dirS);
    Vector2 pL = Projetar(cam, dirL);
    Vector2 pO = Projetar(cam, dirO);
    Vector2 pY = Normalizar2D(Projetar(cam, dirY));

    float escala = GIZMO_RAIO * 0.75f;
    float escalaY = GIZMO_RAIO * 0.55f;

    Vector2 nPos = {centro.x + pN.x * escala, centro.y + pN.y * escala};
    Vector2 sPos = {centro.x + pS.x * escala, centro.y + pS.y * escala};
    Vector2 lPos = {centro.x + pL.x * escala, centro.y + pL.y * escala};
    Vector2 oPos = {centro.x + pO.x * escala, centro.y + pO.y * escala};

    DrawLineEx(centro, nPos, 2, COR_NORTE);
    DrawLineEx(centro, sPos, 2, COR_SUL);
    DrawLineEx(centro, lPos, 2, COR_LESTE);
    DrawLineEx(centro, oPos, 2, COR_OESTE);

    DrawTexto("N", nPos, (Vector2){-4, -16}, (Color){242, 239, 231, 255});
    DrawTexto("-Z", nPos, (Vector2){-6, 2}, COR_Z_NEG);

    DrawTexto("S", sPos, (Vector2){-4, -16}, (Color){242, 239, 231, 255});
    DrawTexto("+Z", sPos, (Vector2){-6, 2}, COR_Z_POS);

    DrawTexto("L", lPos, (Vector2){-4, -16}, (Color){242, 239, 231, 255});
    DrawTexto("+X", lPos, (Vector2){-6, 2}, COR_X_POS);

    DrawTexto("O", oPos, (Vector2){-4, -16}, (Color){242, 239, 231, 255});
    DrawTexto("-X", oPos, (Vector2){-6, 2}, COR_X_NEG);

    Vector2 yPos = {centro.x + pY.x * escalaY, centro.y + pY.y * escalaY};
    Vector2 yNeg = {centro.x - pY.x * escalaY, centro.y - pY.y * escalaY};

    DrawLineEx(yNeg, yPos, 2, COR_Y_POS);

    DrawTrianguloCima(yPos, COR_Y_POS);
    DrawTrianguloBaixo(yNeg, COR_Y_NEG);

    DrawTexto("+Y", yPos, (Vector2){-6, -18}, COR_Y_POS);
    DrawTexto("-Y", yNeg, (Vector2){-6, 8}, COR_Y_NEG);

}

static Vector3 AxisDir(int axis)
{
    if (axis == 0)
        return (Vector3){1, 0, 0};
    if (axis == 1)
        return (Vector3){0, 1, 0};
    return (Vector3){0, 0, 1};
}

static Color AxisColor(int axis)
{
    if (axis == 0)
        return (Color){184, 38, 36, 255};
    if (axis == 1)
        return GREEN;
    return BLUE;
}

static Color AxisColorRotate(int axis)
{
    if (axis == 0)
        return BLUE;
    if (axis == 1)
        return GREEN;
    return (Color){184, 38, 36, 255};
}

static float AxisHandleLength(Camera cam, Vector3 pos)
{
    float dx = cam.position.x - pos.x;
    float dy = cam.position.y - pos.y;
    float dz = cam.position.z - pos.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    float len = dist * 0.15f;
    if (len < 0.6f)
        len = 0.6f;
    if (len > 4.0f)
        len = 4.0f;
    return len;
}

static bool RayAxisClosest(Camera cam, Vector3 origin, Vector3 axisDir, float *outT, float *outDist)
{
    Ray ray = GetMouseRay(GetMousePosition(), cam);
    Vector3 d1 = ray.direction;
    Vector3 d2 = axisDir;

    float a = d1.x * d1.x + d1.y * d1.y + d1.z * d1.z;
    float b = d1.x * d2.x + d1.y * d2.y + d1.z * d2.z;
    float c = d2.x * d2.x + d2.y * d2.y + d2.z * d2.z;

    Vector3 r = (Vector3){ray.position.x - origin.x, ray.position.y - origin.y, ray.position.z - origin.z};
    float d = d1.x * r.x + d1.y * r.y + d1.z * r.z;
    float e = d2.x * r.x + d2.y * r.y + d2.z * r.z;

    float denom = a * c - b * b;
    if (fabsf(denom) < 0.000001f)
        return false;

    float t1 = (b * e - c * d) / denom;
    float t2 = (a * e - b * d) / denom;

    if (t1 < 0.0f)
        return false;

    Vector3 p1 = (Vector3){ray.position.x + d1.x * t1, ray.position.y + d1.y * t1, ray.position.z + d1.z * t1};
    Vector3 p2 = (Vector3){origin.x + d2.x * t2, origin.y + d2.y * t2, origin.z + d2.z * t2};
    float dx = p1.x - p2.x;
    float dy = p1.y - p2.y;
    float dz = p1.z - p2.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (outT)
        *outT = t2;
    if (outDist)
        *outDist = dist;
    return true;
}

static bool gizmoDragging = false;
static int gizmoAxis = -1;
static float gizmoStartT = 0.0f;
static float gizmoLastT = 0.0f;
static Vector3 gizmoStartPos = {0};
static Vector3 gizmoStartRot = {0};
static Vector3 gizmoStartRingVec = {0};
static const float GIZMO_SNAP_STEP = 0.5f;
static const float GIZMO_SLOW_FACTOR = 0.2f;
#define GIZMO_ROT_SNAP (15.0f * (PI / 180.0f))

typedef enum
{
    GIZMO_MODE_MOVE = 0,
    GIZMO_MODE_ROTATE = 1
} GizmoMode;

static GizmoMode gizmoMode = GIZMO_MODE_MOVE;
#define GIZMO_UNDO_MAX 32
typedef struct
{
    int id;
    Vector3 pos;
    Vector3 rot;
} GizmoUndoEntry;

static GizmoUndoEntry gizmoUndoStack[GIZMO_UNDO_MAX];
static int gizmoUndoTop = 0;
static GizmoUndoEntry gizmoRedoStack[GIZMO_UNDO_MAX];
static int gizmoRedoTop = 0;

void UpdateMoveGizmo(Camera cam)
{
    // Keep transform mode shortcuts responsive even when mouse is over UI.
    if (IsKeyPressed(KEY_W))
        gizmoMode = GIZMO_MODE_MOVE;
    if (IsKeyPressed(KEY_R))
        gizmoMode = GIZMO_MODE_ROTATE;

    if (!IsMouseEnabledForUI() && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_Z))
    {
        if (gizmoUndoTop > 0)
        {
            GizmoUndoEntry entry = gizmoUndoStack[--gizmoUndoTop];
            int idxUndo = BuscarIndicePorId(entry.id);
            if (idxUndo != -1)
            {
                if (gizmoRedoTop < GIZMO_UNDO_MAX)
                    gizmoRedoStack[gizmoRedoTop++] = (GizmoUndoEntry){entry.id, objetos[idxUndo].posicao, objetos[idxUndo].rotacao};
                else
                {
                    for (int i = 0; i < GIZMO_UNDO_MAX - 1; i++)
                        gizmoRedoStack[i] = gizmoRedoStack[i + 1];
                    gizmoRedoStack[GIZMO_UNDO_MAX - 1] = (GizmoUndoEntry){entry.id, objetos[idxUndo].posicao, objetos[idxUndo].rotacao};
                }
                objetos[idxUndo].posicao = entry.pos;
                objetos[idxUndo].rotacao = entry.rot;
            }
        }
    }

    if (IsMouseEnabledForUI())
        return;
    if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_Y))
    {
        if (gizmoRedoTop > 0)
        {
            GizmoUndoEntry entry = gizmoRedoStack[--gizmoRedoTop];
            int idxRedo = BuscarIndicePorId(entry.id);
            if (idxRedo != -1)
            {
                if (gizmoUndoTop < GIZMO_UNDO_MAX)
                    gizmoUndoStack[gizmoUndoTop++] = (GizmoUndoEntry){entry.id, objetos[idxRedo].posicao, objetos[idxRedo].rotacao};
                else
                {
                    for (int i = 0; i < GIZMO_UNDO_MAX - 1; i++)
                        gizmoUndoStack[i] = gizmoUndoStack[i + 1];
                    gizmoUndoStack[GIZMO_UNDO_MAX - 1] = (GizmoUndoEntry){entry.id, objetos[idxRedo].posicao, objetos[idxRedo].rotacao};
                }
                objetos[idxRedo].posicao = entry.pos;
                objetos[idxRedo].rotacao = entry.rot;
            }
        }
    }

    int selId = ObterObjetoSelecionadoId();
    if (selId <= 0)
        return;
    int idx = BuscarIndicePorId(selId);
    if (idx == -1)
        return;

    Vector3 pos = objetos[idx].posicao;
    Vector3 rot = objetos[idx].rotacao;
    float len = AxisHandleLength(cam, pos);
    float pickThreshold = len * 0.08f;
    if (pickThreshold < 0.05f)
        pickThreshold = 0.05f;
    float rotRadius = len * 0.6f;

    if (!gizmoDragging && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        float bestDist = 1000000.0f;
        float bestDiff = 0.0f;
        int bestAxis = -1;
        float bestT = 0.0f;

        for (int a = 0; a < 3; a++)
        {
            Vector3 axis = AxisDir(a);
            if (gizmoMode == GIZMO_MODE_MOVE)
            {
                float t = 0.0f;
                float dist = 0.0f;
                if (!RayAxisClosest(cam, pos, axis, &t, &dist))
                    continue;
                if (dist < bestDist)
                {
                    bestDist = dist;
                    bestAxis = a;
                    bestT = t;
                }
            }
            else
            {
                Vector3 unit = {0};
                float r = 0.0f;
                float denomAbs = 0.0f;
                if (!RayPlaneUnitVector(cam, pos, axis, &unit, &r, &denomAbs))
                    continue;
                if (denomAbs < 0.2f)
                    continue;
                float diff = fabsf(r - rotRadius);
                float score = diff / denomAbs;
                if (score < bestDist)
                {
                    bestDist = score;
                    bestDiff = diff;
                    bestAxis = a;
                    bestT = 0.0f; // unused in rotate mode
                }
            }
        }

        float metric = (gizmoMode == GIZMO_MODE_ROTATE) ? bestDiff : bestDist;
        float allow = (gizmoMode == GIZMO_MODE_ROTATE) ? (rotRadius * 0.12f) : pickThreshold;
        if (bestAxis != -1 && metric <= allow)
        {
            gizmoDragging = true;
            gizmoAxis = bestAxis;
            gizmoStartT = bestT;
            gizmoLastT = bestT;
            gizmoStartPos = pos;
            gizmoStartRot = rot;
            if (gizmoMode == GIZMO_MODE_ROTATE)
            {
                Vector3 axis = AxisDir(gizmoAxis);
                Vector3 unit = {0};
                if (RayPlaneUnitVector(cam, pos, axis, &unit, nullptr, nullptr))
                    gizmoStartRingVec = unit;
                else
                    gizmoStartRingVec = (Vector3){1, 0, 0};
            }
        }
    }

    if (gizmoDragging)
    {
        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON))
        {
            Vector3 axis = AxisDir(gizmoAxis);
            if (gizmoMode == GIZMO_MODE_MOVE)
            {
                float t = 0.0f;
                float dist = 0.0f;
                if (RayAxisClosest(cam, gizmoStartPos, axis, &t, &dist))
                {
                    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
                    {
                        float delta = t - gizmoStartT;
                        delta = floorf(delta / GIZMO_SNAP_STEP + 0.5f) * GIZMO_SNAP_STEP;
                        objetos[idx].posicao = (Vector3){
                            gizmoStartPos.x + axis.x * delta,
                            gizmoStartPos.y + axis.y * delta,
                            gizmoStartPos.z + axis.z * delta};
                    }
                    else
                    {
                        float deltaStep = t - gizmoLastT;
                        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                            deltaStep *= GIZMO_SLOW_FACTOR;
                        objetos[idx].posicao = (Vector3){
                            objetos[idx].posicao.x + axis.x * deltaStep,
                            objetos[idx].posicao.y + axis.y * deltaStep,
                            objetos[idx].posicao.z + axis.z * deltaStep};
                    }
                    gizmoLastT = t;
                }
            }
            else
            {
                Vector3 curUnit = {0};
                if (!RayPlaneUnitVector(cam, gizmoStartPos, axis, &curUnit, nullptr, nullptr))
                    curUnit = gizmoStartRingVec;

                Vector3 c = Vector3CrossProduct(gizmoStartRingVec, curUnit);
                float sinA = Vector3DotProduct(axis, c);
                float cosA = Vector3DotProduct(gizmoStartRingVec, curUnit);
                float angle = atan2f(sinA, cosA);

                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
                    angle *= GIZMO_SLOW_FACTOR;
                if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
                    angle = floorf(angle / GIZMO_ROT_SNAP + 0.5f) * GIZMO_ROT_SNAP;

                Vector3 newRot = gizmoStartRot;
                if (gizmoAxis == 0)
                    newRot.x = gizmoStartRot.x + angle;
                else if (gizmoAxis == 1)
                    newRot.y = gizmoStartRot.y + angle;
                else
                    newRot.z = gizmoStartRot.z + angle;
                objetos[idx].rotacao = newRot;
            }
        }
        else
        {
            gizmoDragging = false;
            gizmoAxis = -1;
            if (idx != -1)
            {
                Vector3 cur = objetos[idx].posicao;
                Vector3 curRot = objetos[idx].rotacao;
                bool changed = (cur.x != gizmoStartPos.x || cur.y != gizmoStartPos.y || cur.z != gizmoStartPos.z) ||
                               (curRot.x != gizmoStartRot.x || curRot.y != gizmoStartRot.y || curRot.z != gizmoStartRot.z);
                if (changed)
                {
                    if (gizmoUndoTop < GIZMO_UNDO_MAX)
                    {
                        gizmoUndoStack[gizmoUndoTop++] = (GizmoUndoEntry){selId, gizmoStartPos, gizmoStartRot};
                    }
                    else
                    {
                        for (int i = 0; i < GIZMO_UNDO_MAX - 1; i++)
                            gizmoUndoStack[i] = gizmoUndoStack[i + 1];
                        gizmoUndoStack[GIZMO_UNDO_MAX - 1] = (GizmoUndoEntry){selId, gizmoStartPos, gizmoStartRot};
                    }
                    gizmoRedoTop = 0;
                }
            }
        }
    }
}

void DrawMoveGizmo(Camera cam)
{
    int selId = ObterObjetoSelecionadoId();
    if (selId <= 0)
        return;
    int idx = BuscarIndicePorId(selId);
    if (idx == -1)
        return;

    Vector3 pos = objetos[idx].posicao;
    float len = AxisHandleLength(cam, pos);

    if (gizmoMode == GIZMO_MODE_ROTATE)
    {
        float radius = len * 0.6f;
        for (int a = 0; a < 3; a++)
        {
            Vector3 axis = AxisDir(a);
            Color cor = AxisColorRotate(a);
            if (gizmoDragging && gizmoAxis == a)
                cor = (Color){242, 239, 231, 255};
            DrawRing(pos, axis, radius, cor);
        }
    }
    else
    {
        for (int a = 0; a < 3; a++)
        {
            Vector3 axis = AxisDir(a);
            Vector3 end = (Vector3){pos.x + axis.x * len, pos.y + axis.y * len, pos.z + axis.z * len};
            Color cor = AxisColor(a);
            if (gizmoDragging && gizmoAxis == a)
                cor = (Color){242, 239, 231, 255};
            DrawLine3D(pos, end, cor);
            DrawSphere(end, len * 0.04f, cor);
        }
    }
}
