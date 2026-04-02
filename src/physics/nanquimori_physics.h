#ifndef NANQUIMORI_PHYSICS_H
#define NANQUIMORI_PHYSICS_H

#include "raylib.h"

void InitNanquimoriPhysics(void);
void ShutdownNanquimoriPhysics(void);
void ResetNanquimoriPhysicsWorld(void);
void StepNanquimoriPhysics(float frameDelta);
void DrawNanquimoriPhysicsDebug(void);
float GetNanquimoriPhysicsProfileMs(void);

#endif // NANQUIMORI_PHYSICS_H
