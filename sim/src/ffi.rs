//! C ABI used by the Qt plugin (see `include/desktocean_sim.h`).
//!
//! The library runs inside plasmashell, so a panic must never unwind into C++:
//! every entry point catches it, marks the handle poisoned and returns a
//! neutral value. A poisoned simulation simply stops; the desktop survives.

use std::ffi::c_char;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::ptr;

use crate::sim::{FishSpec, Sim};

pub struct DcSim {
    sim: Sim,
    poisoned: bool,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcFish {
    /// From DcFishSpec
    pub id: u64,
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub facing: f32,
    pub phase: f32,
    pub amplitude: f32,
    pub tilt: f32,
    pub depth: f32,
    pub sprite: u32,
    pub hunger: f32,
    /// Character::index(): 0 explorer, 1 homebody, 2 bottom, 3 skimmer, 4 lazy, 5 playful, 6 shy
    pub character: u32,
    /// Non-zero while in (or partly in) a cave: draw it behind the cave's front
    pub sheltered: u32,
}

/// A cave opening in scene pixels (top-left corner and size)
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcShelter {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcFishSpec {
    pub id: u64,
    pub sprite: u32,
    pub aspect: f32,
    pub scale: f32,
    /// Non-zero for grown-up fish
    pub adult: u32,
    /// Mouth on the drawing as fractions of width and height; negative = default
    pub mouth_x: f32,
    pub mouth_y: f32,
    /// 0 fish, 1 jellyfish, 2 seahorse, 3 ray, 4 turtle
    pub body: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcBirth {
    pub parent_a: u64,
    pub parent_b: u64,
    pub x: f32,
    pub y: f32,
    /// Babies in this litter
    pub count: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcBubble {
    pub x: f32,
    pub y: f32,
    pub radius: f32,
    pub alpha: f32,
}

/// A ring on the surface; `progress` runs 0..1 over its life.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcRipple {
    pub x: f32,
    pub y: f32,
    pub progress: f32,
    pub strength: f32,
}

/// Affinity between fish `a` and `b` (ids from DcFishSpec), -1..1.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcRelation {
    pub a: u64,
    pub b: u64,
    pub affinity: f32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcFood {
    pub x: f32,
    pub y: f32,
    pub opacity: f32,
    /// Diameter in scene pixels (one size of pellet per fish size)
    pub size: f32,
}

fn with_sim<R>(handle: *mut DcSim, fallback: R, f: impl FnOnce(&mut Sim) -> R) -> R {
    // SAFETY: the plugin only passes handles returned by dc_sim_new and not yet freed.
    let Some(h) = (unsafe { handle.as_mut() }) else {
        return fallback;
    };
    if h.poisoned {
        return fallback;
    }
    match catch_unwind(AssertUnwindSafe(|| f(&mut h.sim))) {
        Ok(r) => r,
        Err(_) => {
            h.poisoned = true;
            fallback
        }
    }
}

fn optional_point(x: f32, y: f32) -> Option<(f32, f32)> {
    (x >= 0.0 && y >= 0.0 && x.is_finite() && y.is_finite()).then_some((x, y))
}

#[unsafe(no_mangle)]
pub extern "C" fn dc_sim_version() -> *const c_char {
    c"0.1.0".as_ptr()
}

#[unsafe(no_mangle)]
pub extern "C" fn dc_sim_new(seed: u64) -> *mut DcSim {
    catch_unwind(|| {
        Box::into_raw(Box::new(DcSim {
            sim: Sim::new(seed),
            poisoned: false,
        }))
    })
    .unwrap_or(ptr::null_mut())
}

/// # Safety
/// `handle` must come from `dc_sim_new` and must not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_free(handle: *mut DcSim) {
    if !handle.is_null() {
        // SAFETY: guaranteed by the caller contract above.
        let _ = catch_unwind(AssertUnwindSafe(|| drop(unsafe { Box::from_raw(handle) })));
    }
}

/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_ok(handle: *const DcSim) -> bool {
    // SAFETY: see with_sim.
    unsafe { handle.as_ref() }.is_some_and(|h| !h.poisoned)
}

/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_resize(handle: *mut DcSim, width: f32, height: f32) {
    with_sim(handle, (), |s| s.resize(width, height));
}

/// Replaces the set of fish; see `Sim::set_population`.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `specs` null
/// or valid for `count` reads.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_population(
    handle: *mut DcSim,
    specs: *const DcFishSpec,
    count: u32,
    animate_new: bool,
) {
    with_sim(handle, (), |s| {
        let specs: Vec<FishSpec> = if specs.is_null() || count == 0 {
            Vec::new()
        } else {
            // SAFETY: caller provides `count` readable elements.
            unsafe { std::slice::from_raw_parts(specs, count.min(1000) as usize) }
                .iter()
                .map(|d| FishSpec {
                    id: d.id,
                    sprite: d.sprite,
                    aspect: d.aspect,
                    scale: d.scale,
                    adult: d.adult != 0,
                    mouth: if d.mouth_x >= 0.0 && d.mouth_y >= 0.0 {
                        (d.mouth_x, d.mouth_y)
                    } else {
                        crate::sim::DEFAULT_MOUTH
                    },
                    body: crate::sim::Body::from_index(d.body),
                })
                .collect()
        };
        s.set_population(&specs, animate_new)
    });
}

/// `x < 0` drops the food somewhere random.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_feed(handle: *mut DcSim, x: f32) {
    with_sim(handle, (), |s| {
        s.feed((x >= 0.0 && x.is_finite()).then_some(x))
    });
}

