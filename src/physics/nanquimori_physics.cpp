#include "physics/nanquimori_physics.h"
#include "assets/model_manager.h"
#include "editor/ui/properties_panel.h"
#include "raymath.h"
#include "scene/outliner.h"
#include <float.h>
#include <math.h>
#include <vector>

typedef struct
{
    int objectId;
    Vector3 linearVelocity;
    Vector3 angularVelocity;
    bool grounded;
    bool active;
} NanquimoriBodyState;

typedef struct
{
    int id;
    Vector3 center;
    Vector3 axis[3];
    Vector3 half;
} NanquimoriOBB;

typedef struct
{
    bool valid;
    BoundingBox localBounds;
} CollisionShapeCache;

struct TerrainCollisionCache
{
    int objectId = 0;
    bool active = false;
    bool valid = false;
    int width = 0;
    int depth = 0;
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    float stepX = 0.0f;
    float stepZ = 0.0f;
    BoundingBox bounds = {{0}};
    std::vector<float> heights;
    std::vector<unsigned char> validMask;
};

typedef struct
{
    int objectIndex;
    int objectId;
    float inverseMass;
    bool hasCollider;
    CollisionShapeCache shape;
    NanquimoriOBB obb;
    BoundingBox aabb;
    NanquimoriBodyState *state;
} DynamicBodyRef;

typedef struct
{
    int objectIndex;
    int objectId;
    CollisionShapeCache shape;
    NanquimoriOBB obb;
    BoundingBox aabb;
} StaticColliderRef;

typedef struct
{
    float gravity;
    float restitution;
    float friction;
    float damping;
    float groundFriction;
    float fixedDelta;
    float maxFrameDelta;
    int maxSubSteps;
    int solverIterations;
} NanquimoriPhysicsSettings;

typedef struct
{
    NanquimoriBodyState bodies[MAX_OBJETOS];
    TerrainCollisionCache terrainCaches[MAX_OBJETOS];
    float accumulator;
    float profileMs;
    bool initialized;
    NanquimoriPhysicsSettings settings;
} NanquimoriPhysicsWorld;

static NanquimoriPhysicsWorld gPhysics = {0};

static float Minf(float a, float b)
{
    return (a < b) ? a : b;
}

static float Maxf(float a, float b)
{
    return (a > b) ? a : b;
}

