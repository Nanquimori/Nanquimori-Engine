#ifndef SCENE_CAMERA_H
#define SCENE_CAMERA_H

#include "outliner.h"
#include "raylib.h"

void ConfigureObjetoComoCamera(ObjetoCena *obj);
bool ObjetoEhCamera(const ObjetoCena *obj);
int AddCameraObjectFromView(const Camera *viewCamera);
void SetSceneRenderCameraObjectId(int objetoId);
int GetSceneRenderCameraObjectId(void);
bool BuildSceneCameraFromObject(const ObjetoCena *obj, Camera *out);
bool CopySceneObjectFromCameraView(ObjetoCena *obj, const Camera *camera);
bool GetSceneRenderCamera(Camera *out, int *objectId);
void DrawSceneCameraHelpers(Camera viewCamera);
bool RaycastSceneCameraHelpers(Ray ray, Vector3 *hitPos, float *hitDist, int *hitObjectId);

#endif // SCENE_CAMERA_H