/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_clear_food(handle: *mut DcSim) {
    with_sim(handle, (), |s| s.clear_food());
}

/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_lure_active(handle: *mut DcSim) -> bool {
    with_sim(handle, false, |s| s.lure_active())
}

/// Sends a fish up to blow bubbles; false if no fish is free.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_announce(handle: *mut DcSim) -> bool {
    with_sim(handle, false, |s| s.announce())
}

/// Same contract as `dc_sim_fish`, for bubbles.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_bubbles(handle: *mut DcSim, out: *mut DcBubble, cap: u32) -> u32 {
    with_sim(handle, 0, |s| {
        let water = s.water_level();
        let bubbles = s.bubbles();
        if !out.is_null() {
            // SAFETY: caller provides room for `cap` elements.
            let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
            for (d, b) in dst.iter_mut().zip(bubbles) {
                *d = DcBubble {
                    x: b.x,
                    y: b.y,
                    radius: b.radius,
                    alpha: b.alpha(water),
                };
            }
        }
        bubbles.len() as u32
    })
}

/// Same contract as `dc_sim_fish`, for surface ripples.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_ripples(handle: *mut DcSim, out: *mut DcRipple, cap: u32) -> u32 {
    with_sim(handle, 0, |s| {
        let ripples = s.ripples();
        if !out.is_null() {
            // SAFETY: caller provides room for `cap` elements.
            let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
            for (d, r) in dst.iter_mut().zip(ripples) {
                *d = DcRipple {
                    x: r.x,
                    y: r.y,
                    progress: r.age / crate::sim::RIPPLE_SECS,
                    strength: r.strength,
                };
            }
        }
        ripples.len() as u32
    })
}

/// Moves babies born since the last call into `out` (up to `cap`) and returns
/// how many were copied; the rest are dropped.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_take_births(
    handle: *mut DcSim,
    out: *mut DcBirth,
    cap: u32,
) -> u32 {
    with_sim(handle, 0, |s| {
        let births = s.take_births();
        if out.is_null() {
            return 0;
        }
        // SAFETY: caller provides room for `cap` elements.
        let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
        let mut n = 0;
        for (d, b) in dst.iter_mut().zip(&births) {
            *d = DcBirth {
                parent_a: b.parent_a,
                parent_b: b.parent_b,
                x: b.x,
                y: b.y,
                count: b.count,
            };
            n += 1;
        }
        n
    })
}

/// Puts fish `id` at (x, y) right away; false if there is no such fish.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_place_fish(handle: *mut DcSim, id: u64, x: f32, y: f32) -> bool {
    with_sim(handle, false, |s| s.place_fish(id, x, y))
}

/// 1 = normal, higher speeds up babies for testing, 0 = no babies.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_life_speed(handle: *mut DcSim, speed: f32) {
    with_sim(handle, (), |s| s.set_life_speed(speed));
}