static float Clampf(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static int Clampi(int value, int minValue, int maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

static Vector3 NormalizeSafe(Vector3 v, Vector3 fallback)
{
    float len = Vector3Length(v);
    if (len <= 0.00001f)
        return fallback;
    return Vector3Scale(v, 1.0f / len);
}

static bool BoxesOverlap(const BoundingBox *a, const BoundingBox *b)
{
    if (!a || !b)
        return false;

    return (a->min.x <= b->max.x && a->max.x >= b->min.x) &&
           (a->min.y <= b->max.y && a->max.y >= b->min.y) &&
           (a->min.z <= b->max.z && a->max.z >= b->min.z);
}

static Matrix RotationFromEuler(Vector3 euler)
{
    return MatrixRotateXYZ((Vector3){euler.x, euler.y, euler.z});
}

static Vector3 MatGetColumn(const Matrix *matrix, int column)
{
    switch (column)
    {
    case 0:
        return (Vector3){matrix->m0, matrix->m1, matrix->m2};
    case 1:
        return (Vector3){matrix->m4, matrix->m5, matrix->m6};
    default:
        return (Vector3){matrix->m8, matrix->m9, matrix->m10};
    }
}

static bool TryGetObjectModelByIndex(int objectIndex, Model **outModel)
{
    if (objectIndex < 0 || objectIndex >= totalObjetos)
        return false;

    ObjetoCena *obj = &objetos[objectIndex];
    if (!obj->ativo)
        return false;

    for (int i = 0; i < modelManager.modelCount; i++)
    {
        LoadedModel *loaded = &modelManager.models[i];
        if (!loaded->loaded || loaded->idObjeto != obj->id)
            continue;
        if (loaded->cacheIndex < 0 || loaded->cacheIndex >= modelManager.cacheCount)
            continue;
        if (!modelManager.cache[loaded->cacheIndex].loaded)
            continue;

        if (outModel)
            *outModel = &modelManager.cache[loaded->cacheIndex].model;
        return true;
    }

    return false;
}

static bool TryGetObjectModelTransformByIndex(int objectIndex, Model **outModel, Matrix *outTransform)
{
    if (objectIndex < 0 || objectIndex >= totalObjetos)
        return false;

    ObjetoCena *obj = &objetos[objectIndex];
    if (!obj->ativo)
        return false;

    Model *model = nullptr;
    if (!TryGetObjectModelByIndex(objectIndex, &model) || !model)
        return false;

    Matrix world = MatrixTranslate(obj->posicao.x, obj->posicao.y, obj->posicao.z);
    if (obj->rotacao.x != 0.0f || obj->rotacao.y != 0.0f || obj->rotacao.z != 0.0f)
        world = MatrixMultiply(world, MatrixRotateXYZ(obj->rotacao));

    if (outModel)
        *outModel = model;
    if (outTransform)
        *outTransform = MatrixMultiply(world, model->transform);
    return true;
}

static bool TryGetLocalBoundsByIndex(int objectIndex, BoundingBox *outBounds)
{
    if (objectIndex < 0 || objectIndex >= totalObjetos || !outBounds)
        return false;

    Model *model = nullptr;
    if (TryGetObjectModelByIndex(objectIndex, &model) && model)
    {
        *outBounds = GetModelBoundingBox(*model);
        return true;
    }

    outBounds->min = (Vector3){-0.5f, -0.5f, -0.5f};
    outBounds->max = (Vector3){0.5f, 0.5f, 0.5f};
    return true;
}

static void BuildCollisionOBB(const ObjetoCena *obj, const BoundingBox *localBounds, NanquimoriOBB *outObb)
{
    Vector3 localCenter = {
        (localBounds->min.x + localBounds->max.x) * 0.5f,
        (localBounds->min.y + localBounds->max.y) * 0.5f,
        (localBounds->min.z + localBounds->max.z) * 0.5f};
    Vector3 half = {
        (localBounds->max.x - localBounds->min.x) * 0.5f,
        (localBounds->max.y - localBounds->min.y) * 0.5f,
        (localBounds->max.z - localBounds->min.z) * 0.5f};
    Vector3 scale = PropertiesGetCollisionSize(obj->id);
    Matrix rotation = RotationFromEuler(obj->rotacao);

    if (scale.x <= 0.0f)
        scale.x = 1.0f;
    if (scale.y <= 0.0f)
        scale.y = 1.0f;
    if (scale.z <= 0.0f)
        scale.z = 1.0f;

    outObb->id = obj->id;
    outObb->axis[0] = NormalizeSafe(MatGetColumn(&rotation, 0), (Vector3){1.0f, 0.0f, 0.0f});
    outObb->axis[1] = NormalizeSafe(MatGetColumn(&rotation, 1), (Vector3){0.0f, 1.0f, 0.0f});
    outObb->axis[2] = NormalizeSafe(MatGetColumn(&rotation, 2), (Vector3){0.0f, 0.0f, 1.0f});
    outObb->center = Vector3Add(obj->posicao, Vector3Transform(localCenter, rotation));
    outObb->half = (Vector3){
        Maxf(0.0005f, half.x * scale.x),
        Maxf(0.0005f, half.y * scale.y),
        Maxf(0.0005f, half.z * scale.z)};
}

static bool TryGetObjectCollisionOBBByIndex(int objectIndex, NanquimoriOBB *outObb)
{
    if (objectIndex < 0 || objectIndex >= totalObjetos || !outObb)
        return false;

    ObjetoCena *obj = &objetos[objectIndex];
    if (!obj->ativo || PropertiesIsTerrain(obj->id))
        return false;

    BoundingBox localBounds = {0};
    if (!TryGetLocalBoundsByIndex(objectIndex, &localBounds))
        return false;

    BuildCollisionOBB(obj, &localBounds, outObb);
    return true;
}

static BoundingBox OBBToAABB(const NanquimoriOBB *obb)
{
    Vector3 axisX = Vector3Scale(obb->axis[0], obb->half.x);
    Vector3 axisY = Vector3Scale(obb->axis[1], obb->half.y);
    Vector3 axisZ = Vector3Scale(obb->axis[2], obb->half.z);
    Vector3 corners[8];
    int index = 0;

    for (int x = -1; x <= 1; x += 2)
    {
        for (int y = -1; y <= 1; y += 2)
        {
            for (int z = -1; z <= 1; z += 2)
            {
                Vector3 p = obb->center;
                p = Vector3Add(p, Vector3Scale(axisX, (float)x));
                p = Vector3Add(p, Vector3Scale(axisY, (float)y));
                p = Vector3Add(p, Vector3Scale(axisZ, (float)z));
                corners[index++] = p;
            }
        }
    }

    BoundingBox box = {corners[0], corners[0]};
    for (int i = 1; i < 8; i++)
    {
        box.min.x = Minf(box.min.x, corners[i].x);
        box.min.y = Minf(box.min.y, corners[i].y);
        box.min.z = Minf(box.min.z, corners[i].z);
        box.max.x = Maxf(box.max.x, corners[i].x);
        box.max.y = Maxf(box.max.y, corners[i].y);
        box.max.z = Maxf(box.max.z, corners[i].z);
    }

    return box;
}

static float ProjectHalfExtent(const NanquimoriOBB *obb, Vector3 axis)
{
    return fabsf(Vector3DotProduct(obb->axis[0], axis)) * obb->half.x +
           fabsf(Vector3DotProduct(obb->axis[1], axis)) * obb->half.y +
           fabsf(Vector3DotProduct(obb->axis[2], axis)) * obb->half.z;
}

static bool OBBIntersect(const NanquimoriOBB *a, const NanquimoriOBB *b, Vector3 *axisOut, float *depthOut)
{
    Vector3 axes[15];
    int axisCount = 0;
    Vector3 delta = Vector3Subtract(b->center, a->center);
    float minDepth = FLT_MAX;
    Vector3 minAxis = {0};

    axes[axisCount++] = a->axis[0];
    axes[axisCount++] = a->axis[1];
    axes[axisCount++] = a->axis[2];
    axes[axisCount++] = b->axis[0];
    axes[axisCount++] = b->axis[1];
    axes[axisCount++] = b->axis[2];

    for (int i = 0; i < 3; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            Vector3 cross = Vector3CrossProduct(a->axis[i], b->axis[j]);
            float len = Vector3Length(cross);
            if (len > 0.00001f)
                axes[axisCount++] = Vector3Scale(cross, 1.0f / len);
        }
    }

    for (int i = 0; i < axisCount; i++)
    {
        Vector3 axis = NormalizeSafe(axes[i], (Vector3){0.0f, 1.0f, 0.0f});
        float ra = ProjectHalfExtent(a, axis);
        float rb = ProjectHalfExtent(b, axis);
        float distance = fabsf(Vector3DotProduct(delta, axis));
        float overlap = ra + rb - distance;
        if (overlap < 0.0f)
            return false;
        if (overlap < minDepth)
        {
            minDepth = overlap;
            minAxis = axis;
        }
    }

    if (Vector3DotProduct(delta, minAxis) < 0.0f)
        minAxis = Vector3Negate(minAxis);

    if (axisOut)
        *axisOut = minAxis;
    if (depthOut)
        *depthOut = minDepth;
    return true;
}

static Vector3 ProjectVectorOnPlane(Vector3 value, Vector3 normal)
{
    float dot = Vector3DotProduct(value, normal);
    return Vector3Subtract(value, Vector3Scale(normal, dot));
}

static NanquimoriBodyState *FindBodyState(int objectId)
{
    if (objectId <= 0)
        return nullptr;

    for (int i = 0; i < MAX_OBJETOS; i++)
    {
        if (gPhysics.bodies[i].active && gPhysics.bodies[i].objectId == objectId)
            return &gPhysics.bodies[i];
    }

    return nullptr;
}

static NanquimoriBodyState *EnsureBodyState(int objectId)
{
    NanquimoriBodyState *existing = FindBodyState(objectId);
    if (existing)
        return existing;

    for (int i = 0; i < MAX_OBJETOS; i++)
    {
        if (!gPhysics.bodies[i].active)
        {
            gPhysics.bodies[i].active = true;
            gPhysics.bodies[i].objectId = objectId;
            gPhysics.bodies[i].linearVelocity = (Vector3){0};
            gPhysics.bodies[i].angularVelocity = (Vector3){0};
            gPhysics.bodies[i].grounded = false;
            return &gPhysics.bodies[i];
        }
    }

    return nullptr;
}

static void CleanupBodyStates(void)
{
    for (int i = 0; i < MAX_OBJETOS; i++)
    {
        NanquimoriBodyState *state = &gPhysics.bodies[i];
        if (!state->active)
            continue;

        int objectIndex = BuscarIndicePorId(state->objectId);
        if (objectIndex == -1 || !objetos[objectIndex].ativo ||
            PropertiesIsStatic(state->objectId) || !PropertiesIsRigidbody(state->objectId))
        {
            gPhysics.bodies[i] = (NanquimoriBodyState){0};
        }
    }
}

static void ClearTerrainCache(TerrainCollisionCache *cache)
{
    if (!cache)
        return;
    *cache = TerrainCollisionCache();
}

static void ClearTerrainCaches(void)
{
    for (int i = 0; i < MAX_OBJETOS; i++)
        ClearTerrainCache(&gPhysics.terrainCaches[i]);
}

static TerrainCollisionCache *FindTerrainCache(int objectId)
{
    for (int i = 0; i < MAX_OBJETOS; i++)
        if (gPhysics.terrainCaches[i].active && gPhysics.terrainCaches[i].objectId == objectId)
            return &gPhysics.terrainCaches[i];
    return nullptr;
}

static TerrainCollisionCache *AcquireTerrainCache(int objectId)
{
    TerrainCollisionCache *existing = FindTerrainCache(objectId);
    if (existing)
        return existing;

    for (int i = 0; i < MAX_OBJETOS; i++)
    {
        if (!gPhysics.terrainCaches[i].active)
        {
            ClearTerrainCache(&gPhysics.terrainCaches[i]);
            gPhysics.terrainCaches[i].active = true;
            gPhysics.terrainCaches[i].objectId = objectId;
            return &gPhysics.terrainCaches[i];
        }
    }

    return nullptr;
}

static float GetInverseMassForObject(int objectId)
{
    float mass = PropertiesGetMass(objectId);
    if (mass <= 0.0001f)
        return 0.0f;
    return 1.0f / mass;
}

static bool RefreshDynamicBodyCollision(DynamicBodyRef *body)
{
    if (!body || !body->hasCollider || !body->shape.valid)
        return false;

    BuildCollisionOBB(&objetos[body->objectIndex], &body->shape.localBounds, &body->obb);
    body->aabb = OBBToAABB(&body->obb);
    return true;
}

static void RefreshStaticColliderCollision(StaticColliderRef *collider)
{
    if (!collider || !collider->shape.valid)
        return;

    BuildCollisionOBB(&objetos[collider->objectIndex], &collider->shape.localBounds, &collider->obb);
    collider->aabb = OBBToAABB(&collider->obb);
}

static void ApplyHorizontalDamping(Vector3 *velocity, float damping, float deltaTime)
{
    float factor = Clampf(1.0f - damping * deltaTime * 60.0f, 0.0f, 1.0f);
    velocity->x *= factor;
    velocity->z *= factor;
}

static int GatherSceneBodies(DynamicBodyRef *dynamicBodies, StaticColliderRef *staticColliders, int *staticCount, int *terrainIndices, int *terrainCount)
{
    int dynamicCount = 0;
    *staticCount = 0;
    *terrainCount = 0;

    CleanupBodyStates();

    for (int i = 0; i < totalObjetos; i++)
    {
        ObjetoCena *obj = &objetos[i];
        if (!obj->ativo)
            continue;

        bool hasCollider = PropertiesHasCollider(obj->id);
        bool isStatic = PropertiesIsStatic(obj->id);
        bool isTerrain = PropertiesIsTerrain(obj->id);
        bool isRigidbody = PropertiesIsRigidbody(obj->id);
        if (!hasCollider && !isRigidbody)
            continue;

        if (isTerrain)
        {
            if (*terrainCount < MAX_OBJETOS)
                terrainIndices[(*terrainCount)++] = i;
            continue;
        }

        BoundingBox localBounds = {0};
        bool hasBounds = hasCollider && TryGetLocalBoundsByIndex(i, &localBounds);

        if (isStatic)
        {
            if (!hasCollider || !hasBounds || *staticCount >= MAX_OBJETOS)
                continue;

            staticColliders[*staticCount].objectIndex = i;
            staticColliders[*staticCount].objectId = obj->id;
            staticColliders[*staticCount].shape.valid = true;
            staticColliders[*staticCount].shape.localBounds = localBounds;
            RefreshStaticColliderCollision(&staticColliders[*staticCount]);
            (*staticCount)++;
            continue;
        }

        if (!isRigidbody || dynamicCount >= MAX_OBJETOS)
            continue;

        NanquimoriBodyState *state = EnsureBodyState(obj->id);
        if (!state)
            continue;

        dynamicBodies[dynamicCount].objectIndex = i;
        dynamicBodies[dynamicCount].objectId = obj->id;
        dynamicBodies[dynamicCount].inverseMass = GetInverseMassForObject(obj->id);
        dynamicBodies[dynamicCount].hasCollider = hasCollider && hasBounds;
        dynamicBodies[dynamicCount].shape.valid = hasBounds;
        dynamicBodies[dynamicCount].shape.localBounds = localBounds;
        dynamicBodies[dynamicCount].state = state;
        if (dynamicBodies[dynamicCount].hasCollider)
            RefreshDynamicBodyCollision(&dynamicBodies[dynamicCount]);
        dynamicCount++;
    }

    return dynamicCount;
}

static void ResolveContact(ObjetoCena *objectA, NanquimoriBodyState *stateA, float inverseMassA,
                           ObjetoCena *objectB, NanquimoriBodyState *stateB, float inverseMassB,
                           Vector3 normal, float depth)
{
    float inverseMassSum = inverseMassA + inverseMassB;
    if (inverseMassSum <= 0.0f)
        return;

    const float slop = 0.0005f;
    float correctionDepth = Maxf(depth - slop, 0.0f);
    Vector3 correction = Vector3Scale(normal, correctionDepth / inverseMassSum);

    if (objectA && inverseMassA > 0.0f)
        objectA->posicao = Vector3Add(objectA->posicao, Vector3Scale(correction, inverseMassA));
    if (objectB && inverseMassB > 0.0f)
        objectB->posicao = Vector3Subtract(objectB->posicao, Vector3Scale(correction, inverseMassB));

    Vector3 velocityA = stateA ? stateA->linearVelocity : (Vector3){0};
    Vector3 velocityB = stateB ? stateB->linearVelocity : (Vector3){0};
    Vector3 relativeVelocity = Vector3Subtract(velocityA, velocityB);
    float velocityAlongNormal = Vector3DotProduct(relativeVelocity, normal);
    if (velocityAlongNormal >= 0.0f)
        return;

    float impulseScalar = -(1.0f + gPhysics.settings.restitution) * velocityAlongNormal;
    impulseScalar /= inverseMassSum;

    Vector3 impulse = Vector3Scale(normal, impulseScalar);
    if (stateA && inverseMassA > 0.0f)
        stateA->linearVelocity = Vector3Add(stateA->linearVelocity, Vector3Scale(impulse, inverseMassA));
    if (stateB && inverseMassB > 0.0f)
        stateB->linearVelocity = Vector3Subtract(stateB->linearVelocity, Vector3Scale(impulse, inverseMassB));

    Vector3 tangent = Vector3Subtract(relativeVelocity, Vector3Scale(normal, velocityAlongNormal));
    float tangentLength = Vector3Length(tangent);
    if (tangentLength <= 0.00001f)
        return;

    tangent = Vector3Scale(tangent, 1.0f / tangentLength);
    float frictionImpulseScalar = -Vector3DotProduct(relativeVelocity, tangent);
    frictionImpulseScalar /= inverseMassSum;

    float maxFriction = impulseScalar * gPhysics.settings.friction;
    frictionImpulseScalar = Clampf(frictionImpulseScalar, -maxFriction, maxFriction);

    Vector3 frictionImpulse = Vector3Scale(tangent, frictionImpulseScalar);
    if (stateA && inverseMassA > 0.0f)
        stateA->linearVelocity = Vector3Add(stateA->linearVelocity, Vector3Scale(frictionImpulse, inverseMassA));
    if (stateB && inverseMassB > 0.0f)
        stateB->linearVelocity = Vector3Subtract(stateB->linearVelocity, Vector3Scale(frictionImpulse, inverseMassB));
}

static void ResolveDynamicStaticContacts(DynamicBodyRef *body, const StaticColliderRef *staticColliders, int staticCount)
{
    if (!body || !body->hasCollider)
        return;

    ObjetoCena *dynamicObject = &objetos[body->objectIndex];
    for (int i = 0; i < staticCount; i++)
    {
        if (staticColliders[i].objectId == body->objectId || !BoxesOverlap(&body->aabb, &staticColliders[i].aabb))
            continue;

        Vector3 normal = {0};
        float depth = 0.0f;
        if (!OBBIntersect(&body->obb, &staticColliders[i].obb, &normal, &depth))
            continue;

        ResolveContact(dynamicObject, body->state, body->inverseMass, nullptr, nullptr, 0.0f, normal, depth);
        RefreshDynamicBodyCollision(body);
    }
}

static void ResolveDynamicDynamicContacts(DynamicBodyRef *bodyA, DynamicBodyRef *bodyB)
{
    if (!bodyA || !bodyB || !bodyA->hasCollider || !bodyB->hasCollider)
        return;
    if (!BoxesOverlap(&bodyA->aabb, &bodyB->aabb))
        return;

    Vector3 normal = {0};
    float depth = 0.0f;
    if (!OBBIntersect(&bodyA->obb, &bodyB->obb, &normal, &depth))
        return;

    ResolveContact(&objetos[bodyA->objectIndex], bodyA->state, bodyA->inverseMass,
                   &objetos[bodyB->objectIndex], bodyB->state, bodyB->inverseMass,
                   normal, depth);
    RefreshDynamicBodyCollision(bodyA);
    RefreshDynamicBodyCollision(bodyB);
}

static bool BuildTriangleHeightAtXZ(Vector3 a, Vector3 b, Vector3 c, float x, float z, float *outHeight)
{
    float denom = ((b.z - c.z) * (a.x - c.x)) + ((c.x - b.x) * (a.z - c.z));
    if (fabsf(denom) <= 0.000001f)
        return false;

    float w1 = (((b.z - c.z) * (x - c.x)) + ((c.x - b.x) * (z - c.z))) / denom;
    float w2 = (((c.z - a.z) * (x - c.x)) + ((a.x - c.x) * (z - c.z))) / denom;
    float w3 = 1.0f - w1 - w2;
    const float epsilon = 0.0001f;
    if (w1 < -epsilon || w2 < -epsilon || w3 < -epsilon)
        return false;

    if (outHeight)
        *outHeight = a.y * w1 + b.y * w2 + c.y * w3;
    return true;
}

static void RasterizeTerrainTriangle(TerrainCollisionCache *cache, Vector3 a, Vector3 b, Vector3 c)
{
    float triMinX = Minf(a.x, Minf(b.x, c.x));
    float triMaxX = Maxf(a.x, Maxf(b.x, c.x));
    float triMinZ = Minf(a.z, Minf(b.z, c.z));
    float triMaxZ = Maxf(a.z, Maxf(b.z, c.z));

    int startX = Clampi((int)floorf((triMinX - cache->minX) / cache->stepX), 0, cache->width - 1);
    int endX = Clampi((int)ceilf((triMaxX - cache->minX) / cache->stepX), 0, cache->width - 1);
    int startZ = Clampi((int)floorf((triMinZ - cache->minZ) / cache->stepZ), 0, cache->depth - 1);
    int endZ = Clampi((int)ceilf((triMaxZ - cache->minZ) / cache->stepZ), 0, cache->depth - 1);

    for (int z = startZ; z <= endZ; z++)
    {
        float sampleZ = cache->minZ + cache->stepZ * (float)z;
        for (int x = startX; x <= endX; x++)
        {
            float sampleX = cache->minX + cache->stepX * (float)x;
            float height = 0.0f;
            if (!BuildTriangleHeightAtXZ(a, b, c, sampleX, sampleZ, &height))
                continue;

            int index = z * cache->width + x;
            if (!cache->validMask[index] || height > cache->heights[index])
            {
                cache->heights[index] = height;
                cache->validMask[index] = 1;
            }
        }
    }
}

static void FillTerrainCacheGaps(TerrainCollisionCache *cache)
{
    for (int pass = 0; pass < 6; pass++)
    {
        std::vector<float> nextHeights = cache->heights;
        std::vector<unsigned char> nextMask = cache->validMask;

        for (int z = 0; z < cache->depth; z++)
        {
            for (int x = 0; x < cache->width; x++)
            {
                int index = z * cache->width + x;
                if (cache->validMask[index])
                    continue;

                float total = 0.0f;
                int count = 0;
                for (int dz = -1; dz <= 1; dz++)
                {
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        if (dx == 0 && dz == 0)
                            continue;
                        int nx = x + dx;
                        int nz = z + dz;
                        if (nx < 0 || nz < 0 || nx >= cache->width || nz >= cache->depth)
                            continue;
                        int neighborIndex = nz * cache->width + nx;
                        if (!cache->validMask[neighborIndex])
                            continue;
                        total += cache->heights[neighborIndex];
                        count++;
                    }
                }

                if (count >= 2)
                {
                    nextHeights[index] = total / (float)count;
                    nextMask[index] = 1;
                }
            }
        }

        cache->heights.swap(nextHeights);
        cache->validMask.swap(nextMask);
    }
}

