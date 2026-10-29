//! Carrying a fish in a bubble. The bubble wand widget blows a bubble at the
//! cursor; held next to a fish for a moment, the fish swims into it on its own
//! and floats along calmly inside while you move it. Let go and the bubble
//! pops softly; the fish swims out where you put it.

use super::{Fish, Mode, Sim};

/// A fish this close to the bubble may come in.
const INVITE_RADIUS: f32 = 220.0;
/// Seconds the bubble has to wait by a fish before it comes (less if it trusts you).
const INVITE_SECS: f32 = 0.8;
/// Radius of an empty bubble.
const EMPTY_RADIUS: f32 = 45.0;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Carry {
    pub x: f32,
    pub y: f32,
    /// The fish inside, or on its way in
    pub fish: Option<u64>,
    /// It has arrived: it now floats where the bubble goes
    pub holding: bool,
    /// The fish the bubble is waiting by, and for how long
    candidate: Option<u64>,
    linger: f32,
    /// Let go of: floating up to pop at the surface
    pub rising: bool,
    /// Being led around (by the cursor); let go, it drifts on with its
    /// momentum, slows down, and after IDLE_SECS alone floats up
    pub held: bool,
    vx: f32,
    vy: f32,
    idle: f32,
    /// When it was last moved, and where it was at the last step (for its speed)
    moved_at: f32,
    last: (f32, f32),
}

/// Left alone this long (not moving much), a bubble floats up to pop.
const IDLE_SECS: f32 = 3.0;

impl Carry {
    pub fn radius(&self, fish: &[Fish]) -> f32 {
        match self.fish.and_then(|id| fish.iter().find(|f| f.id == id)) {
            Some(f) if self.holding => f.w.max(f.h) * 0.62 + 10.0,
            _ => EMPTY_RADIUS,
        }
    }

    /// 0 .. 1 while waiting for a fish to come
    pub fn invite_progress(&self) -> f32 {
        if self.fish.is_some() {
            1.0
        } else {
            (self.linger / INVITE_SECS).min(1.0)
        }
    }
}

impl Sim {
    pub fn carry(&self) -> Option<&Carry> {
        self.carry.as_ref()
    }

    /// Where to draw the bubble: around the fish once it is inside (it can't
    /// leave the water), at the cursor otherwise. (x, y, radius)
    pub fn carry_bubble(&self) -> Option<(f32, f32, f32)> {
        let c = self.carry.as_ref()?;
        let held = c
            .fish
            .filter(|_| c.holding)
            .and_then(|id| self.fish.iter().find(|f| f.id == id));
        Some(match held {
            Some(f) => (f.x, f.y, c.radius(&self.fish)),
            None => (c.x, c.y, c.radius(&self.fish)),
        })
    }

    /// The bubble is at (x, y) (scene pixels); starts one if there is none
    pub fn carry_to(&mut self, x: f32, y: f32) {
        match &mut self.carry {
            Some(c) => {
                c.x = x;
                c.y = y;
                c.rising = false;
                c.held = true;
                c.idle = 0.0;
                c.moved_at = self.time;
            }
            None => {
                self.carry = Some(Carry {
                    x,
                    y,
                    fish: None,
                    holding: false,
                    candidate: None,
                    linger: 0.0,
                    rising: false,
                    held: false,
                    vx: 0.0,
                    vy: 0.0,
                    idle: 0.0,
                    moved_at: self.time,
                    last: (x, y),
                })
            }
        }
    }

    /// Let go of the bubble: it carries on with the speed it had
    pub fn carry_release(&mut self) {
        if let Some(c) = &mut self.carry {
            c.held = false;
            c.idle = 0.0;
            // Held still before letting go: no fling
            if self.time - c.moved_at > 0.1 {
                c.vx = 0.0;
                c.vy = 0.0;
            }
        }
    }

    /// Left alone: the bubble floats up and pops at the surface
    pub fn carry_rise(&mut self) {
        if let Some(c) = &mut self.carry {
            c.rising = true;
        }
    }

    /// Let go: the bubble pops and the fish swims out
    pub fn carry_end(&mut self) {
        // Pops where it is seen (around its fish)
        let Some((bx, by, r)) = self.carry_bubble() else {
            return;
        };
        let Some(c) = self.carry.take() else { return };
        let (c_x, c_y) = (bx, by);
        // The pop: a ring of small bubbles
        for k in 0..8 {
            let a = k as f32 * std::f32::consts::TAU / 8.0;
            self.blow_bubbles(
                c_x + a.cos() * r * 0.8,
                c_y + a.sin() * r * 0.8,
                1,
                (2.5, 4.0),
                0.3,
            );
        }
        if let Some(f) = c
            .fish
            .and_then(|id| self.fish.iter_mut().find(|f| f.id == id))
        {
            f.mode = Mode::Wander;
            f.retarget = true;
            f.trust = (f.trust + 0.05 * (1.0 - f.trust)).min(1.0);
        }
    }

