//! C ABI used by the Qt plugin (see `include/desktocean_sim.h`).
//!
//! The library runs inside plasmashell, so a panic must never unwind into C++:
//! every entry point catches it, marks the handle poisoned and returns a
//! neutral value. A poisoned simulation simply stops; the desktop survives.

use std::ffi::c_char;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::ptr;

use crate::sim::Sim;

pub struct DcSim {
    sim: Sim,
    poisoned: bool,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcFish {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    pub facing: f32,
    pub phase: f32,
    pub amplitude: f32,
    pub tilt: f32,
    pub depth: f32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct DcFood {
    pub x: f32,
    pub y: f32,
    pub opacity: f32,
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

/// # Safety
/// `handle` must be null or a live pointer from `dc_sim_new`.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn dc_sim_set_fish_count(handle: *mut DcSim, count: u32) {
    with_sim(handle, (), |s| s.set_fish_count(count.min(200) as usize));
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
                    x: f.x,
                    y: f.y,
                    w: f.w,
                    h: f.h,
                    facing: f.facing,
                    phase: f.phase,
                    amplitude: f.amplitude(),
                    tilt: f.tilt(),
                    depth: f.depth,
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
            dc_sim_resize(h, 1280.0, 720.0);
            dc_sim_feed(h, 640.0);
            dc_sim_step(h, 0.033, -1.0, -1.0);
            let n = dc_sim_fish(h, ptr::null_mut(), 0);
            assert_eq!(n, 7);
            let mut buf = vec![DcFish::default(); n as usize];
            assert_eq!(dc_sim_fish(h, buf.as_mut_ptr(), n), 7);
            assert!(buf.iter().all(|f| f.w > 0.0));
            assert_eq!(dc_sim_food(h, ptr::null_mut(), 0), 8);
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