static bool BuildTerrainCollisionCache(int objectIndex, TerrainCollisionCache *cache)
{
    Model *model = nullptr;
    Matrix transform = {0};
    if (!TryGetObjectModelTransformByIndex(objectIndex, &model, &transform) || !model)
        return false;

    bool foundVertex = false;
    Vector3 minVertex = {0};
    Vector3 maxVertex = {0};

    for (int meshIndex = 0; meshIndex < model->meshCount; meshIndex++)
    {
        Mesh *mesh = &model->meshes[meshIndex];
        if (!mesh->vertices || mesh->vertexCount <= 0)
            continue;

        for (int vertexIndex = 0; vertexIndex < mesh->vertexCount; vertexIndex++)
        {
            Vector3 vertex = {
                mesh->vertices[vertexIndex * 3 + 0],
                mesh->vertices[vertexIndex * 3 + 1],
                mesh->vertices[vertexIndex * 3 + 2]};
            vertex = Vector3Transform(vertex, transform);

            if (!foundVertex)
            {
                minVertex = vertex;
                maxVertex = vertex;
                foundVertex = true;
            }
            else
            {
                minVertex.x = Minf(minVertex.x, vertex.x);
                minVertex.y = Minf(minVertex.y, vertex.y);
                minVertex.z = Minf(minVertex.z, vertex.z);
                maxVertex.x = Maxf(maxVertex.x, vertex.x);
                maxVertex.y = Maxf(maxVertex.y, vertex.y);
                maxVertex.z = Maxf(maxVertex.z, vertex.z);
            }
        }
    }

    if (!foundVertex)
        return false;

    float sizeX = Maxf(maxVertex.x - minVertex.x, 0.01f);
    float sizeZ = Maxf(maxVertex.z - minVertex.z, 0.01f);
    cache->width = Clampi((int)ceilf(sizeX * 1.5f), 16, 64);
    cache->depth = Clampi((int)ceilf(sizeZ * 1.5f), 16, 64);
    cache->minX = minVertex.x;
    cache->maxX = maxVertex.x;
    cache->minZ = minVertex.z;
    cache->maxZ = maxVertex.z;
    cache->stepX = sizeX / (float)(cache->width - 1);
    cache->stepZ = sizeZ / (float)(cache->depth - 1);
    cache->bounds.min = minVertex;
    cache->bounds.max = maxVertex;
    cache->heights.assign((size_t)cache->width * (size_t)cache->depth, -FLT_MAX);
    cache->validMask.assign((size_t)cache->width * (size_t)cache->depth, 0);

    for (int meshIndex = 0; meshIndex < model->meshCount; meshIndex++)
    {
        Mesh *mesh = &model->meshes[meshIndex];
        if (!mesh->vertices || mesh->vertexCount <= 0)
            continue;

        int triangleCount = (mesh->indices && mesh->triangleCount > 0) ? mesh->triangleCount : (mesh->vertexCount / 3);
        for (int triangleIndex = 0; triangleIndex < triangleCount; triangleIndex++)
        {
            int i0 = mesh->indices ? mesh->indices[triangleIndex * 3 + 0] : triangleIndex * 3 + 0;
            int i1 = mesh->indices ? mesh->indices[triangleIndex * 3 + 1] : triangleIndex * 3 + 1;
            int i2 = mesh->indices ? mesh->indices[triangleIndex * 3 + 2] : triangleIndex * 3 + 2;
            if (i0 < 0 || i1 < 0 || i2 < 0 || i0 >= mesh->vertexCount || i1 >= mesh->vertexCount || i2 >= mesh->vertexCount)
                continue;

            Vector3 a = {mesh->vertices[i0 * 3 + 0], mesh->vertices[i0 * 3 + 1], mesh->vertices[i0 * 3 + 2]};
            Vector3 b = {mesh->vertices[i1 * 3 + 0], mesh->vertices[i1 * 3 + 1], mesh->vertices[i1 * 3 + 2]};
            Vector3 c = {mesh->vertices[i2 * 3 + 0], mesh->vertices[i2 * 3 + 1], mesh->vertices[i2 * 3 + 2]};
            RasterizeTerrainTriangle(cache, Vector3Transform(a, transform), Vector3Transform(b, transform), Vector3Transform(c, transform));
        }
    }

    FillTerrainCacheGaps(cache);
    cache->valid = false;
    for (size_t i = 0; i < cache->validMask.size(); i++)
    {
        if (cache->validMask[i])
        {
            cache->valid = true;
            break;
        }
    }

    return cache->valid;
}

