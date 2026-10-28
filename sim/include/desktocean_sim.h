// C interface of the Rust simulation core (sim/src/ffi.rs). Keep in sync.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DcSim DcSim;

typedef struct DcFish {
    uint64_t id;       // from DcFishSpec
    float x, y;        // centre, scene pixels
    float w, h;
    float facing;      // -1 left .. 1 right
    float phase;       // tail-wave phase, radians
    float amplitude;   // tail swing, fraction of h
    float tilt;        // degrees
    float depth;       // 0 front .. 1 back
    uint32_t sprite;   // from DcFishSpec
    float hunger;      // 0 full .. 1 starving
    uint32_t character; // DC_CHARACTER_*
} DcFish;

// What kind of fish it is at heart
#define DC_CHARACTER_EXPLORER 0u
#define DC_CHARACTER_HOMEBODY 1u
#define DC_CHARACTER_BOTTOM   2u
#define DC_CHARACTER_SKIMMER  3u
#define DC_CHARACTER_LAZY     4u
#define DC_CHARACTER_PLAYFUL  5u
#define DC_CHARACTER_SHY      6u

// Affinity between two fish (ids from DcFishSpec), -1..1
typedef struct DcRelation {
    uint64_t a, b;
    float affinity;
} DcRelation;

#define DC_FRIEND 0.5f    // affinity above this: friends
#define DC_RIVAL (-0.4f)  // affinity below this: rivals

typedef struct DcFishSpec {
    uint64_t id;       // stable per fish; kept fish keep position and personality
    uint32_t sprite;   // host's sprite index, passed through to DcFish
    float aspect;      // sprite height / width
    float scale;       // size multiplier
    uint32_t adult;    // non-zero once grown up; only adults have babies
} DcFishSpec;

// Two fish had a baby at (x, y)
typedef struct DcBirth {
    uint64_t parent_a, parent_b;
    float x, y;
} DcBirth;

typedef struct DcBubble {
    float x, y;
    float radius;
    float alpha;
} DcBubble;

typedef struct DcRipple {
    float x, y;        // centre, on the surface
    float progress;    // 0 .. 1 over its life
    float strength;    // radius of the bubble that made it
} DcRipple;

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
void dc_sim_set_daylight(DcSim *sim, float daylight);        // 0 night .. 1 day
bool dc_sim_announce(DcSim *sim);                            // a fish swims up and blows bubbles
uint32_t dc_sim_bubbles(DcSim *sim, DcBubble *out, uint32_t cap);
uint32_t dc_sim_ripples(DcSim *sim, DcRipple *out, uint32_t cap);
uint32_t dc_sim_take_births(DcSim *sim, DcBirth *out, uint32_t cap);  // drains them
bool dc_sim_place_fish(DcSim *sim, uint64_t id, float x, float y);
void dc_sim_set_life_speed(DcSim *sim, float speed);                  // 0 = no babies
uint32_t dc_sim_relations(DcSim *sim, DcRelation *out, uint32_t cap); // returns total count
void dc_sim_set_relations(DcSim *sim, const DcRelation *list, uint32_t count);
void dc_sim_step(DcSim *sim, float dt, float pointer_x, float pointer_y); // < 0: no pointer

uint32_t dc_sim_fish(DcSim *sim, DcFish *out, uint32_t cap); // returns total count
uint32_t dc_sim_food(DcSim *sim, DcFood *out, uint32_t cap);

#ifdef __cplusplus
}
#endif
