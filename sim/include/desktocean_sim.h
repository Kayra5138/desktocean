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
    uint32_t sheltered; // non-zero while in (or partly in) a cave: draw behind the cave's front
} DcFish;

// Bottom creatures (crabs, snails, starfish, shrimp)
#define DC_GAIT_CRAB   0u
#define DC_GAIT_SNAIL  1u
#define DC_GAIT_STAR   2u
#define DC_GAIT_WALKER 3u

typedef struct DcCrawlerSpec {
    uint64_t id;
    uint32_t sprite;
    float aspect;      // sprite height / width
    float scale;
    uint32_t gait;     // DC_GAIT_*
} DcCrawlerSpec;

typedef struct DcCrawler {
    uint64_t id;
    float x, y;        // middle of the bottom edge, scene pixels
    float w, h;
    float facing;      // -1 left .. 1 right (crabs: always 1)
    float phase;       // leg cycle
    float tilt;        // degrees, follows the ground
    float buried;      // unused, always 0
    uint32_t sprite;
    uint32_t gait;
    float hop;         // lifted off the ground while hopping in play, scene pixels
} DcCrawler;

// A fish swam out through a side edge, toward the screen next to it
typedef struct DcDeparture {
    uint64_t id;
    int32_t side;      // -1 left edge, 1 right edge
    float y;           // scene pixels
} DcDeparture;

// How much a fish trusts you (petting, eating food you dropped)
typedef struct DcTrust {
    uint64_t id;
    float trust;       // 0 .. 1
} DcTrust;

// A clutch of eggs; hatching makes a DcBirth at (x, y)
typedef struct DcEgg {
    uint64_t parent_a, parent_b;
    float x, y;
    uint32_t count;    // babies inside
    float progress;    // 0 just laid .. 1 hatching
} DcEgg;

// A cave opening, scene pixels (top-left corner and size)
typedef struct DcShelter {
    float x, y, w, h;
} DcShelter;

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
    float mouth_x;     // mouth on the drawing, fraction of width (head right); < 0 = default
    float mouth_y;     // ... fraction of height
    uint32_t body;     // DC_BODY_*: how it swims
} DcFishSpec;

#define DC_BODY_FISH     0u
#define DC_BODY_JELLY    1u
#define DC_BODY_SEAHORSE 2u
#define DC_BODY_RAY      3u
#define DC_BODY_TURTLE   4u

// Two fish had a baby at (x, y)
typedef struct DcBirth {
    uint64_t parent_a, parent_b;
    float x, y;
    uint32_t count;    // babies in this litter (small fish have several)
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
    float size;        // diameter, scene pixels (one pellet size per fish size)
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
void dc_sim_set_shelters(DcSim *sim, const DcShelter *list, uint32_t count);  // replaces the caves
void dc_sim_set_exits(DcSim *sim, uint32_t left, uint32_t right);  // side edges with a screen next to them
uint32_t dc_sim_take_departures(DcSim *sim, DcDeparture *out, uint32_t cap);  // drains them
bool dc_sim_arrive(DcSim *sim, uint64_t id, int32_t side, float y);  // swim in through a side edge
void dc_sim_carry_to(DcSim *sim, float x, float y);   // bubble wand: the bubble is here
void dc_sim_carry_end(DcSim *sim);                     // let go: it pops, the fish swims out
void dc_sim_carry_release(DcSim *sim);                 // let go: drifts on, pops at a side, rises when idle
void dc_sim_carry_rise(DcSim *sim);                    // left alone: floats up and pops at the surface
bool dc_sim_carry_bubble(DcSim *sim, DcBubble *out);   // alpha = how close a fish is to coming in
uint32_t dc_sim_hearts(DcSim *sim, DcBubble *out, uint32_t cap);   // radius = half size
uint32_t dc_sim_trust(DcSim *sim, DcTrust *out, uint32_t cap);      // returns total count
void dc_sim_set_trust(DcSim *sim, const DcTrust *list, uint32_t count);
uint32_t dc_sim_eggs(DcSim *sim, DcEgg *out, uint32_t cap);          // returns total count
void dc_sim_set_eggs(DcSim *sim, const DcEgg *list, uint32_t count);  // restore saved eggs
void dc_sim_set_nests(DcSim *sim, const float *xy, uint32_t count);   // count (x, y) pairs on plants
void dc_sim_set_crawlers(DcSim *sim, const DcCrawlerSpec *list, uint32_t count);
uint32_t dc_sim_crawlers(DcSim *sim, DcCrawler *out, uint32_t cap);  // returns total count
void dc_sim_set_ground(DcSim *sim, const float *heights, uint32_t count, float step);  // scene y per step px
void dc_sim_set_life_speed(DcSim *sim, float speed);                  // 0 = no babies
uint32_t dc_sim_relations(DcSim *sim, DcRelation *out, uint32_t cap); // returns total count
void dc_sim_set_relations(DcSim *sim, const DcRelation *list, uint32_t count);
void dc_sim_step(DcSim *sim, float dt, float pointer_x, float pointer_y); // < 0: no pointer

uint32_t dc_sim_fish(DcSim *sim, DcFish *out, uint32_t cap); // returns total count
uint32_t dc_sim_food(DcSim *sim, DcFood *out, uint32_t cap);

#ifdef __cplusplus
}
#endif