static TerrainCollisionCache *GetTerrainCollisionCache(int objectIndex)
{
    if (objectIndex < 0 || objectIndex >= totalObjetos)
        return nullptr;
    if (!PropertiesIsTerrain(objetos[objectIndex].id))
        return nullptr;

    TerrainCollisionCache *cache = AcquireTerrainCache(objetos[objectIndex].id);
    if (!cache)
        return nullptr;
    if (!cache->valid)
        BuildTerrainCollisionCache(objectIndex, cache);
    return cache->valid ? cache : nullptr;
}

static bool SampleTerrainGridHeight(const TerrainCollisionCache *cache, int x, int z, float *outHeight)
{
    if (!cache || !outHeight || x < 0 || z < 0 || x >= cache->width || z >= cache->depth)
        return false;

    int index = z * cache->width + x;
    if (cache->validMask[index])
    {
        *outHeight = cache->heights[index];
        return true;
    }

    for (int radius = 1; radius <= 2; radius++)
    {
        for (int dz = -radius; dz <= radius; dz++)
        {
            for (int dx = -radius; dx <= radius; dx++)
            {
                int nx = x + dx;
                int nz = z + dz;
                if (nx < 0 || nz < 0 || nx >= cache->width || nz >= cache->depth)
                    continue;
                int neighborIndex = nz * cache->width + nx;
                if (!cache->validMask[neighborIndex])
                    continue;
                *outHeight = cache->heights[neighborIndex];
                return true;
            }
        }
    }

    return false;
}

