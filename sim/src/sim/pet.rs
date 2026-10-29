//! Petting and stirring the water with the cursor.
//!
//! Clicks never reach the wallpaper, so petting is a gesture: stroke a fish by
//! moving the cursor gently back and forth over it. It stops, enjoys it (a few
//! hearts float up) and trusts you a little more. Trust also grows when a fish
//! eats food you dropped; fish that trust you follow the cursor more readily,
//! and shy ones stop fleeing from it. Sweeping the cursor quickly through the
//! water makes a current that pushes fish and bubbles along.

use super::{Fish, Mode, Sim};

/// Px of gentle stroking over a fish before it counts as being petted.
const PET_STROKE: f32 = 455.0;
/// Cursor speeds (px/s) that count as stroking; faster is a sweep.
const STROKE_SPEED: (f32, f32) = (40.0, 1100.0);
/// Above this, the cursor stirs the water.
const SWEEP_SPEED: f32 = 1500.0;
const SWEEP_RADIUS: f32 = 260.0;
/// How long a petted fish stays still after the last stroke.
const PETTED_SECS: f32 = 2.5;
/// Trust fades back over a few days without attention.
const TRUST_FADE_PER_SEC: f32 = 1.0 / (3.0 * 24.0 * 3600.0);

#[derive(Debug, Clone)]
pub struct Heart {
    pub x: f32,
    pub y: f32,
    pub size: f32,
    age: f32,
    seed: f32,
}

impl Heart {
    pub const LIFETIME: f32 = 2.2;

    pub fn alpha(&self) -> f32 {
        let t = self.age / Self::LIFETIME;
        (t * 6.0).min(1.0) * (1.0 - t).max(0.0)
    }
}

impl Fish {
    /// How much this fish trusts you, 0 .. 1
    pub fn trust(&self) -> f32 {
        self.trust
    }

    /// The cursor is over its body (a little generous)
    fn under_cursor(&self, (px, py): (f32, f32)) -> bool {
        let dx = (px - self.x) / (self.w * 0.6);
        let dy = (py - self.y) / (self.h * 0.8).max(20.0);
        dx * dx + dy * dy < 1.0
    }
}

impl Sim {
    pub fn hearts(&self) -> &[Heart] {
        &self.hearts
    }

    /// Trust by fish id, for saving
    pub fn trust_list(&self) -> Vec<(u64, f32)> {
        self.fish
            .iter()
            .filter(|f| f.trust > 0.001)
            .map(|f| (f.id, f.trust))
            .chain(self.saved_trust.iter().map(|(id, t)| (*id, *t)))
            .collect()
    }

    /// Restores saved trust; ids not in the tank yet are kept until they arrive
    pub fn set_trust(&mut self, list: &[(u64, f32)]) {
        self.saved_trust.clear();
        for &(id, t) in list {
            let t = if t.is_finite() {
                t.clamp(0.0, 1.0)
            } else {
                0.0
            };
            match self.fish.iter_mut().find(|f| f.id == id) {
                Some(f) => f.trust = t,
                None => {
                    self.saved_trust.insert(id, t);
                }
            }
        }
    }

    /// A fish that ate food you dropped trusts you a bit more
    pub(super) fn ate_your_food(f: &mut Fish) {
        f.trust = (f.trust + 0.02 * (1.0 - f.trust)).min(1.0);
    }

    pub(super) fn update_petting(&mut self, dt: f32, pointer: Option<(f32, f32)>) {
        // Cursor speed from frame to frame
        let vel = match (pointer, self.last_pointer) {
            (Some((x, y)), Some((lx, ly))) if dt > 0.0 => ((x - lx) / dt, (y - ly) / dt),
            _ => (0.0, 0.0),
        };
        self.last_pointer = pointer;
        let speed = vel.0.hypot(vel.1);

        for f in &mut self.fish {
            // Saved trust for a fish that has arrived since
            if let Some(t) = self.saved_trust.remove(&f.id) {
                f.trust = t;
            }
            f.trust = (f.trust - TRUST_FADE_PER_SEC * dt).max(0.0);
            if f.entering {
                continue;
            }
            let stroking = pointer.is_some_and(|p| f.under_cursor(p))
                && (STROKE_SPEED.0..STROKE_SPEED.1).contains(&speed);
            if stroking {
                f.pet += speed * dt;
            } else {
                f.pet = (f.pet - 250.0 * dt).max(0.0);
            }
            match f.mode {
                Mode::Petted { .. } if stroking => f.mode = Mode::Petted { left: PETTED_SECS },
                Mode::Eating { .. } | Mode::Petted { .. } => {}
                _ if f.pet > PET_STROKE => {
                    f.pet = 0.0;
                    f.trust = (f.trust + 0.12 * (1.0 - f.trust)).min(1.0);
                    f.mode = Mode::Petted { left: PETTED_SECS };
                    f.flee_left = 0.0;
                }
                _ => {}
            }
        }

        // A quick sweep stirs the water
        if let Some((px, py)) = pointer.filter(|_| speed > SWEEP_SPEED) {
            let (dx, dy) = (vel.0 / speed, vel.1 / speed);
            let push = (speed / SWEEP_SPEED).min(2.5);
            for f in &mut self.fish {
                let d = (f.x - px).hypot(f.y - py);
                if d < SWEEP_RADIUS && !f.entering {
                    let k = (1.0 - d / SWEEP_RADIUS) * push * f.max_speed * (dt * 6.0).min(1.0);
                    f.vx += dx * k;
                    f.vy += dy * k * 0.6;
                }
            }
            for b in &mut self.bubbles {
                let d = (b.x - px).hypot(b.y - py);
                if d < SWEEP_RADIUS {
                    let k = (1.0 - d / SWEEP_RADIUS) * push * 120.0 * dt;
                    b.x += dx * k;
                    b.y += dy * k * 0.5;
                }
            }
        }

        // Hearts float up and fade
        for h in &mut self.hearts {
            h.age += dt;
            h.y -= 28.0 * dt;
            h.x += (h.age * 2.5 + h.seed).sin() * 12.0 * dt;
        }
        self.hearts.retain(|h| h.age < Heart::LIFETIME);
    }