    pub(super) fn update_carry(&mut self, dt: f32) {
        let Some(mut c) = self.carry else { return };
        if c.held && dt > 0.0 {
            // Its speed while being led, smoothed: it keeps it when let go
            c.vx = 0.6 * c.vx + 0.4 * (c.x - c.last.0) / dt;
            c.vy = 0.6 * c.vy + 0.4 * (c.y - c.last.1) / dt;
        }
        c.last = (c.x, c.y);
        if !c.held && !c.rising {
            // Drifting on after a fling, slowing down in the water
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            let drag = (-1.6 * dt).exp();
            c.vx *= drag;
            c.vy *= drag;
            // Flung into a side, the sand or the surface: pop
            let r = c.radius(&self.fish);
            if c.x - r < 0.0
                || c.x + r > self.width
                || c.y - r < self.water_top()
                || c.y + r > self.height
            {
                self.carry = Some(c);
                self.carry_end();
                return;
            }
            // Settled and left alone a while: up it goes
            if c.vx.hypot(c.vy) < 40.0 {
                c.idle += dt;
                if c.idle >= IDLE_SECS {
                    c.rising = true;
                }
            }
        }
        if c.rising {
            // Up it goes, swaying a little, and pops just under the surface
            c.y -= 70.0 * dt;
            c.x += (self.time * 2.0).sin() * 12.0 * dt;
            let r = c.radius(&self.fish);
            if c.y - r <= self.water_top() + 4.0 || c.y < 0.0 {
                self.carry = Some(c);
                self.carry_end();
                return;
            }
        }
        // The fish it held went away (to another screen, out of the library)
        if c.fish
            .is_some_and(|id| !self.fish.iter().any(|f| f.id == id))
        {
            c.fish = None;
            c.holding = false;
        }
        if c.fish.is_none() {
            // Waiting by the nearest fish that could come
            let nearest = self
                .fish
                .iter()
                .filter(|f| !f.entering && f.travel == 0 && !f.arriving)
                .map(|f| (f.id, (f.x - c.x).hypot(f.y - c.y), f.trust))
                .filter(|&(_, d, _)| d < INVITE_RADIUS)
                .min_by(|a, b| a.1.total_cmp(&b.1));
            match nearest {
                Some((id, _, trust)) => {
                    if c.candidate == Some(id) {
                        c.linger += dt;
                    } else {
                        c.candidate = Some(id);
                        c.linger = 0.0;
                    }
                    if c.linger >= INVITE_SECS * (1.0 - 0.6 * trust) {
                        c.fish = Some(id);
                        if let Some(f) = self.fish.iter_mut().find(|f| f.id == id) {
                            f.mode = Mode::Bubbled;
                            f.flee_left = 0.0;
                            f.follow_left = 0.0;
                        }
                    }
                }
                None => {
                    c.candidate = None;
                    c.linger = 0.0;
                }
            }
        }
        self.carry = Some(c);
    }