static bool SampleTerrainHeight(const TerrainCollisionCache *cache, float x, float z, float *outHeight, Vector3 *outNormal)
{
    if (!cache || !cache->valid)
        return false;
    if (x < cache->minX || x > cache->maxX || z < cache->minZ || z > cache->maxZ)
        return false;

    float fx = (x - cache->minX) / cache->stepX;
    float fz = (z - cache->minZ) / cache->stepZ;
    int x0 = Clampi((int)floorf(fx), 0, cache->width - 1);
    int z0 = Clampi((int)floorf(fz), 0, cache->depth - 1);
    int x1 = Clampi(x0 + 1, 0, cache->width - 1);
    int z1 = Clampi(z0 + 1, 0, cache->depth - 1);
    float tx = fx - (float)x0;
    float tz = fz - (float)z0;

    float h00 = 0.0f, h10 = 0.0f, h01 = 0.0f, h11 = 0.0f;
    bool v00 = SampleTerrainGridHeight(cache, x0, z0, &h00);
    bool v10 = SampleTerrainGridHeight(cache, x1, z0, &h10);
    bool v01 = SampleTerrainGridHeight(cache, x0, z1, &h01);
    bool v11 = SampleTerrainGridHeight(cache, x1, z1, &h11);
    if (!v00 && !v10 && !v01 && !v11)
        return false;

    if (!v10)
        h10 = h00;
    if (!v01)
        h01 = h00;
    if (!v11)
        h11 = v10 ? h10 : (v01 ? h01 : h00);

    float top = h00 + (h10 - h00) * tx;
    float bottom = h01 + (h11 - h01) * tx;
    if (outHeight)
        *outHeight = top + (bottom - top) * tz;

    if (outNormal)
    {
        float left = h00, right = h10, down = h00, up = h01;
        SampleTerrainGridHeight(cache, Clampi(x0 - 1, 0, cache->width - 1), z0, &left);
        SampleTerrainGridHeight(cache, Clampi(x1 + 1, 0, cache->width - 1), z0, &right);
        SampleTerrainGridHeight(cache, x0, Clampi(z0 - 1, 0, cache->depth - 1), &down);
        SampleTerrainGridHeight(cache, x0, Clampi(z1 + 1, 0, cache->depth - 1), &up);
        *outNormal = NormalizeSafe((Vector3){left - right, cache->stepX + cache->stepZ, down - up}, (Vector3){0.0f, 1.0f, 0.0f});
    }

    return true;
}