/// 0 = night, 1 = day.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_daylight(handle: *mut DcSim, daylight: f32) {
    with_sim(handle, (), |s| s.set_daylight(daylight));
}

/// Copies up to `cap` relations into `out` and returns how many exist.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_relations(
    handle: *mut DcSim,
    out: *mut DcRelation,
    cap: u32,
) -> u32 {
    with_sim(handle, 0, |s| {
        let list = s.relations();
        if !out.is_null() {
            // SAFETY: caller provides room for `cap` elements.
            let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
            for (d, &(a, b, affinity)) in dst.iter_mut().zip(&list) {
                *d = DcRelation { a, b, affinity };
            }
        }
        list.len() as u32
    })
}

/// Restores relations saved earlier with `dc_sim_relations`.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `list` null or valid for `count` reads.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_relations(
    handle: *mut DcSim,
    list: *const DcRelation,
    count: u32,
) {
    with_sim(handle, (), |s| {
        let items: Vec<(u64, u64, f32)> = if list.is_null() || count == 0 {
            Vec::new()
        } else {
            // SAFETY: caller provides `count` readable elements.
            unsafe { std::slice::from_raw_parts(list, count.min(100_000) as usize) }
                .iter()
                .map(|r| (r.a, r.b, r.affinity))
                .collect()
        };
        s.set_relations(&items)
    });
}

/// A bottom creature to keep in the tank
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcCrawlerSpec {
    pub id: u64,
    pub sprite: u32,
    pub aspect: f32,
    pub scale: f32,
    /// 0 crab, 1 snail, 2 starfish, 3 walker (shrimp)
    pub gait: u32,
}

/// A bottom creature right now
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcCrawler {
    pub id: u64,
    /// Middle of the bottom edge, scene pixels
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub facing: f32,
    pub phase: f32,
    pub tilt: f32,
    /// Unused, always 0
    pub buried: f32,
    pub sprite: u32,
    pub gait: u32,
    /// Lifted off the ground while hopping in play, scene pixels
    pub hop: f32,
}

/// Replaces the bottom creatures (same ids keep their place).
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `list` null or valid for `count` reads.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_crawlers(
    handle: *mut DcSim,
    list: *const DcCrawlerSpec,
    count: u32,
) {
    with_sim(handle, (), |s| {
        let items: Vec<crate::sim::CrawlerSpec> = if list.is_null() || count == 0 {
            Vec::new()
        } else {
            // SAFETY: caller provides `count` readable elements.
            unsafe { std::slice::from_raw_parts(list, count.min(1000) as usize) }
                .iter()
                .filter(|c| {
                    c.aspect.is_finite() && c.aspect > 0.0 && c.scale.is_finite() && c.scale > 0.0
                })
                .map(|c| crate::sim::CrawlerSpec {
                    id: c.id,
                    sprite: c.sprite,
                    aspect: c.aspect,
                    scale: c.scale,
                    gait: crate::sim::Gait::from_index(c.gait),
                })
                .collect()
        };
        s.set_crawlers(&items)
    });
}

/// Same contract as `dc_sim_fish`, for bottom creatures.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_crawlers(handle: *mut DcSim, out: *mut DcCrawler, cap: u32) -> u32 {
    with_sim(handle, 0, |s| {
        let list = s.crawlers();
        if !out.is_null() {
            // SAFETY: caller provides room for `cap` elements.
            let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
            for (d, c) in dst.iter_mut().zip(list) {
                *d = DcCrawler {
                    id: c.id,
                    x: c.x,
                    y: c.y,
                    w: c.w,
                    h: c.h,
                    facing: c.facing,
                    phase: c.phase,
                    tilt: c.tilt,
                    buried: c.buried,
                    sprite: c.sprite,
                    gait: c.gait.index(),
                    hop: c.hop,
                };
            }
        }
        list.len() as u32
    })
}

