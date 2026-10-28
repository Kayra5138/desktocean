// C interface of the Rust simulation core (sim/src/ffi.rs). Keep in sync.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DcSim DcSim;

typedef struct DcFish {
    float x, y;        // centre, scene pixels
    float w, h;
    float facing;      // -1 left .. 1 right
    float phase;       // tail-wave phase, radians
    float amplitude;   // tail swing, fraction of h
    float tilt;        // degrees
    float depth;       // 0 front .. 1 back
    uint32_t sprite;   // from DcFishSpec
} DcFish;

typedef struct DcFishSpec {
    uint64_t id;       // stable per fish; kept fish keep position and personality
    uint32_t sprite;   // host's sprite index, passed through to DcFish
    float aspect;      // sprite height / width
    float scale;       // size multiplier
} DcFishSpec;

typedef struct DcFood {
    float x, y;
    float opacity;
} DcFood;

const char *dc_sim_version(void);

DcSim *dc_sim_new(uint64_t seed);
void dc_sim_free(DcSim *sim);
bool dc_sim_ok(const DcSim *sim);

void dc_sim_resize(DcSim *sim, float width, float height);
// New fish drop in through the surface when animate_new is set
void dc_sim_set_population(DcSim *sim, const DcFishSpec *specs, uint32_t count, bool animate_new);
void dc_sim_feed(DcSim *sim, float x);                       // x < 0: random spot
void dc_sim_clear_food(DcSim *sim);
bool dc_sim_lure_active(DcSim *sim);                         // curious fish following the cursor
void dc_sim_step(DcSim *sim, float dt, float pointer_x, float pointer_y); // < 0: no pointer

uint32_t dc_sim_fish(DcSim *sim, DcFish *out, uint32_t cap); // returns total count
uint32_t dc_sim_food(DcSim *sim, DcFood *out, uint32_t cap);

#ifdef __cplusplus
}
#endif