static bool QueryTerrainAtXZ(const int *terrainIndices, int terrainCount, float x, float z, float *outHeight, Vector3 *outNormal)
{
    bool hitAny = false;
    float bestHeight = -FLT_MAX;
    Vector3 bestNormal = {0.0f, 1.0f, 0.0f};

    for (int i = 0; i < terrainCount; i++)
    {
        TerrainCollisionCache *cache = GetTerrainCollisionCache(terrainIndices[i]);
        if (!cache)
            continue;

        float sampleHeight = 0.0f;
        Vector3 sampleNormal = {0.0f, 1.0f, 0.0f};
        if (!SampleTerrainHeight(cache, x, z, &sampleHeight, &sampleNormal))
            continue;
        if (!hitAny || sampleHeight > bestHeight)
        {
            hitAny = true;
            bestHeight = sampleHeight;
            bestNormal = sampleNormal;
        }
    }

    if (hitAny)
    {
        if (outHeight)
            *outHeight = bestHeight;
        if (outNormal)
            *outNormal = bestNormal;
    }

    return hitAny;
}

static void ResolveTerrainContact(DynamicBodyRef *body, const int *terrainIndices, int terrainCount, float deltaTime)
{
    if (!body || !body->hasCollider)
        return;

    float terrainHeight = 0.0f;
    Vector3 terrainNormal = {0.0f, 1.0f, 0.0f};
    if (!QueryTerrainAtXZ(terrainIndices, terrainCount, body->obb.center.x, body->obb.center.z, &terrainHeight, &terrainNormal))
        return;

    float penetration = terrainHeight - body->aabb.min.y;
    if (penetration < -0.02f)
        return;

    ObjetoCena *object = &objetos[body->objectIndex];
    if (penetration > 0.0f)
    {
        object->posicao.y += penetration;
        RefreshDynamicBodyCollision(body);
    }

    body->state->grounded = true;
    terrainNormal = NormalizeSafe(terrainNormal, (Vector3){0.0f, 1.0f, 0.0f});
    if (Vector3DotProduct(body->state->linearVelocity, terrainNormal) < 0.0f)
        body->state->linearVelocity = ProjectVectorOnPlane(body->state->linearVelocity, terrainNormal);

    if (PropertiesHasGravity(body->objectId))
    {
        Vector3 gravityVector = {0.0f, -gPhysics.settings.gravity, 0.0f};
        Vector3 tangentGravity = ProjectVectorOnPlane(gravityVector, terrainNormal);
        body->state->linearVelocity = Vector3Add(body->state->linearVelocity, Vector3Scale(tangentGravity, deltaTime));
    }

    float groundFactor = Clampf(1.0f - gPhysics.settings.groundFriction * deltaTime * 60.0f, 0.0f, 1.0f);
    body->state->linearVelocity.x *= groundFactor;
    body->state->linearVelocity.z *= groundFactor;
}

