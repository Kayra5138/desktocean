//! Carrying fish in bubbles. The bubble wand widget blows bubbles (as many as
//! you like); a bubble led next to a fish for a moment gets the fish to swim
//! in on its own, and it floats along calmly inside. Flung, a bubble sails on
//! and bounces off the sides; left alone for a few seconds it floats up and
//! pops at the surface, and the fish swims out.

use super::{Fish, Mode, Sim};

/// A fish this close to the bubble may come in.
const INVITE_RADIUS: f32 = 220.0;
/// Seconds the bubble has to wait by a fish before it comes (everyone comes, shy or not).
const INVITE_SECS: f32 = 0.25;
/// Radius of an empty bubble.
const EMPTY_RADIUS: f32 = 45.0;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Carry {
    pub id: u32,
    pub x: f32,
    pub y: f32,
    /// The fish (or crawler) inside, or on its way in
    pub fish: Option<u64>,
    /// What is inside is a crawler (crab, snail...) rather than a fish
    pub crawler: bool,
    /// It has arrived: it now floats where the bubble goes
    pub holding: bool,
    /// The fish or crawler the bubble is waiting by (crawler?), and for how long
    candidate: Option<(bool, u64)>,
    linger: f32,
    /// Let go of: floating up to pop at the surface
    pub rising: bool,
    /// Being led around (by the cursor); let go, it drifts on with its
    /// momentum, slows down, and after IDLE_SECS alone floats up
    pub held: bool,
    vx: f32,
    vy: f32,
    idle: f32,
    /// Thrown when let go: it pops once its momentum runs out
    flung: bool,
    /// When it was last moved, and where it was at the last step (for its speed)
    moved_at: f32,
    last: (f32, f32),
}

/// Left alone this long (not moving much), a bubble floats up to pop.
const IDLE_SECS: f32 = 3.0;
/// Let go faster than this (px/s), a bubble counts as thrown.
const FLING_SPEED: f32 = 150.0;
/// A thrown bubble pops once it has slowed below this.
const SPENT_SPEED: f32 = 30.0;

impl Carry {
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
    pub fn carries(&self) -> &[Carry] {
        &self.carries
    }

    fn carry_index(&self, id: u32) -> Option<usize> {
        self.carries.iter().position(|c| c.id == id)
    }

    /// A bubble's radius: round whoever is inside
    pub fn carry_radius(&self, c: &Carry) -> f32 {
        if !c.holding {
            return EMPTY_RADIUS;
        }
        let size = match (c.fish, c.crawler) {
            (Some(id), false) => self.fish.iter().find(|f| f.id == id).map(|f| f.w.max(f.h)),
            (Some(id), true) => self
                .crawlers
                .iter()
                .find(|k| k.id == id)
                .map(|k| k.w.max(k.h)),
            _ => None,
        };
        size.map_or(EMPTY_RADIUS, |s| s * 0.62 + 10.0)
    }

    /// Where to draw a bubble: around its fish once it is inside (the fish
    /// can't leave the water), where it was led otherwise. (x, y, radius)
    pub fn bubble_of(&self, c: &Carry) -> (f32, f32, f32) {
        let r = self.carry_radius(c);
        let inside = match (c.fish.filter(|_| c.holding), c.crawler) {
            (Some(id), false) => self.fish.iter().find(|f| f.id == id).map(|f| (f.x, f.y)),
            (Some(id), true) => self
                .crawlers
                .iter()
                .find(|k| k.id == id)
                .map(|k| (k.x, k.y - k.h * 0.5)),
            _ => None,
        };
        let (x, y) = inside.unwrap_or((c.x, c.y));
        (x, y, r)
    }

    /// A new bubble at (x, y), scene pixels; it floats there on its own
    pub fn carry_new(&mut self, x: f32, y: f32) -> u32 {
        let id = self.next_carry;
        self.next_carry = self.next_carry.wrapping_add(1).max(1);
        self.carries.push(Carry {
            id,
            x,
            y,
            fish: None,
            crawler: false,
            holding: false,
            candidate: None,
            linger: 0.0,
            rising: false,
            held: false,
            vx: 0.0,
            vy: 0.0,
            idle: 0.0,
            flung: false,
            moved_at: self.time,
            last: (x, y),
        });
        id
    }

    /// Leads bubble `id` to (x, y); false if it has popped
    pub fn carry_to(&mut self, id: u32, x: f32, y: f32) -> bool {
        let time = self.time;
        let Some(c) = self.carries.iter_mut().find(|c| c.id == id) else {
            return false;
        };
        c.x = x;
        c.y = y;
        c.rising = false;
        c.held = true;
        c.idle = 0.0;
        c.flung = false;
        c.moved_at = time;
        true
    }

