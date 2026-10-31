//! Swimming a fish yourself: pick a fish and steer it with the keyboard (the
//! host turns WASD / arrow keys into a direction); a dash gives a quick burst
//! of speed. The fish still eats food of its size that its mouth touches.

use super::{EAT_RADIUS, Fish, Mode, PELLET_NOURISHMENT, Sim};

/// Steered fish swim this much faster than their top speed on their own.
const STEER_SPEED: f32 = 2.4;
/// A dash: this many times the steering speed, for DASH_SECS, then a rest.
const DASH_BOOST: f32 = 3.2;
const DASH_SECS: f32 = 0.6;
const DASH_REST: f32 = 0.5;
/// Going deeper or nearer: depth units (0 near .. 1 far) per second.
const DEPTH_SPEED: f32 = 0.7;

#[derive(Debug, Clone, Copy, PartialEq, Default)]
pub struct Control {
    pub fish: u64,
    /// Wanted direction, -1..1 each (from the keys); dz > 0 is away
    dx: f32,
    dy: f32,
    dz: f32,
    dash_left: f32,
    dash_rest: f32,
}

impl Sim {
    /// Takes over fish `id`; false if there is no such fish here
    pub fn control_start(&mut self, id: u64) -> bool {
        self.control_end();
        let Some(f) = self.fish.iter_mut().find(|f| f.id == id && !f.entering) else {
            return false;
        };
        f.mode = Mode::Controlled;
        f.flee_left = 0.0;
        f.follow_left = 0.0;
        self.control = Some(Control {
            fish: id,
            ..Default::default()
        });
        true
    }

    /// The direction the keys point (x right, y down, z away; each -1..1),
    /// and whether to dash now
    pub fn control_input(&mut self, dx: f32, dy: f32, dz: f32, dash: bool) {
        let Some(c) = &mut self.control else { return };
        let len = dx.hypot(dy);
        (c.dx, c.dy) = if len > 1.0 {
            (dx / len, dy / len)
        } else {
            (dx, dy)
        };
        c.dz = dz.clamp(-1.0, 1.0);
        if dash && c.dash_left <= 0.0 && c.dash_rest <= 0.0 {
            c.dash_left = DASH_SECS;
        }
    }

    /// Lets go: the fish swims on by itself
    pub fn control_end(&mut self) {
        let Some(c) = self.control.take() else { return };
        if let Some(f) = self.fish.iter_mut().find(|f| f.id == c.fish) {
            f.mode = Mode::Wander;
            f.retarget = true;
        }
    }

    pub fn controlled(&self) -> Option<u64> {
        self.control.map(|c| c.fish)
    }

