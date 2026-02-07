#include "pch.h"
#include <math.h>
#include "mathfunc.h"

float expDecay(float a, float b, float decay, float dt) {
    return b + (a - b) * expf(-decay * dt);
}