    /// Let go of a bubble: it carries on with the speed it had
    pub fn carry_release(&mut self, id: u32) {
        let time = self.time;
        if let Some(c) = self.carries.iter_mut().find(|c| c.id == id) {
            c.held = false;
            c.idle = 0.0;
            // Held still before letting go: no fling
            if time - c.moved_at > 0.1 {
                c.vx = 0.0;
                c.vy = 0.0;
            }
            c.flung = c.vx.hypot(c.vy) > FLING_SPEED;
        }
    }

    /// Up it goes, to pop at the surface
    pub fn carry_rise(&mut self, id: u32) {
        if let Some(c) = self.carries.iter_mut().find(|c| c.id == id) {
            c.rising = true;
        }
    }

    /// Pops a bubble; the fish inside swims out
    pub fn carry_end(&mut self, id: u32) {
        let Some(i) = self.carry_index(id) else {
            return;
        };
        let (bx, by, r) = self.bubble_of(&self.carries[i]);
        let c = self.carries.remove(i);
        // The pop: a ring of small bubbles where it was seen
        for k in 0..8 {
            let a = k as f32 * std::f32::consts::TAU / 8.0;
            self.blow_bubbles(
                bx + a.cos() * r * 0.8,
                by + a.sin() * r * 0.8,
                1,
                (2.5, 4.0),
                0.3,
            );
        }
        if c.crawler {
            // A crawler drops back down to the ground
            if let Some(k) = c
                .fish
                .and_then(|id| self.crawlers.iter_mut().find(|k| k.id == id))
            {
                k.carried = false;
                k.falling = true;
                k.fall_v = 0.0;
            }
        } else if let Some(f) = c
            .fish
            .and_then(|id| self.fish.iter_mut().find(|f| f.id == id))
        {
            f.mode = Mode::Wander;
            f.retarget = true;
            f.trust = (f.trust + 0.05 * (1.0 - f.trust)).min(1.0);
        }
    }

    pub(super) fn update_carry(&mut self, dt: f32) {
        let mut popped = Vec::new();
        for i in 0..self.carries.len() {
            let mut c = self.carries[i];
            if self.step_carry(&mut c, dt) {
                popped.push(c.id);
            }
            self.carries[i] = c;
        }
        for id in popped {
            self.carry_end(id);
        }
    }