    /// The steered fish's step: it goes where the keys say
    pub(super) fn step_controlled(&mut self, f: &mut Fish, dt: f32) {
        let Some(mut c) = self.control.filter(|c| c.fish == f.id) else {
            f.mode = Mode::Wander;
            f.retarget = true;
            return;
        };
        let mut speed = f.max_speed * STEER_SPEED;
        if c.dash_left > 0.0 {
            c.dash_left -= dt;
            speed *= DASH_BOOST;
            if c.dash_left <= 0.0 {
                c.dash_rest = DASH_REST;
            }
            // A little trail of bubbles behind it
            if self.rng.chance(dt * 20.0) {
                let tail = f.x - f.facing * f.w * 0.45;
                self.blow_bubbles(tail, f.y, 1, (2.0, 3.5), 0.3);
            }
        } else {
            c.dash_rest = (c.dash_rest - dt).max(0.0);
        }
        // Ease toward the wanted velocity: quick to start, a little glide to stop
        let (wx, wy) = (c.dx * speed, c.dy * speed);
        let k = (dt * if c.dash_left > 0.0 { 12.0 } else { 5.0 }).min(1.0);
        f.vx += (wx - f.vx) * k;
        f.vy += (wy - f.vy) * k;
        f.x += f.vx * dt;
        f.y += f.vy * dt;
        // Deeper or nearer: smaller and hazier away, bigger up close (the same
        // way depth sizes fish when they arrive)
        if c.dz != 0.0 {
            let boost = if c.dash_left > 0.0 { 2.0 } else { 1.0 };
            let depth = (f.depth + c.dz * DEPTH_SPEED * boost * dt).clamp(0.0, 1.0);
            let ratio = (1.0 - 0.45 * depth) / (1.0 - 0.45 * f.depth);
            f.base_w *= ratio;
            f.w *= ratio;
            f.h *= ratio;
            f.depth = depth;
        }
        self.clamp_to_water(f);
        if f.vx.abs() > 10.0 {
            f.dir = f.vx.signum();
        }
        f.facing += (f.dir - f.facing).clamp(-8.0 * dt, 8.0 * dt);
        f.speed = f.vx.hypot(f.vy);
        f.phase = (f.phase + dt * (2.5 + f.speed * 0.07)) % (std::f32::consts::TAU * 64.0);
        f.hunger = (f.hunger + dt * super::HUNGER_RATE).min(1.0);

        // It eats food of its size that its mouth touches
        let (mx, my) = f.mouth();
        let bite = f.bite();
        if let Some(j) = self.food.iter().position(|p| {
            !p.eaten && p.size == bite && (p.x - mx).hypot(p.y - my) < EAT_RADIUS * 2.0
        }) {
            self.food[j].eaten = true;
            f.hunger = (f.hunger - PELLET_NOURISHMENT).max(0.0);
            self.eaten_this_step.push((f.id, j));
            self.blow_bubbles(mx, my, 1, (2.5, 3.5), 0.2);
        }
        self.control = Some(c);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sim::{Body, DEFAULT_MOUTH, FishSpec};

    const DT: f32 = 1.0 / 30.0;

    fn one_fish() -> Sim {
        let mut s = Sim::new(2);
        s.set_population(
            &[FishSpec {
                id: 3,
                sprite: 0,
                aspect: 0.6,
                scale: 1.0,
                adult: true,
                mouth: DEFAULT_MOUTH,
                body: Body::Fish,
                speed: 1.0,
            }],
            false,
        );
        s.resize(1920.0, 1080.0);
        s.fish[0].x = 960.0;
        s.fish[0].y = 500.0;
        s
    }

    #[test]
    fn a_steered_fish_goes_where_the_keys_say_and_dashes() {
        let mut s = one_fish();
        assert!(s.control_start(3));
        s.control_input(-1.0, 0.0, 0.0, false);
        for _ in 0..30 {
            s.step(DT, None);
        }
        let after_walk = s.fish()[0].x;
        assert!(after_walk < 900.0, "it should swim left (x {after_walk})");
        assert!(s.fish()[0].facing < 0.0, "facing left");
        s.control_input(0.0, 1.0, 0.0, false);
        let y0 = s.fish()[0].y;
        for _ in 0..30 {
            s.step(DT, None);
        }
        assert!(s.fish()[0].y > y0 + 40.0, "and down");
        // A dash is much faster than plain steering
        s.control_input(1.0, 0.0, 0.0, false);
        for _ in 0..20 {
            s.step(DT, None);
        }
        let plain = s.fish()[0].speed;
        s.control_input(1.0, 0.0, 0.0, true);
        for _ in 0..5 {
            s.step(DT, None);
        }
        assert!(
            s.fish()[0].speed > plain * 1.8,
            "dash: {} vs {plain}",
            s.fish()[0].speed
        );
        // Away into the tank: deeper and smaller
        let (d0, w0) = (s.fish()[0].depth, s.fish()[0].w);
        s.control_input(0.0, 0.0, 1.0, false);
        for _ in 0..15 {
            s.step(DT, None);
        }
        assert!(
            s.fish()[0].depth > d0 + 0.2 || s.fish()[0].depth == 1.0,
            "it should go deeper"
        );
        assert!(s.fish()[0].w < w0, "and look smaller");
        // Let go: it swims on by itself
        s.control_end();
        s.step(DT, None);
        assert!(!matches!(s.fish()[0].mode, Mode::Controlled));
        assert_eq!(s.controlled(), None);
    }

    #[test]
    fn a_steered_fish_eats_what_its_mouth_touches() {
        let mut s = one_fish();
        s.control_start(3);
        s.fish[0].facing = 1.0;
        s.fish[0].dir = 1.0;
        let (mx, my) = s.fish[0].mouth();
        s.feed_at(mx + 60.0, my);
        s.food.truncate(1);
        s.food[0].x = mx + 60.0;
        s.food[0].y = my;
        s.food[0].size = s.fish[0].bite();
        s.fish[0].facing = 1.0;
        s.fish[0].dir = 1.0;
        s.control_input(1.0, 0.0, 0.0, false);
        for _ in 0..30 * 2 {
            s.step(DT, None);
            s.control_input(1.0, 0.0, 0.0, false);
        }
        assert!(
            s.food().is_empty(),
            "the pellet in its way should have been eaten"
        );
    }
}