static void StepFixedSimulation(float deltaTime)
{
    DynamicBodyRef dynamicBodies[MAX_OBJETOS] = {0};
    StaticColliderRef staticColliders[MAX_OBJETOS] = {0};
    int terrainIndices[MAX_OBJETOS] = {0};
    int staticCount = 0;
    int terrainCount = 0;
    int dynamicCount = GatherSceneBodies(dynamicBodies, staticColliders, &staticCount, terrainIndices, &terrainCount);

    for (int i = 0; i < dynamicCount; i++)
    {
        DynamicBodyRef *body = &dynamicBodies[i];
        ObjetoCena *obj = &objetos[body->objectIndex];
        NanquimoriBodyState *state = body->state;

        state->grounded = false;
        state->angularVelocity = (Vector3){0};

        if (PropertiesHasGravity(body->objectId))
            state->linearVelocity.y -= gPhysics.settings.gravity * deltaTime;

        ApplyHorizontalDamping(&state->linearVelocity, gPhysics.settings.damping, deltaTime);
        obj->posicao = Vector3Add(obj->posicao, Vector3Scale(state->linearVelocity, deltaTime));
        if (body->hasCollider)
            RefreshDynamicBodyCollision(body);
    }

    for (int iteration = 0; iteration < gPhysics.settings.solverIterations; iteration++)
    {
        for (int i = 0; i < dynamicCount; i++)
            ResolveDynamicStaticContacts(&dynamicBodies[i], staticColliders, staticCount);

        for (int i = 0; i < dynamicCount; i++)
        {
            for (int j = i + 1; j < dynamicCount; j++)
                ResolveDynamicDynamicContacts(&dynamicBodies[i], &dynamicBodies[j]);
        }
    }

    if (terrainCount > 0)
    {
        for (int i = 0; i < dynamicCount; i++)
            ResolveTerrainContact(&dynamicBodies[i], terrainIndices, terrainCount, deltaTime);
    }
}