/// Height of the ground (scene y) every `step` px from x = 0, for crawlers
/// climbing rocks. `count` 0 = flat sand.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `heights` null or valid for `count` reads.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_ground(
    handle: *mut DcSim,
    heights: *const f32,
    count: u32,
    step: f32,
) {
    with_sim(handle, (), |s| {
        let list: Vec<f32> = if heights.is_null() || count == 0 || !step.is_finite() {
            Vec::new()
        } else {
            // SAFETY: caller provides `count` readable elements.
            unsafe { std::slice::from_raw_parts(heights, count.min(100_000) as usize) }
                .iter()
                .map(|h| if h.is_finite() { *h } else { f32::INFINITY })
                .collect()
        };
        s.set_ground(&list, step)
    });
}

/// Hearts floating up from petted fish (same layout as a bubble: centre,
/// half size, opacity).
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_hearts(handle: *mut DcSim, out: *mut DcBubble, cap: u32) -> u32 {
    with_sim(handle, 0, |s| {
        let list = s.hearts();
        if !out.is_null() {
            // SAFETY: caller provides room for `cap` elements.
            let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
            for (d, h) in dst.iter_mut().zip(list) {
                *d = DcBubble {
                    x: h.x,
                    y: h.y,
                    radius: h.size * 0.5,
                    alpha: h.alpha(),
                };
            }
        }
        list.len() as u32
    })
}

/// How much a fish trusts the person at the computer
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcTrust {
    pub id: u64,
    pub trust: f32,
}

/// Trust of every fish, for saving. Same contract as `dc_sim_fish`.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_trust(handle: *mut DcSim, out: *mut DcTrust, cap: u32) -> u32 {
    with_sim(handle, 0, |s| {
        let list = s.trust_list();
        if !out.is_null() {
            // SAFETY: caller provides room for `cap` elements.
            let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
            for (d, (id, trust)) in dst.iter_mut().zip(&list) {
                *d = DcTrust {
                    id: *id,
                    trust: *trust,
                };
            }
        }
        list.len() as u32
    })
}

/// Restores saved trust.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `list` null or valid for `count` reads.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_trust(handle: *mut DcSim, list: *const DcTrust, count: u32) {
    with_sim(handle, (), |s| {
        let items: Vec<(u64, f32)> = if list.is_null() || count == 0 {
            Vec::new()
        } else {
            // SAFETY: caller provides `count` readable elements.
            unsafe { std::slice::from_raw_parts(list, count.min(10_000) as usize) }
                .iter()
                .map(|t| (t.id, t.trust))
                .collect()
        };
        s.set_trust(&items)
    });
}

/// Which side edges lead to another screen (non-zero = open).
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_exits(handle: *mut DcSim, left: u32, right: u32) {
    with_sim(handle, (), |s| s.set_exits(left != 0, right != 0));
}

/// A fish swam out through a side edge
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcDeparture {
    pub id: u64,
    /// -1 left edge, 1 right edge
    pub side: i32,
    pub y: f32,
}

/// Moves fish that left since the last call into `out` (up to `cap`); the
/// fish are gone from this tank's population.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_take_departures(
    handle: *mut DcSim,
    out: *mut DcDeparture,
    cap: u32,
) -> u32 {
    with_sim(handle, 0, |s| {
        let list = s.take_departures();
        if out.is_null() {
            return 0;
        }
        // SAFETY: caller provides room for `cap` elements.
        let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
        let mut n = 0;
        for (d, g) in dst.iter_mut().zip(&list) {
            *d = DcDeparture {
                id: g.id,
                side: g.side,
                y: g.y,
            };
            n += 1;
        }
        n
    })
}

/// A fish already in the population comes in through a side edge at `y`.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_arrive(handle: *mut DcSim, id: u64, side: i32, y: f32) -> bool {
    with_sim(handle, false, |s| y.is_finite() && s.arrive(id, side, y))
}

/// The bubble wand: the bubble is at (x, y) (scene pixels); starts one if needed.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_carry_to(handle: *mut DcSim, x: f32, y: f32) {
    with_sim(handle, (), |s| {
        if x.is_finite() && y.is_finite() {
            s.carry_to(x, y)
        }
    });
}

/// Let go of the bubble: it pops and the fish inside swims out.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_carry_end(handle: *mut DcSim) {
    with_sim(handle, (), |s| s.carry_end());
}

