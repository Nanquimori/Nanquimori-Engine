#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include "outliner.h"
#include "raylib.h"

#define MAX_SCENES 16
#define MAX_RECENT_PROJECTS 3

void InitSceneManager(void);
int GetSceneCount(void);
int GetActiveSceneIndex(void);
const char *GetActiveSceneName(void);
const char *GetSceneName(int index);
int GetSceneObjectCount(int index);
void CreateNewScene(void);
void SwitchScene(int index);
void DuplicateActiveScene(void);
void RenameActiveScene(const char *nome);
void DeleteScene(int index);
bool MoveObjetoParaCena(int objetoId, int cenaIndex);
const char *GetProjectPath(void);
const char *GetProjectDir(void);
void SetProjectPath(const char *path);
bool SaveProject(void);
bool LoadProject(const char *path);
bool SaveProjectAs(const char *name);
bool OpenProject(const char *path);
void ReloadActiveScene(void);
void SaveActiveSceneSnapshot(void);
int GetRecentProjectCount(void);
const char *GetRecentProjectPath(int index);
bool ConsumePendingProjectIconPath(char *out, int outSize);
void SetProjectCameraState(Vector3 position, Vector3 target);
bool ConsumeLoadedProjectCameraState(Vector3 *position, Vector3 *target);

#endif // SCENE_MANAGER_H