    /// Being petted: holds still, rocks a little, now and then a heart
    pub(super) fn step_petted(&mut self, f: &mut Fish, left: f32, dt: f32) {
        let brake = 1.0 - (4.0 * dt).min(1.0);
        f.vx *= brake;
        f.vy *= brake;
        f.x += f.vx * dt;
        f.y += f.vy * dt;
        self.clamp_to_water(f);
        f.speed = f.vx.hypot(f.vy);
        // A slow, happy tail
        f.phase = (f.phase + dt * 2.0) % (std::f32::consts::TAU * 64.0);
        f.heart_timer -= dt;
        if f.heart_timer <= 0.0 {
            f.heart_timer = self.rng.range(0.5, 0.9);
            let size = (f.h * 0.3).clamp(14.0, 30.0);
            self.hearts.push(Heart {
                x: f.x + self.rng.range(-0.2, 0.2) * f.w,
                y: f.y - f.h * 0.5,
                size,
                age: 0.0,
                seed: self.rng.range(0.0, 6.0),
            });
        }
        let left = left - dt;
        f.mode = if left > 0.0 {
            Mode::Petted { left }
        } else {
            f.retarget = true;
            Mode::Wander
        };
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sim::{Character, DEFAULT_MOUTH, FishSpec};

    const DT: f32 = 1.0 / 30.0;

    fn one_fish() -> Sim {
        let mut s = Sim::new(4);
        s.set_population(
            &[FishSpec {
                id: 9,
                sprite: 0,
                aspect: 0.6,
                scale: 1.0,
                adult: true,
                mouth: DEFAULT_MOUTH,
                body: Default::default(),
            }],
            false,
        );
        s.resize(1920.0, 1080.0);
        s.fish[0].hunger = 0.0;
        s
    }

    /// The cursor strokes back and forth over the fish at about 300 px/s
    fn stroke(s: &mut Sim, n: usize) -> Option<(f32, f32)> {
        let f = &s.fish()[0];
        let t = n as f32 * DT;
        Some((f.x + (t * 5.0).sin() * f.w * 0.3, f.y))
    }

    #[test]
    fn stroking_a_fish_pets_it() {
        let mut s = one_fish();
        let mut petted = false;
        for n in 0..30 * 6 {
            let p = stroke(&mut s, n);
            s.step(DT, p);
            petted |= matches!(s.fish()[0].mode, Mode::Petted { .. });
        }
        assert!(petted, "stroking should pet the fish");
        assert!(
            !s.hearts().is_empty() || s.fish()[0].trust() > 0.1,
            "a petted fish shows it"
        );
        assert!(
            s.fish()[0].trust() > 0.1,
            "petting builds trust ({})",
            s.fish()[0].trust()
        );
        assert!(
            s.fish()[0].speed < s.fish()[0].max_speed * 0.5,
            "it holds still while petted"
        );
    }

    #[test]
    fn a_shy_fish_that_trusts_you_stays() {
        let mut s = one_fish();
        s.fish[0].personality.character = Character::Shy;
        s.fish[0].trust = 0.6;
        let start = (s.fish[0].x, s.fish[0].y);
        for n in 0..30 * 3 {
            let p = stroke(&mut s, n);
            s.step(DT, p);
        }
        assert_eq!(
            s.fish()[0].flee_left,
            0.0,
            "a trusting shy fish doesn't flee"
        );
        assert!(matches!(
            s.fish()[0].mode,
            Mode::Petted { .. } | Mode::Wander | Mode::Idle { .. }
        ));
        let _ = start;
    }

    #[test]
    fn a_quick_sweep_pushes_fish_along() {
        let mut s = one_fish();
        let (x, y) = (s.fish[0].x, s.fish[0].y);
        s.fish[0].vx = 0.0;
        s.fish[0].vy = 0.0;
        // Sweep left to right right past the fish
        for k in 0..6 {
            s.step(DT, Some((x - 150.0 + k as f32 * 70.0, y + 30.0)));
        }
        assert!(
            s.fish()[0].vx > 20.0,
            "the current should carry it right ({})",
            s.fish()[0].vx
        );
    }
}