/// The bubble to draw, if one is out (alpha: 0..1, how close a fish is to coming in).
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for one write.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_carry_bubble(handle: *mut DcSim, out: *mut DcBubble) -> bool {
    with_sim(handle, false, |s| {
        let (Some((x, y, radius)), Some(c)) = (s.carry_bubble(), s.carry()) else {
            return false;
        };
        let progress = c.invite_progress();
        // SAFETY: caller provides room for one element.
        if let Some(o) = unsafe { out.as_mut() } {
            *o = DcBubble {
                x,
                y,
                radius,
                alpha: progress,
            };
        }
        true
    })
}

/// A clutch of eggs
#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcEgg {
    pub parent_a: u64,
    pub parent_b: u64,
    pub x: f32,
    pub y: f32,
    pub count: u32,
    /// 0 just laid .. 1 hatching
    pub progress: f32,
}

/// Same contract as `dc_sim_fish`, for eggs.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_eggs(handle: *mut DcSim, out: *mut DcEgg, cap: u32) -> u32 {
    with_sim(handle, 0, |s| {
        let list = s.eggs();
        if !out.is_null() {
            // SAFETY: caller provides room for `cap` elements.
            let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
            for (d, e) in dst.iter_mut().zip(list) {
                *d = DcEgg {
                    parent_a: e.parent_a,
                    parent_b: e.parent_b,
                    x: e.x,
                    y: e.y,
                    count: e.count,
                    progress: e.progress,
                };
            }
        }
        list.len() as u32
    })
}

/// Restores eggs saved from an earlier run.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `list` null or valid for `count` reads.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_eggs(handle: *mut DcSim, list: *const DcEgg, count: u32) {
    with_sim(handle, (), |s| {
        let items: Vec<crate::sim::Egg> = if list.is_null() || count == 0 {
            Vec::new()
        } else {
            // SAFETY: caller provides `count` readable elements.
            unsafe { std::slice::from_raw_parts(list, count.min(1000) as usize) }
                .iter()
                .map(|e| crate::sim::Egg {
                    parent_a: e.parent_a,
                    parent_b: e.parent_b,
                    x: e.x,
                    y: e.y,
                    count: e.count,
                    progress: e.progress,
                })
                .collect()
        };
        s.set_eggs(&items)
    });
}

/// Spots on plants where eggs are laid: `count` (x, y) pairs, scene pixels.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `xy` null or valid for `2 * count` reads.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_nests(handle: *mut DcSim, xy: *const f32, count: u32) {
    with_sim(handle, (), |s| {
        let items: Vec<(f32, f32)> = if xy.is_null() || count == 0 {
            Vec::new()
        } else {
            // SAFETY: caller provides `2 * count` readable floats.
            unsafe { std::slice::from_raw_parts(xy, 2 * count.min(1000) as usize) }
                .as_chunks::<2>()
                .0
                .iter()
                .map(|p| (p[0], p[1]))
                .collect()
        };
        s.set_nests(&items)
    });
}

/// Replaces the caves fish can swim into.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `list` null or valid for `count` reads.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_shelters(
    handle: *mut DcSim,
    list: *const DcShelter,
    count: u32,
) {
    with_sim(handle, (), |s| {
        let items: Vec<crate::sim::Shelter> = if list.is_null() || count == 0 {
            Vec::new()
        } else {
            // SAFETY: caller provides `count` readable elements.
            unsafe { std::slice::from_raw_parts(list, count.min(1000) as usize) }
                .iter()
                .filter(|r| {
                    [r.x, r.y, r.w, r.h].iter().all(|v| v.is_finite()) && r.w > 0.0 && r.h > 0.0
                })
                .map(|r| crate::sim::Shelter {
                    x: r.x,
                    y: r.y,
                    w: r.w,
                    h: r.h,
                })
                .collect()
        };
        s.set_shelters(&items)
    });
}

/// Pointer coordinates `< 0` mean the cursor is not over the aquarium.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_step(handle: *mut DcSim, dt: f32, pointer_x: f32, pointer_y: f32) {
    with_sim(handle, (), |s| {
        s.step(dt, optional_point(pointer_x, pointer_y))
    });
}