    /// One bubble's step; true when it pops
    fn step_carry(&mut self, c: &mut Carry, dt: f32) -> bool {
        if c.held && dt > 0.0 {
            // Its speed while being led, smoothed: it keeps it when let go
            c.vx = 0.6 * c.vx + 0.4 * (c.x - c.last.0) / dt;
            c.vy = 0.6 * c.vy + 0.4 * (c.y - c.last.1) / dt;
        }
        c.last = (c.x, c.y);
        let r = self.carry_radius(c);
        if !c.held && !c.rising {
            // Drifting on after a fling, slowing down in the water
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            let drag = (-1.6 * dt).exp();
            c.vx *= drag;
            c.vy *= drag;
            // Bounces off the very edges of the screen (and the surface),
            // losing a little speed each time
            let (left, right) = (r, (self.width - r).max(r));
            let top = self.water_top() + r * 0.6;
            let bottom = (self.height - r).max(top);
            const BOUNCE: f32 = 0.75;
            if c.x < left || c.x > right {
                c.x = c.x.clamp(left, right);
                c.vx = -c.vx * BOUNCE;
            }
            if c.y < top || c.y > bottom {
                c.y = c.y.clamp(top, bottom);
                c.vy = -c.vy * BOUNCE;
            }
            // Thrown and its momentum spent: pop
            if c.flung && c.vx.hypot(c.vy) < SPENT_SPEED {
                return true;
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
            c.x += (self.time * 2.0 + c.id as f32).sin() * 12.0 * dt;
            if c.y - r <= self.water_top() + 4.0 || c.y < 0.0 {
                return true;
            }
        }
        // What it held went away (to another screen, out of the library)
        let gone = match (c.fish, c.crawler) {
            (Some(id), false) => !self.fish.iter().any(|f| f.id == id),
            (Some(id), true) => !self.crawlers.iter().any(|k| k.id == id),
            _ => false,
        };
        if gone {
            c.fish = None;
            c.holding = false;
        }
        if c.fish.is_none() {
            // Waiting by the nearest fish or crawler that could come (and isn't
            // in another bubble)
            let fish = self
                .fish
                .iter()
                .filter(|f| {
                    !f.entering && f.travel == 0 && !f.arriving && !matches!(f.mode, Mode::Bubbled)
                })
                .map(|f| (false, f.id, (f.x - c.x).hypot(f.y - c.y), f.trust));
            let crawlers = self
                .crawlers
                .iter()
                .filter(|k| !k.carried && !k.falling)
                .map(|k| (true, k.id, (k.x - c.x).hypot(k.y - k.h * 0.5 - c.y), 0.5));
            let nearest = fish
                .chain(crawlers)
                .filter(|&(_, _, d, _)| d < INVITE_RADIUS)
                .min_by(|a, b| a.2.total_cmp(&b.2));
            match nearest {
                Some((crawler, id, _, _)) => {
                    if c.candidate == Some((crawler, id)) {
                        c.linger += dt;
                    } else {
                        c.candidate = Some((crawler, id));
                        c.linger = 0.0;
                    }
                    if c.linger >= INVITE_SECS {
                        c.fish = Some(id);
                        c.crawler = crawler;
                        if crawler {
                            if let Some(k) = self.crawlers.iter_mut().find(|k| k.id == id) {
                                k.carried = true;
                            }
                        } else if let Some(f) = self.fish.iter_mut().find(|f| f.id == id) {
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
        // A crawler can't swim: it floats up into the bubble, then rides in it
        if let (Some(id), true) = (c.fish, c.crawler) {
            let (tx, ty) = (
                c.x.clamp(0.0, self.width),
                c.y.clamp(self.water_top(), self.height),
            );
            if let Some(k) = self.crawlers.iter_mut().find(|k| k.id == id) {
                let (cx, cy) = (k.x, k.y - k.h * 0.5);
                let (dx, dy) = (tx - cx, ty - cy);
                let dist = dx.hypot(dy);
                if c.holding || dist < 15.0 {
                    c.holding = true;
                    k.x = tx;
                    k.y = ty + k.h * 0.5 + (self.time * 1.3 + k.phase).sin() * 3.0;
                } else {
                    let step = (320.0 * dt).min(dist);
                    k.x += dx / dist * step;
                    k.y += dy / dist * step;
                }
                k.tilt *= 0.9;
                k.hop = 0.0;
            }
        }
        false
    }

    /// In (or swimming into) a bubble: calm, follows it, now and then a heart
    pub(super) fn step_bubbled(&mut self, f: &mut Fish, dt: f32) {
        let Some(i) = self.carries.iter().position(|c| c.fish == Some(f.id)) else {
            f.mode = Mode::Wander;
            f.retarget = true;
            return;
        };
        let c = self.carries[i];
        // Where it can get to: the bubble, kept inside the water
        let tx =
            c.x.clamp(f.w * 0.5, (self.width - f.w * 0.5).max(f.w * 0.5));
        // (in a bubble it may go down over the sand, to the very bottom)
        let ty = c.y.clamp(
            self.water_top() + f.h * 0.5,
            (self.height - f.h * 0.5).max(self.water_top() + f.h),
        );
        let (dx, dy) = (tx - f.x, ty - f.y);
        let dist = dx.hypot(dy);
        // Brought to the front of the tank, so nobody swims across it
        f.depth = (f.depth - dt * 0.8).max(0.0);
        if c.holding {
            // Rides exactly in the middle (no lag, or a thrown bubble would
            // seem to turn back before it reaches the edge), swaying a little
            let sway = (self.time * 1.3 + f.phase * 0.02).sin() * 3.0;
            f.vx = dx / dt.max(1e-3);
            f.vy = dy / dt.max(1e-3);
            f.x = tx;
            f.y = ty + sway;
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
            // Swims in quickly, slowing only right at the end
            let speed = (f.max_speed * 2.5).max(260.0).min(dist * 8.0);
            f.vx = dx / dist.max(1.0) * speed;
            f.vy = dy / dist.max(1.0) * speed;
            f.x += f.vx * dt;
            f.y += f.vy * dt;
            f.phase = (f.phase + dt * (2.5 + speed * 0.07)) % (std::f32::consts::TAU * 64.0);
            if dist < 15.0 {
                self.carries[i].holding = true;
            }
        }
        // Face the way it is going; inside, keep facing
        if f.vx.abs() > 8.0 {
            f.dir = f.vx.signum();
        }
        f.facing += (f.dir - f.facing).clamp(-6.0 * dt, 6.0 * dt);
        f.speed = f.vx.hypot(f.vy);
        f.y = f.y.clamp(
            self.water_top() + f.h * 0.5,
            (self.height - f.h * 0.5).max(self.water_top() + f.h),
        );
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

    fn get(s: &Sim, id: u32) -> Option<Carry> {
        s.carries().iter().find(|c| c.id == id).copied()
    }

    #[test]
    fn a_fish_swims_into_the_bubble_and_is_carried() {
        // Even a shy fish comes along (it just takes a moment)
        let mut s = one_fish(Character::Shy);
        let (fx, fy) = (s.fish[0].x, s.fish[0].y);
        // Beside it, on the side with room
        let (bx, by) = (if fx > 960.0 { fx - 120.0 } else { fx + 120.0 }, fy);
        let id = s.carry_new(bx, by);
        for _ in 0..30 * 3 {
            s.carry_to(id, bx, by);
            s.step(DT, Some((bx, by)));
        }
        let c = get(&s, id).unwrap();
        assert_eq!(c.fish, Some(5), "the fish should have come into the bubble");
        assert!(c.holding, "it should be inside by now");
        // Carry it across the tank
        for k in 0..30 * 4 {
            let x = 1500.0 - k as f32 * 6.0;
            s.carry_to(id, x, 400.0);
            s.step(DT, Some((x, 400.0)));
        }
        let f = &s.fish()[0];
        let c = get(&s, id).unwrap();
        assert!(
            (f.x - c.x).hypot(f.y - c.y) < 20.0,
            "the fish should float in the bubble"
        );
        assert!(f.flee_left == 0.0, "nobody is frightened");
        s.carry_end(id);
        assert!(get(&s, id).is_none());
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
        let id = s.carry_new(bx, fy);
        for _ in 0..30 * 3 {
            s.carry_to(id, bx, fy);
            s.step(DT, None);
        }
        assert!(get(&s, id).unwrap().holding);
        s.carry_release(id);
        for _ in 0..30 * 25 {
            s.step(DT, None);
            if get(&s, id).is_none() {
                break;
            }
        }
        assert!(
            get(&s, id).is_none(),
            "it should have popped at the surface"
        );
        let f = &s.fish()[0];
        assert!(f.y < 1080.0 * 0.3, "the fish rode up with it (y {})", f.y);
        assert!(!matches!(f.mode, Mode::Bubbled));
    }

    #[test]
    fn a_flung_bubble_keeps_going_and_bounces_in_every_direction() {
        for (dx, dy) in [(40.0f32, 0.0f32), (0.0, -40.0), (0.0, 40.0), (-30.0, 30.0)] {
            let mut s = one_fish(Character::Explorer);
            // Keep the fish out of the way: an empty bubble
            s.fish[0].x = 100.0;
            s.fish[0].y = 1000.0;
            let id = s.carry_new(960.0, 520.0);
            for k in 0..8 {
                s.carry_to(id, 960.0 + k as f32 * dx, 520.0 + k as f32 * dy);
                s.step(DT, None);
            }
            s.carry_release(id);
            let start = get(&s, id).unwrap();
            let (mut bounced, mut travelled, mut reached) = (false, 0.0f32, false);
            let mut last = (start.x, start.y);
            for _ in 0..30 * 2 {
                s.step(DT, None);
                let c = get(&s, id).expect("a fling must not pop it");
                travelled += (c.x - last.0).hypot(c.y - last.1);
                // Coming back along either axis it was flung along
                bounced |= (c.x - last.0) * dx < -1.0 || (c.y - last.1) * dy < -1.0;
                last = (c.x, c.y);
                let r = s.carry_radius(&c);
                reached |= c.x + r >= 1919.0
                    || c.x - r <= 1.0
                    || c.y + r >= 1079.0
                    || c.y - r * 0.6 <= s.water_top() + 1.0;
                assert!(
                    c.y - r * 0.6 >= s.water_top() - 1.0,
                    "stays (mostly) under the surface"
                );
                assert!(
                    c.x - r >= -1.0 && c.x + r <= 1921.0,
                    "stays inside the sides"
                );
            }
            assert!(
                travelled > 150.0,
                "flung {dx},{dy}: it should sail on ({travelled:.0}px)"
            );
            assert!(
                reached,
                "flung {dx},{dy}: it should go all the way to the edge before bouncing"
            );
            // Spent: it pops
            for _ in 0..30 * 3 {
                s.step(DT, None);
            }
            assert!(
                get(&s, id).is_none(),
                "flung {dx},{dy}: it should pop once its momentum is spent"
            );
            assert!(
                bounced,
                "flung {dx},{dy}: it should bounce back off the edge"
            );
        }
    }

    #[test]
    fn a_bubble_with_a_fish_reaches_the_edge_too() {
        let mut s = one_fish(Character::Explorer);
        let (fx, fy) = (s.fish[0].x, s.fish[0].y);
        let bx = if fx > 960.0 { fx - 120.0 } else { fx + 120.0 };
        let id = s.carry_new(bx, fy);
        for _ in 0..30 * 3 {
            s.carry_to(id, bx, fy);
            s.step(DT, None);
        }
        assert!(get(&s, id).unwrap().holding);
        // Thrown hard to the right
        for k in 0..8 {
            s.carry_to(id, 900.0 + k as f32 * 45.0, 500.0);
            s.step(DT, None);
        }
        s.carry_release(id);
        let mut furthest = 0.0f32;
        for _ in 0..30 * 2 {
            s.step(DT, None);
            if let Some(c) = get(&s, id) {
                let (x, _, r) = s.bubble_of(&c);
                furthest = furthest.max(x + r);
            }
        }
        assert!(
            furthest >= 1915.0,
            "the bubble it is seen in turned back at {furthest:.0}px, not at the edge"
        );
    }

    #[test]
    fn several_bubbles_each_carry_their_own_fish() {
        let mut s = Sim::new(9);
        let specs: Vec<FishSpec> = (0..2u64)
            .map(|k| FishSpec {
                id: 20 + k,
                sprite: 0,
                aspect: 0.6,
                scale: 1.0,
                adult: true,
                mouth: DEFAULT_MOUTH,
                body: Body::Fish,
            })
            .collect();
        s.set_population(&specs, false);
        s.resize(1920.0, 1080.0);
        s.fish[0].x = 400.0;
        s.fish[0].y = 500.0;
        s.fish[1].x = 1500.0;
        s.fish[1].y = 500.0;
        let a = s.carry_new(500.0, 500.0);
        let b = s.carry_new(1400.0, 500.0);
        for _ in 0..30 * 3 {
            s.carry_to(a, 500.0, 500.0);
            s.carry_to(b, 1400.0, 500.0);
            s.step(DT, None);
        }
        let (ca, cb) = (get(&s, a).unwrap(), get(&s, b).unwrap());
        assert!(
            ca.fish.is_some() && cb.fish.is_some(),
            "both bubbles have a fish"
        );
        assert_ne!(ca.fish, cb.fish, "each its own");
    }

    #[test]
    fn a_crab_rides_in_a_bubble_and_drops_back_down() {
        let mut s = Sim::new(4);
        s.resize(1920.0, 1080.0);
        s.set_crawlers(&[crate::sim::CrawlerSpec {
            id: 900,
            sprite: 0,
            aspect: 0.6,
            scale: 1.0,
            gait: crate::sim::Gait::Crab,
        }]);
        let (kx, ky) = (s.crawlers()[0].x, s.crawlers()[0].y);
        let id = s.carry_new(kx, ky - 80.0);
        for _ in 0..30 * 3 {
            let x = s.crawlers()[0].x;
            s.carry_to(id, x, ky - 80.0);
            s.step(DT, None);
        }
        let c = get(&s, id).unwrap();
        assert!(
            c.crawler && c.fish == Some(900) && c.holding,
            "the crab should be in the bubble: {c:?}"
        );
        // Carried up into the water
        for _ in 0..30 {
            s.carry_to(id, 900.0, 400.0);
            s.step(DT, None);
        }
        assert!(
            s.crawlers()[0].y < 600.0,
            "the crab rides up with the bubble (y {})",
            s.crawlers()[0].y
        );
        s.carry_end(id);
        for _ in 0..30 * 3 {
            s.step(DT, None);
        }
        let k = &s.crawlers()[0];
        assert!(
            k.y > 1080.0 * 0.85,
            "it drops back down to the sand (y {})",
            k.y
        );
    }

    #[test]
    fn a_bubble_far_from_fish_stays_empty() {
        let mut s = one_fish(Character::Explorer);
        let (fx, fy) = (s.fish[0].x, s.fish[0].y);
        let (bx, by) = (if fx > 960.0 { 100.0 } else { 1800.0 }, fy);
        let id = s.carry_new(bx, by);
        for _ in 0..30 * 3 {
            s.carry_to(id, bx, by);
            s.step(DT, Some((bx, by)));
        }
        assert_eq!(get(&s, id).unwrap().fish, None);
    }
}
