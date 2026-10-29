//! Eggs: a couple's babies start as a clutch of eggs, laid on a plant (a
//! "nest" spot the host passes in) or wherever the mother is if there are no
//! plants. They hatch after a while and the babies come out right there.

use super::{Birth, Sim};

/// Seconds of life time (scaled by the life speed) until eggs hatch.
pub const HATCH_SECS: f32 = 600.0;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Egg {
    pub parent_a: u64,
    pub parent_b: u64,
    pub x: f32,
    pub y: f32,
    /// Babies that will hatch
    pub count: u32,
    /// 0 just laid .. 1 hatching
    pub progress: f32,
}

impl Sim {
    pub fn eggs(&self) -> &[Egg] {
        &self.eggs
    }

    /// Restores eggs saved from an earlier run
    pub fn set_eggs(&mut self, eggs: &[Egg]) {
        self.eggs = eggs
            .iter()
            .filter(|e| e.x.is_finite() && e.y.is_finite() && e.progress.is_finite())
            .map(|e| Egg {
                count: e.count.clamp(1, 5),
                progress: e.progress.clamp(0.0, 1.0),
                ..*e
            })
            .take(50)
            .collect();
    }

    /// Spots on plants where eggs can be laid (scene pixels)
    pub fn set_nests(&mut self, nests: &[(f32, f32)]) {
        self.nests = nests
            .iter()
            .copied()
            .filter(|(x, y)| x.is_finite() && y.is_finite())
            .collect();
    }

    /// The nest closest to (x, y), if there are any
    pub(super) fn nearest_nest(&self, x: f32, y: f32) -> Option<(f32, f32)> {
        self.nests.iter().copied().min_by(|a, b| {
            (a.0 - x)
                .hypot(a.1 - y)
                .total_cmp(&(b.0 - x).hypot(b.1 - y))
        })
    }

    pub(super) fn lay_eggs(&mut self, parent_a: u64, parent_b: u64, x: f32, y: f32, count: u32) {
        // Spread a little so clutches on the same plant don't sit on top of each other
        let x = x + self.rng.range(-12.0, 12.0);
        let y = y + self.rng.range(-8.0, 8.0);
        self.eggs.push(Egg {
            parent_a,
            parent_b,
            x,
            y,
            count,
            progress: 0.0,
        });
    }

    pub(super) fn update_eggs(&mut self, life_dt: f32) {
        if life_dt <= 0.0 {
            return;
        }
        for e in &mut self.eggs {
            e.progress = (e.progress + life_dt / HATCH_SECS).min(1.0);
        }
        let (hatched, waiting): (Vec<Egg>, Vec<Egg>) = std::mem::take(&mut self.eggs)
            .into_iter()
            .partition(|e| e.progress >= 1.0);
        self.eggs = waiting;
        for e in hatched {
            // A few bubbles as they come out
            self.blow_bubbles(e.x, e.y, 3 + e.count as usize, (2.5, 4.5), 0.8);
            self.births.push(Birth {
                parent_a: e.parent_a,
                parent_b: e.parent_b,
                x: e.x,
                y: e.y,
                count: e.count,
            });
        }
    }

    /// A parent of eggs that are still waiting: (x, y) of the clutch
    pub(super) fn own_eggs(&self, id: u64) -> Option<(f32, f32)> {
        self.eggs
            .iter()
            .find(|e| e.parent_a == id || e.parent_b == id)
            .map(|e| (e.x, e.y))
    }
}
