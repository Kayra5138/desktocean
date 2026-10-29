//! Creatures that walk on the bottom: crabs, snails, starfish, shrimp.
//!
//! They live on the sand and climb over rocks (the host passes the height of
//! the ground across the tank), eat food that has sunk to the bottom, and hide
//! when the cursor lingers next to them: crabs and shrimp dig in, snails pull
//! into their shell.

use super::{Sim, depth_of_floor};

/// How a bottom creature moves.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Gait {
    /// Quick sideways dashes, then a pause; never turns round
    Crab,
    /// A slow, steady glide; turns to face where it goes
    Snail,
    /// Barely moves, slowly rocks
    Star,
    /// Walks forward on little legs, stops to look around
    Walker,
}

impl Gait {
    pub fn from_index(i: u32) -> Self {
        match i {
            1 => Gait::Snail,
            2 => Gait::Star,
            3 => Gait::Walker,
            _ => Gait::Crab,
        }
    }

    pub fn index(self) -> u32 {
        match self {
            Gait::Crab => 0,
            Gait::Snail => 1,
            Gait::Star => 2,
            Gait::Walker => 3,
        }
    }

    /// (walking speed, px/s at 1080p, walk seconds, rest seconds)
    fn pace(self) -> (f32, (f32, f32), (f32, f32)) {
        match self {
            Gait::Crab => (110.0, (0.4, 1.4), (0.8, 4.0)),
            Gait::Snail => (9.0, (15.0, 45.0), (8.0, 30.0)),
            Gait::Star => (3.5, (8.0, 20.0), (30.0, 90.0)),
            Gait::Walker => (40.0, (1.5, 5.0), (1.5, 6.0)),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct CrawlerSpec {
    pub id: u64,
    pub sprite: u32,
    /// Sprite height / width
    pub aspect: f32,
    pub scale: f32,
    pub gait: Gait,
}

#[derive(Debug, Clone)]
pub struct Crawler {
    pub id: u64,
    pub sprite: u32,
    pub gait: Gait,
    /// Middle of the bottom edge: where it stands
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
    /// -1 left .. 1 right (crabs always face the viewer: 1)
    pub facing: f32,
    /// Leg cycle, advances with the distance walked
    pub phase: f32,
    /// Degrees, follows the slope it stands on
    pub tilt: f32,
    /// Unused (crawlers used to dig in); always 0
    pub buried: f32,
    /// Lifted off the ground while hopping about in play, scene pixels
    pub hop: f32,
    /// 0 near .. 1 far: from how far back on the sand it walks
    pub depth: f32,
    /// Riding in a bubble from the wand (see carry.rs)
    pub(super) carried: bool,
    /// Dropped from a bubble: falling back to the ground
    pub(super) falling: bool,
    pub(super) fall_v: f32,
    /// How far back on the sand it walks (scene y of its feet on flat sand)
    lane: f32,
    lane_target: f32,
    dir: f32,
    walking: bool,
    left: f32,
    speed: f32,
    cursor_near: f32,
    /// Playing with the cursor: seconds left, time into it, rest before the next game
    play_left: f32,
    play_t: f32,
    play_cooldown: f32,
    /// Pellet it is heading for
    food: Option<usize>,
}

/// Crawlers this close to a cursor that lingers for a moment come and play.
const PLAY_RADIUS: f32 = 190.0;

impl Sim {
    /// Replaces the bottom creatures; ones with the same id keep their place
    pub fn set_crawlers(&mut self, specs: &[CrawlerSpec]) {
        let old = std::mem::take(&mut self.crawlers);
        for spec in specs {
            let c = match old.iter().find(|c| c.id == spec.id) {
                Some(c) => {
                    let mut c = c.clone();
                    c.sprite = spec.sprite;
                    c.gait = spec.gait;
                    self.size_crawler(&mut c, spec);
                    c
                }
                None => self.spawn_crawler(spec),
            };
            self.crawlers.push(c);
        }
        self.crawler_specs = specs.to_vec();
    }

    pub fn crawlers(&self) -> &[Crawler] {
        &self.crawlers
    }

    /// Height of the ground (scene y) every `step` px from x = 0: rocks and
    /// other decorations crawlers can climb. Empty = flat sand.
    /// `depths` (same length, or empty) says how far back the decoration
    /// making each column is: crawlers only climb what is at about their depth.
    pub fn set_ground(&mut self, heights: &[f32], depths: &[f32], step: f32) {
        self.ground = heights.to_vec();
        self.ground_depth = if depths.len() == heights.len() {
            depths.to_vec()
        } else {
            Vec::new()
        };
        self.ground_step = step.max(1.0);
    }

    fn size_crawler(&self, c: &mut Crawler, spec: &CrawlerSpec) {
        let base = match spec.gait {
            Gait::Crab => 120.0,
            Gait::Snail => 95.0,
            Gait::Star => 85.0,
            Gait::Walker => 130.0,
        };
        let screen = (self.height / 1080.0).clamp(0.6, 2.0);
        c.w = base * spec.scale.clamp(0.3, 3.0) * screen;
        c.h = c.w * spec.aspect.clamp(0.1, 5.0);
    }

    fn spawn_crawler(&mut self, spec: &CrawlerSpec) -> Crawler {
        let lane = self.random_lane();
        let mut c = Crawler {
            id: spec.id,
            sprite: spec.sprite,
            gait: spec.gait,
            x: self.rng.range(0.1, 0.9) * self.width.max(1.0),
            y: lane,
            w: 0.0,
            h: 0.0,
            facing: 1.0,
            phase: self.rng.range(0.0, 6.0),
            tilt: 0.0,
            buried: 0.0,
            hop: 0.0,
            depth: 0.5,
            carried: false,
            falling: false,
            fall_v: 0.0,
            lane,
            lane_target: lane,
            dir: if self.rng.chance(0.5) { 1.0 } else { -1.0 },
            walking: false,
            left: self.rng.range(0.5, 5.0),
            speed: 0.0,
            cursor_near: 0.0,
            play_left: 0.0,
            play_t: 0.0,
            play_cooldown: 0.0,
            food: None,
        };
        self.size_crawler(&mut c, spec);
        if spec.gait != Gait::Crab {
            c.facing = c.dir;
        }
        c
    }

    /// Somewhere on the sand between its back edge and the front of the tank
    fn random_lane(&mut self) -> f32 {
        self.height * self.rng.range(0.89, 0.975)
    }

    /// The ground under x: the sand at `lane`, or a rock's top if that is higher
    fn ground_at(&self, x: f32, lane: f32, depth: f32) -> f32 {
        if self.ground.is_empty() {
            return lane;
        }
        let i = (x / self.ground_step)
            .round()
            .clamp(0.0, (self.ground.len() - 1) as f32) as usize;
        // A rock well in front of or behind the crawler isn't in its way
        if self
            .ground_depth
            .get(i)
            .is_some_and(|d| (d - depth).abs() > 0.25)
        {
            return lane;
        }
        self.ground[i].min(lane)
    }

    pub(super) fn update_crawlers(&mut self, dt: f32, pointer: Option<(f32, f32)>) {
        let screen = (self.height / 1080.0).clamp(0.6, 2.0);
        let night = 1.0 - self.daylight;
        for k in 0..self.crawlers.len() {
            let mut c = self.crawlers[k].clone();
            // In a bubble: the bubble moves it (carry.rs)
            if c.carried {
                continue;
            }
            // Dropped from a bubble: falls to the ground below, then carries on
            if c.falling {
                let ground = self.ground_at(c.x, c.lane, c.depth);
                c.fall_v += 900.0 * dt;
                c.y += c.fall_v * dt;
                if c.y >= ground {
                    c.y = ground;
                    c.falling = false;
                    c.fall_v = 0.0;
                }
                self.crawlers[k] = c;
                continue;
            }
            let (pace, walk_secs, rest_secs) = c.gait.pace();
            // Crabs and shrimp come out more at night
            let lively = match c.gait {
                Gait::Crab | Gait::Walker => 1.0 + 0.4 * night,
                _ => 1.0,
            };

            // A cursor that lingers close by: they come and play with it
            let cursor =
                pointer.filter(|&(px, py)| (px - c.x).hypot(py - (c.y - c.h * 0.5)) < PLAY_RADIUS);
            c.cursor_near = if cursor.is_some() {
                c.cursor_near + dt
            } else {
                (c.cursor_near - dt).max(0.0)
            };
            c.play_cooldown = (c.play_cooldown - dt).max(0.0);
            if c.cursor_near > 0.5 && c.play_left <= 0.0 && c.play_cooldown == 0.0 {
                c.play_left = self.rng.range(3.0, 5.0);
                c.play_t = 0.0;
            }
            let playing = c.play_left > 0.0;
            if playing {
                c.play_left -= dt;
                c.play_t += dt;
                if c.play_left <= 0.0 {
                    c.play_cooldown = self.rng.range(3.0, 6.0);
                }
            }

            // Food on the bottom
            if c.food
                .is_some_and(|j| j >= self.food.len() || self.food[j].eaten || !self.food[j].landed)
            {
                c.food = None;
            }
            if c.food.is_none() && !playing {
                let reach = match c.gait {
                    Gait::Snail | Gait::Star => 260.0,
                    _ => 650.0,
                } * screen;
                c.food = self
                    .food
                    .iter()
                    .enumerate()
                    .filter(|(_, p)| p.landed && !p.eaten && (p.x - c.x).abs() < reach)
                    .filter(|(j, _)| {
                        !self
                            .crawlers
                            .iter()
                            .any(|o| o.id != c.id && o.food == Some(*j))
                    })
                    .min_by(|a, b| (a.1.x - c.x).abs().total_cmp(&(b.1.x - c.x).abs()))
                    .map(|(j, _)| j);
            }

            let mut want = 0.0;
            let mut hop = 0.0;
            if playing {
                let t = c.play_t;
                match c.gait {
                    // Little hops from side to side
                    Gait::Crab | Gait::Walker => {
                        let high = if c.gait == Gait::Crab { 0.22 } else { 0.14 };
                        hop = (t * 8.0).sin().abs() * c.h * high;
                        c.dir = if (t * 2.5) as i32 % 2 == 0 { 1.0 } else { -1.0 };
                        want = pace * screen * 0.5;
                    }
                    // Turns to the cursor and comes over, bobbing happily
                    Gait::Snail => {
                        if let Some((px, _)) = cursor {
                            c.dir = (px - c.x).signum();
                            if (px - c.x).abs() > c.w * 0.4 {
                                want = pace * screen * 2.0;
                            }
                        }
                        hop = (t * 2.5).sin().abs() * c.h * 0.05;
                    }
                    // A starfish wiggles where it is
                    Gait::Star => {}
                }
                c.walking = want > 0.0;
            } else if let Some(j) = c.food {
                let (px, py) = (self.food[j].x, self.food[j].y);
                c.lane_target = py.max(self.sand_top() + 2.0);
                let dx = px - c.x;
                if dx.abs() < c.w * 0.25 && (c.lane - c.lane_target).abs() < 20.0 {
                    // Got it: a short pause to eat
                    self.food[j].eaten = true;
                    c.food = None;
                    c.walking = false;
                    c.left = self.rng.range(1.0, 2.0);
                    c.lane_target = self.random_lane();
                } else {
                    c.dir = dx.signum();
                    want = pace * screen * lively * if c.gait == Gait::Crab { 0.8 } else { 1.2 };
                    c.walking = true;
                }
            } else {
                c.left -= dt * lively;
                if c.left <= 0.0 {
                    c.walking = !c.walking;
                    let (lo, hi) = if c.walking { walk_secs } else { rest_secs };
                    c.left = self.rng.range(lo, hi);
                    if c.walking {
                        // Mostly keep going; sometimes turn round
                        if self.rng.chance(0.35) {
                            c.dir = -c.dir;
                        }
                        if self.rng.chance(0.2) {
                            c.lane_target = self.random_lane();
                        }
                    }
                }
                if c.walking {
                    want = pace * screen * lively * self.rng.range(0.9, 1.1);
                }
            }

            // Don't walk into another crawler on the same stretch of sand
            if want > 0.0 {
                let blocked = self.crawlers.iter().any(|o| {
                    o.id != c.id
                        && (o.lane - c.lane).abs() < (o.h + c.h) * 0.3
                        && (o.x - c.x) * c.dir > 0.0
                        && (o.x - c.x).abs() < (o.w + c.w) * 0.45
                });
                if blocked {
                    c.dir = -c.dir;
                }
            }

            // Ease into the walking speed; edges turn it round
            c.speed += (want - c.speed) * (dt * 6.0).min(1.0);
            c.x += c.dir * c.speed * dt;
            let (lo, hi) = (c.w * 0.5, (self.width - c.w * 0.5).max(c.w * 0.5));
            if c.x < lo || c.x > hi {
                c.x = c.x.clamp(lo, hi);
                c.dir = -c.dir;
            }
            // Wander forward or back on the sand slowly
            c.lane += (c.lane_target - c.lane).clamp(-25.0 * dt * screen, 25.0 * dt * screen);
            c.phase = (c.phase + c.speed * dt * 0.12 / screen) % 1000.0;

            c.depth = depth_of_floor(c.lane / self.height.max(1.0));
            // Stand on the ground: the highest point under the middle of the
            // body, tilted to follow the slope
            let reach = c.w * 0.3;
            let (gl, gm, gr) = (
                self.ground_at(c.x - reach, c.lane, c.depth),
                self.ground_at(c.x, c.lane, c.depth),
                self.ground_at(c.x + reach, c.lane, c.depth),
            );
            let target_y = gm.min((gl + gr) * 0.5);
            // Climb at a crawl, drop a bit quicker
            let rate = if target_y < c.y { 120.0 } else { 260.0 } * screen;
            c.y += (target_y - c.y).clamp(-rate * dt, rate * dt);
            let slope = ((gr - gl) / (2.0 * reach))
                .atan()
                .to_degrees()
                .clamp(-35.0, 35.0);
            c.tilt += (slope - c.tilt) * (dt * 5.0).min(1.0);
            if c.gait == Gait::Star {
                // A slow rock, and a happy wiggle when playing
                c.tilt += (self.time * 0.3 + c.phase).sin() * 4.0 * dt;
                if playing {
                    c.tilt = slope + (c.play_t * 9.0).sin() * 14.0;
                }
            }
            // Hops ease in and out so they never jump
            c.hop += (hop - c.hop) * (dt * 20.0).min(1.0);
            c.buried = 0.0;

            if c.gait != Gait::Crab {
                let turn = if c.gait == Gait::Snail { 1.5 } else { 6.0 };
                c.facing += (c.dir - c.facing).clamp(-turn * dt, turn * dt);
            }
            self.crawlers[k] = c;
        }
    }

    pub(super) fn resize_crawlers(&mut self, sx: f32, sy: f32) {
        let specs = self.crawler_specs.clone();
        for c in &mut self.crawlers {
            c.x *= sx;
            c.y *= sy;
            c.lane *= sy;
            c.lane_target *= sy;
        }
        // Sizes follow the screen
        for spec in &specs {
            if let Some(mut c) = self.crawlers.iter().find(|c| c.id == spec.id).cloned() {
                self.size_crawler(&mut c, spec);
                if let Some(slot) = self.crawlers.iter_mut().find(|o| o.id == c.id) {
                    *slot = c;
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const DT: f32 = 1.0 / 30.0;

    fn tank(gaits: &[Gait]) -> Sim {
        let mut s = Sim::new(5);
        s.resize(1920.0, 1080.0);
        let specs: Vec<CrawlerSpec> = gaits
            .iter()
            .enumerate()
            .map(|(k, g)| CrawlerSpec {
                id: 500 + k as u64,
                sprite: k as u32,
                aspect: 0.6,
                scale: 1.0,
                gait: *g,
            })
            .collect();
        s.set_crawlers(&specs);
        s
    }

    #[test]
    fn crawlers_stay_on_the_bottom_and_move() {
        let mut s = tank(&[Gait::Crab, Gait::Snail, Gait::Walker, Gait::Star]);
        let mut walked = [0.0f32; 4];
        for _ in 0..30 * 120 {
            let before: Vec<f32> = s.crawlers().iter().map(|c| c.x).collect();
            s.step(DT, None);
            for (w, (c, b)) in walked.iter_mut().zip(s.crawlers().iter().zip(&before)) {
                *w += (c.x - b).abs();
            }
            for c in s.crawlers() {
                assert!(
                    c.y > 1080.0 * 0.86 && c.y <= 1080.0,
                    "{:?} left the sand at y {}",
                    c.gait,
                    c.y
                );
                assert!(c.x >= 0.0 && c.x <= 1920.0);
            }
        }
        assert!(walked[0] > 1000.0, "the crab hardly moved: {walked:?}");
        assert!(walked[2] > 500.0, "the shrimp hardly moved: {walked:?}");
        assert!(
            walked[1] > 30.0 && walked[1] < walked[2],
            "the snail should creep: {walked:?}"
        );
        assert!(
            walked[3] < walked[1],
            "the starfish should barely move: {walked:?}"
        );
        let crab = &s.crawlers()[0];
        assert_eq!(
            crab.facing, 1.0,
            "crabs walk sideways, they don't turn round"
        );
    }

    #[test]
    fn crawlers_climb_rocks() {
        let mut s = tank(&[Gait::Crab]);
        // A rock 200 px tall between x = 800 and 1200
        let ground: Vec<f32> = (0..=240)
            .map(|i| {
                let x = i as f32 * 8.0;
                if (800.0..1200.0).contains(&x) {
                    1080.0 * 0.97 - 200.0
                } else {
                    2000.0
                }
            })
            .collect();
        s.set_ground(&ground, &[], 8.0);
        s.crawlers[0].x = 1000.0;
        let mut highest = f32::INFINITY;
        for _ in 0..30 * 5 {
            s.crawlers[0].x = 1000.0;
            s.step(DT, None);
            highest = highest.min(s.crawlers()[0].y);
        }
        assert!(
            highest < 1080.0 * 0.97 - 150.0,
            "the crab should stand on the rock (y {highest})"
        );
    }

    #[test]
    fn crawlers_eat_food_on_the_bottom() {
        let mut s = tank(&[Gait::Crab]);
        s.crawlers[0].x = 700.0;
        s.feed(Some(1000.0));
        let mut eaten_by_crab = false;
        for _ in 0..30 * 80 {
            // Food sinks slowly: keep the crab close by until some has landed
            if !s.food.iter().any(|p| p.landed) {
                s.crawlers[0].x = 800.0;
            }
            let (n, oldest) = (
                s.food.len(),
                s.food.iter().map(|p| p.age).fold(0.0, f32::max),
            );
            s.step(DT, None);
            // A pellet went before any could dissolve: eaten
            eaten_by_crab |= s.food.len() < n && oldest < 9.5;
        }
        assert!(eaten_by_crab, "the crab never ate a pellet from the bottom");
    }

    #[test]
    fn crawlers_play_with_a_lingering_cursor() {
        let mut s = tank(&[Gait::Crab, Gait::Snail]);
        s.crawlers[0].x = 600.0;
        s.crawlers[1].x = 1400.0;
        let (mut hopped, mut snail_came) = (0.0f32, false);
        for _ in 0..30 * 3 {
            s.crawlers[0].x = 600.0;
            let y = s.crawlers()[0].y - 20.0;
            s.step(DT, Some((620.0, y)));
            hopped = hopped.max(s.crawlers()[0].hop);
            assert_eq!(
                s.crawlers()[0].buried,
                0.0,
                "nobody digs in or fades any more"
            );
        }
        assert!(hopped > 5.0, "the crab should hop about ({hopped:.1}px)");
        // The snail turns to a cursor next to it and comes over
        let x0 = s.crawlers()[1].x;
        for _ in 0..30 * 4 {
            let y = s.crawlers()[1].y - 20.0;
            s.step(DT, Some((x0 + 150.0, y)));
            snail_came |= s.crawlers()[1].x > x0 + 5.0;
        }
        assert!(snail_came, "the snail should come toward the cursor");
        // Left alone, they settle back on the ground
        for _ in 0..30 * 8 {
            s.step(DT, None);
        }
        assert!(s.crawlers()[0].hop < 1.0);
    }
}