/// Copies up to `cap` fish into `out` and returns how many fish exist.
/// Pass `out = NULL` to just query the count.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_fish(handle: *mut DcSim, out: *mut DcFish, cap: u32) -> u32 {
    with_sim(handle, 0, |s| {
        let fish = s.fish();
        if !out.is_null() {
            // SAFETY: caller provides room for `cap` elements.
            let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
            for (d, f) in dst.iter_mut().zip(fish) {
                *d = DcFish {
                    id: f.id,
                    x: f.x,
                    y: f.y,
                    w: f.w,
                    h: f.h,
                    facing: f.facing,
                    phase: f.phase,
                    amplitude: f.amplitude(),
                    tilt: f.tilt(),
                    depth: f.depth,
                    sprite: f.sprite,
                    hunger: f.hunger,
                    character: f.personality.character.index(),
                    sheltered: f.sheltered as u32,
                };
            }
        }
        fish.len() as u32
    })
}

/// Same contract as `dc_sim_fish`, for food pellets.
/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`, and `out` null or valid for `cap` writes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_food(handle: *mut DcSim, out: *mut DcFood, cap: u32) -> u32 {
    with_sim(handle, 0, |s| {
        let food = s.food();
        if !out.is_null() {
            // SAFETY: caller provides room for `cap` elements.
            let dst = unsafe { std::slice::from_raw_parts_mut(out, cap as usize) };
            for (d, p) in dst.iter_mut().zip(food) {
                *d = DcFood {
                    x: p.x,
                    y: p.y,
                    opacity: p.opacity(),
                    size: p.diameter(),
                };
            }
        }
        food.len() as u32
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn roundtrip_through_c_abi() {
        let h = dc_sim_new(7);
        // SAFETY: h is live until dc_sim_free; buffers are sized from the returned count.
        unsafe {
            assert!(dc_sim_ok(h));
            let specs: Vec<DcFishSpec> = (0..7)
                .map(|i| DcFishSpec {
                    id: i,
                    sprite: i as u32,
                    aspect: 0.6,
                    scale: 1.0,
                    adult: 1,
                    mouth_x: -1.0,
                    mouth_y: -1.0,
                    body: 0,
                })
                .collect();
            dc_sim_set_population(h, specs.as_ptr(), 7, false);
            dc_sim_resize(h, 1280.0, 720.0);
            dc_sim_feed(h, 640.0);
            dc_sim_step(h, 0.033, -1.0, -1.0);
            let n = dc_sim_fish(h, ptr::null_mut(), 0);
            assert_eq!(n, 7);
            let mut buf = vec![DcFish::default(); n as usize];
            assert_eq!(dc_sim_fish(h, buf.as_mut_ptr(), n), 7);
            assert!(buf.iter().all(|f| f.w > 0.0));
            assert!(buf.iter().enumerate().all(|(i, f)| f.sprite == i as u32));
            assert_eq!(dc_sim_food(h, ptr::null_mut(), 0), 8);

            dc_sim_set_daylight(h, 0.2);
            let rel = [DcRelation {
                a: 1,
                b: 2,
                affinity: 0.7,
            }];
            dc_sim_set_relations(h, rel.as_ptr(), 1);
            assert_eq!(dc_sim_relations(h, ptr::null_mut(), 0), 1);
            let mut out = [DcRelation::default(); 1];
            dc_sim_relations(h, out.as_mut_ptr(), 1);
            assert_eq!((out[0].a, out[0].b), (1, 2));
            dc_sim_free(h);
        }
    }

    #[test]
    fn null_handle_is_harmless() {
        // SAFETY: null is an allowed handle.
        unsafe {
            dc_sim_step(ptr::null_mut(), 0.1, 0.0, 0.0);
            assert_eq!(dc_sim_fish(ptr::null_mut(), ptr::null_mut(), 0), 0);
            assert!(!dc_sim_ok(ptr::null()));
        }
    }

    #[test]
    fn panic_poisons_instead_of_unwinding() {
        let h = dc_sim_new(1);
        let r = with_sim(h, 5, |_| panic!("boom"));
        assert_eq!(r, 5);
        // SAFETY: h is live.
        unsafe {
            assert!(!dc_sim_ok(h));
            dc_sim_free(h);
        }
    }
}