static void DrawOBB(const NanquimoriOBB *obb, Color color)
{
    if (!obb)
        return;

    Vector3 axisX = obb->axis[0];
    Vector3 axisY = obb->axis[1];
    Vector3 axisZ = obb->axis[2];
    Vector3 half = obb->half;
    Vector3 corners[8];
    int index = 0;

    for (int x = -1; x <= 1; x += 2)
    {
        for (int y = -1; y <= 1; y += 2)
        {
            for (int z = -1; z <= 1; z += 2)
            {
                Vector3 point = obb->center;
                point = Vector3Add(point, Vector3Scale(axisX, half.x * (float)x));
                point = Vector3Add(point, Vector3Scale(axisY, half.y * (float)y));
                point = Vector3Add(point, Vector3Scale(axisZ, half.z * (float)z));
                corners[index++] = point;
            }
        }
    }

    int edges[12][2] = {
        {0, 1}, {0, 2}, {0, 4}, {1, 3},
        {1, 5}, {2, 3}, {2, 6}, {3, 7},
        {4, 5}, {4, 6}, {5, 7}, {6, 7}};

    for (int i = 0; i < 12; i++)
        DrawLine3D(corners[edges[i][0]], corners[edges[i][1]], color);
}

static void DrawTerrainCollisionCache(const TerrainCollisionCache *cache, Color color)
{
    if (!cache || !cache->valid || cache->width <= 0 || cache->depth <= 0)
        return;

    const float lift = 0.03f;
    bool drewAny = false;

    for (int z = 0; z < cache->depth; z++)
    {
        float worldZ = cache->minZ + cache->stepZ * (float)z;
        for (int x = 0; x < cache->width - 1; x++)
        {
            int indexA = z * cache->width + x;
            int indexB = indexA + 1;
            if (!cache->validMask[indexA] || !cache->validMask[indexB])
                continue;

            Vector3 a = {
                cache->minX + cache->stepX * (float)x,
                cache->heights[indexA] + lift,
                worldZ};
            Vector3 b = {
                cache->minX + cache->stepX * (float)(x + 1),
                cache->heights[indexB] + lift,
                worldZ};
            DrawLine3D(a, b, color);
            drewAny = true;
        }
    }

    for (int x = 0; x < cache->width; x++)
    {
        float worldX = cache->minX + cache->stepX * (float)x;
        for (int z = 0; z < cache->depth - 1; z++)
        {
            int indexA = z * cache->width + x;
            int indexB = (z + 1) * cache->width + x;
            if (!cache->validMask[indexA] || !cache->validMask[indexB])
                continue;

            Vector3 a = {
                worldX,
                cache->heights[indexA] + lift,
                cache->minZ + cache->stepZ * (float)z};
            Vector3 b = {
                worldX,
                cache->heights[indexB] + lift,
                cache->minZ + cache->stepZ * (float)(z + 1)};
            DrawLine3D(a, b, color);
            drewAny = true;
        }
    }

    if (!drewAny)
        DrawBoundingBox(cache->bounds, color);
}

void InitNanquimoriPhysics(void)
{
    gPhysics.initialized = true;
    gPhysics.settings.gravity = 9.8f;
    gPhysics.settings.restitution = 0.08f;
    gPhysics.settings.friction = 0.28f;
    gPhysics.settings.damping = 0.02f;
    gPhysics.settings.groundFriction = 0.05f;
    gPhysics.settings.fixedDelta = 1.0f / 60.0f;
    gPhysics.settings.maxFrameDelta = 1.0f / 30.0f;
    gPhysics.settings.maxSubSteps = 2;
    gPhysics.settings.solverIterations = 1;
    ResetNanquimoriPhysicsWorld();
}

void ShutdownNanquimoriPhysics(void)
{
    ResetNanquimoriPhysicsWorld();
    gPhysics.initialized = false;
}

void ResetNanquimoriPhysicsWorld(void)
{
    for (int i = 0; i < MAX_OBJETOS; i++)
        gPhysics.bodies[i] = (NanquimoriBodyState){0};

    ClearTerrainCaches();
    gPhysics.accumulator = 0.0f;
    gPhysics.profileMs = 0.0f;
}

void StepNanquimoriPhysics(float frameDelta)
{
    if (!gPhysics.initialized)
        InitNanquimoriPhysics();

    frameDelta = Clampf(frameDelta, 0.0f, gPhysics.settings.maxFrameDelta);
    if (frameDelta <= 0.0f)
    {
        gPhysics.profileMs = 0.0f;
        return;
    }

    gPhysics.accumulator += frameDelta;
    double start = GetTime();
    int subSteps = 0;

    while (gPhysics.accumulator >= gPhysics.settings.fixedDelta && subSteps < gPhysics.settings.maxSubSteps)
    {
        StepFixedSimulation(gPhysics.settings.fixedDelta);
        gPhysics.accumulator -= gPhysics.settings.fixedDelta;
        subSteps++;
    }

    if (subSteps == gPhysics.settings.maxSubSteps && gPhysics.accumulator > gPhysics.settings.fixedDelta)
        gPhysics.accumulator = fmodf(gPhysics.accumulator, gPhysics.settings.fixedDelta);

    gPhysics.profileMs = (subSteps > 0) ? (float)((GetTime() - start) * 1000.0) : 0.0f;
}

void DrawNanquimoriPhysicsDebug(void)
{
    int selectedId = ObterObjetoSelecionadoId();

    for (int i = 0; i < totalObjetos; i++)
    {
        ObjetoCena *obj = &objetos[i];
        if (!obj->ativo || !PropertiesHasCollider(obj->id))
            continue;

        bool selected = (selectedId > 0 && selectedId == obj->id);
        Color color = selected ? (Color){184, 38, 36, 255}
                               : (PropertiesIsStatic(obj->id) ? (Color){108, 108, 108, 255}
                                                              : (Color){130, 46, 42, 255});

        if (PropertiesIsTerrain(obj->id))
        {
            TerrainCollisionCache *cache = GetTerrainCollisionCache(i);
            if (cache)
                DrawTerrainCollisionCache(cache, color);
            continue;
        }

        NanquimoriOBB obb = {0};
        if (TryGetObjectCollisionOBBByIndex(i, &obb))
            DrawOBB(&obb, color);
    }
}

float GetNanquimoriPhysicsProfileMs(void)
{
    return gPhysics.profileMs;
}