    /// In (or swimming into) the bubble: calm, follows it, now and then a heart
    pub(super) fn step_bubbled(&mut self, f: &mut Fish, dt: f32) {
        let Some(mut c) = self.carry.filter(|c| c.fish == Some(f.id)) else {
            f.mode = Mode::Wander;
            f.retarget = true;
            return;
        };
        // Where it can get to: the bubble, kept inside the water
        let tx =
            c.x.clamp(f.w * 0.5, (self.width - f.w * 0.5).max(f.w * 0.5));
        let ty =
            c.y.clamp(self.water_top() + f.h * 0.5, self.sand_top() - f.h * 0.3);
        let (dx, dy) = (tx - f.x, ty - f.y);
        let dist = dx.hypot(dy);
        // Brought to the front of the tank, so nobody swims across it
        f.depth = (f.depth - dt * 0.8).max(0.0);
        if c.holding {
            // Floats in the middle, swaying a little
            let k = (dt * 8.0).min(1.0);
            f.vx = dx * k / dt.max(1e-3);
            f.vy = dy * k / dt.max(1e-3);
            f.x += dx * k;
            f.y += dy * k + (self.time * 1.3 + f.phase * 0.02).sin() * 4.0 * dt;
            f.phase = (f.phase + dt * 1.8) % (std::f32::consts::TAU * 64.0);
            f.heart_timer -= dt;
            if f.heart_timer <= 0.0 {
                f.heart_timer = self.rng.range(2.5, 4.5);
                self.hearts.push(super::Heart::new(
                    f.x,
                    f.y - f.h * 0.5,
                    (f.h * 0.3).clamp(14.0, 26.0),
                    self.rng.range(0.0, 6.0),
                ));
            }
        } else {
            // Swims in, unhurried
            let speed = (f.max_speed * 1.1).max(90.0).min(dist * 4.0);
            f.vx = dx / dist.max(1.0) * speed;
            f.vy = dy / dist.max(1.0) * speed;
            f.x += f.vx * dt;
            f.y += f.vy * dt;
            f.phase = (f.phase + dt * (2.5 + speed * 0.07)) % (std::f32::consts::TAU * 64.0);
            if dist < 15.0 {
                c.holding = true;
                self.carry = Some(c);
            }
        }
        // Face the way it is going; inside, keep facing
        if f.vx.abs() > 8.0 {
            f.dir = f.vx.signum();
        }
        f.facing += (f.dir - f.facing).clamp(-6.0 * dt, 6.0 * dt);
        f.speed = f.vx.hypot(f.vy);
        f.y =
            f.y.clamp(self.water_top() + f.h * 0.5, self.sand_top() - f.h * 0.3);
        f.x = f.x.clamp(f.w * 0.5, self.width - f.w * 0.5);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sim::{Body, Character, DEFAULT_MOUTH, FishSpec};

    const DT: f32 = 1.0 / 30.0;

    fn one_fish(character: Character) -> Sim {
        let mut s = Sim::new(3);
        s.set_population(
            &[FishSpec {
                id: 5,
                sprite: 0,
                aspect: 0.6,
                scale: 1.0,
                adult: true,
                mouth: DEFAULT_MOUTH,
                body: Body::Fish,
            }],
            false,
        );
        s.resize(1920.0, 1080.0);
        s.fish[0].personality.character = character;
        s.fish[0].hunger = 0.0;
        s
    }

    #[test]
    fn a_fish_swims_into_the_bubble_and_is_carried() {
        // Even a shy fish comes along (it just takes a moment)
        let mut s = one_fish(Character::Shy);
        let (fx, fy) = (s.fish[0].x, s.fish[0].y);
        // Beside it, on the side with room
        let (bx, by) = (if fx > 960.0 { fx - 120.0 } else { fx + 120.0 }, fy);
        for _ in 0..30 * 3 {
            s.carry_to(bx, by);
            s.step(DT, Some((bx, by)));
        }
        let c = *s.carry().unwrap();
        assert_eq!(c.fish, Some(5), "the fish should have come into the bubble");
        assert!(c.holding, "it should be inside by now");
        // Carry it across the tank
        for k in 0..30 * 4 {
            let x = 1500.0 - k as f32 * 6.0;
            s.carry_to(x, 400.0);
            s.step(DT, Some((x, 400.0)));
        }
        let f = &s.fish()[0];
        let c = *s.carry().unwrap();
        assert!(
            (f.x - c.x).hypot(f.y - c.y) < 20.0,
            "the fish should float in the bubble"
        );
        assert!(f.flee_left == 0.0, "nobody is frightened");
        // Let go
        s.carry_end();
        assert!(s.carry().is_none());
        for _ in 0..30 * 2 {
            s.step(DT, None);
        }
        assert!(
            !matches!(s.fish()[0].mode, Mode::Bubbled),
            "it swims off again"
        );
    }

    #[test]
    fn a_bubble_left_alone_rises_and_pops() {
        let mut s = one_fish(Character::Explorer);
        let (fx, fy) = (s.fish[0].x, s.fish[0].y);
        let bx = if fx > 960.0 { fx - 120.0 } else { fx + 120.0 };
        for _ in 0..30 * 3 {
            s.carry_to(bx, fy);
            s.step(DT, None);
        }
        assert!(s.carry().unwrap().holding);
        s.carry_release();
        for _ in 0..30 * 25 {
            s.step(DT, None);
            if s.carry().is_none() {
                break;
            }
        }
        assert!(s.carry().is_none(), "it should have popped at the surface");
        let f = &s.fish()[0];
        assert!(f.y < 1080.0 * 0.3, "the fish rode up with it (y {})", f.y);
        assert!(!matches!(f.mode, Mode::Bubbled));
    }

    #[test]
    fn a_flung_bubble_keeps_going_and_pops_at_the_side() {
        let mut s = one_fish(Character::Explorer);
        // Led quickly to the right, then let go mid-tank
        for k in 0..10 {
            s.carry_to(1100.0 + k as f32 * 40.0, 500.0);
            s.step(DT, None);
        }
        s.carry_release();
        let x0 = s.carry().unwrap().x;
        for _ in 0..10 {
            s.step(DT, None);
        }
        let c = s.carry().copied();
        assert!(
            c.is_none_or(|c| c.x > x0 + 100.0),
            "it should carry on to the right"
        );
        for _ in 0..30 * 4 {
            s.step(DT, None);
        }
        assert!(
            s.carry().is_none(),
            "it should have popped against the side"
        );
    }

    #[test]
    fn a_bubble_far_from_fish_stays_empty() {
        let mut s = one_fish(Character::Explorer);
        let (fx, fy) = (s.fish[0].x, s.fish[0].y);
        let (bx, by) = (if fx > 960.0 { 100.0 } else { 1800.0 }, fy);
        for _ in 0..30 * 3 {
            s.carry_to(bx, by);
            s.step(DT, Some((bx, by)));
        }
        assert_eq!(s.carry().unwrap().fish, None);
    }
}
