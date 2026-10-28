use std::f32::consts::TAU;

use crate::rng::Rng;

/// Fraction of the height where the sand starts.
pub const SAND_TOP: f32 = 0.86;
/// Fraction of the height where the water surface is.
pub const WATER_TOP: f32 = 0.05;
/// Food lies on the sand this long before it dissolves.
const FOOD_LIFETIME: f32 = 10.0;
/// Curious fish lose interest once the cursor has been still this long.
const POINTER_INTEREST_SECS: f32 = 4.0;
/// The cursor counts as moved once it leaves a circle of this radius.
const POINTER_MOVE_RADIUS: f32 = 6.0;
/// A fish must want to go the other way this long before it turns around.
const TURN_INTENT_SECS: f32 = 0.25;
/// Seconds a fish keeps its heading after turning.
const TURN_COOLDOWN: f32 = 1.2;
/// Speed of the visual turn, in "facing units" per second (full turn = 2).
const TURN_RATE: f32 = 6.0;
/// Fastest a fish swims backwards, as a fraction of its top speed.
const BACKUP_SPEED: f32 = 0.3;
/// Neighbours within this distance (and similar depth) influence schooling.
const SCHOOL_RADIUS: f32 = 320.0;

/// Fixed traits rolled when a fish is created.
#[derive(Debug, Clone, Copy)]
pub struct Personality {
    /// Multiplier on top speed
    pub speed: f32,
    /// 0..1, how strongly it sticks to and aligns with nearby fish
    pub sociability: f32,
    /// 0..1, followers of the cursor are the ones above 0.55
    pub curiosity: f32,
    /// 0..1, how far it notices food and how fast it rushes for it
    pub appetite: f32,
    /// 0..1, low values linger and drift in place between trips
    pub restlessness: f32,
}

impl Personality {
    fn random(rng: &mut Rng) -> Self {
        Self {
            speed: rng.range(0.8, 1.2),
            sociability: rng.unit(),
            curiosity: rng.unit(),
            appetite: rng.range(0.2, 1.0),
            restlessness: rng.unit(),
        }
    }

    pub fn follows_cursor(&self) -> bool {
        self.curiosity >= 0.55
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
enum Mode {
    /// Swimming toward (tx, ty)
    Wander,
    /// Drifting in place for a while
    Idle { left: f32 },
}

#[derive(Debug, Clone)]
pub struct Fish {
    pub x: f32,
    pub y: f32,
    pub vx: f32,
    pub vy: f32,
    tx: f32,
    ty: f32,
    cruise: f32,
    mode: Mode,
    /// Set while chasing food or the cursor, so wandering re-plans afterwards
    retarget: bool,
    /// 0 = front, 1 = back; drives size, speed, fog and z-order
    pub depth: f32,
    pub w: f32,
    pub h: f32,
    pub max_speed: f32,
    /// Where the fish is heading: exactly -1 (left) or 1 (right)
    pub dir: f32,
    /// Visual orientation; eases toward `dir`, values between squash the sprite mid-turn
    pub facing: f32,
    turn_cooldown: f32,
    turn_intent: f32,
    /// Tail-wave phase, radians
    pub phase: f32,
    pub speed: f32,
    pub personality: Personality,
}

impl Fish {
    fn mouth(&self) -> (f32, f32) {
        (self.x + self.facing * self.w * 0.42, self.y + self.h * 0.06)
    }

    /// Tail swing as a fraction of body height; livelier when swimming fast.
    pub fn amplitude(&self) -> f32 {
        0.04 + 0.08 * (self.speed / self.max_speed).min(1.0)
    }

    /// Body pitch in degrees, nose following the vertical velocity.
    pub fn tilt(&self) -> f32 {
        let deg = self.vy.atan2(self.vx.abs() + 20.0).to_degrees() * 0.7;
        deg.clamp(-28.0, 28.0) * self.dir
    }
}

#[derive(Debug, Clone)]
pub struct Food {
    pub x: f32,
    pub y: f32,
    seed: f32,
    age: f32,
    landed: bool,
    eaten: bool,
}

impl Food {
    pub fn opacity(&self) -> f32 {
        if self.landed {
            (1.0 - self.age / FOOD_LIFETIME).max(0.0)
        } else {
            1.0
        }
    }
}

/// Per-step copy of what neighbours need to know about each other.
#[derive(Clone, Copy)]
struct Neighbour {
    x: f32,
    y: f32,
    vx: f32,
    vy: f32,
    w: f32,
    depth: f32,
}

#[derive(Debug)]
pub struct Sim {
    width: f32,
    height: f32,
    fish_count: usize,
    fish: Vec<Fish>,
    food: Vec<Food>,
    time: f32,
    rng: Rng,
    pointer_anchor: Option<(f32, f32)>,
    pointer_still_for: f32,
}

impl Sim {
    pub fn new(seed: u64) -> Self {
        Self {
            width: 0.0,
            height: 0.0,
            fish_count: 7,
            fish: Vec::new(),
            food: Vec::new(),
            time: 0.0,
            rng: Rng::new(seed),
            pointer_anchor: None,
            pointer_still_for: f32::INFINITY,
        }
    }

    pub fn fish(&self) -> &[Fish] {
        &self.fish
    }

    pub fn food(&self) -> &[Food] {
        &self.food
    }

    /// Whether curious fish are currently following the cursor.
    pub fn lure_active(&self) -> bool {
        self.pointer_anchor.is_some() && self.pointer_still_for < POINTER_INTEREST_SECS
    }

    pub fn clear_food(&mut self) {
        self.food.clear();
    }

    fn water_top(&self) -> f32 {
        self.height * WATER_TOP
    }

    fn sand_top(&self) -> f32 {
        self.height * SAND_TOP
    }

    fn has_size(&self) -> bool {
        self.width > 1.0 && self.height > 1.0
    }

    pub fn resize(&mut self, width: f32, height: f32) {
        if width == self.width && height == self.height {
            return;
        }
        self.width = width;
        self.height = height;
        if !self.has_size() {
            return;
        }
        if self.fish.len() != self.fish_count {
            self.reset_fish();
        } else {
            // Keep the school, just bring everyone back inside and re-plan.
            for i in 0..self.fish.len() {
                let mut f = self.fish[i].clone();
                self.clamp_to_water(&mut f);
                self.pick_target(&mut f);
                self.fish[i] = f;
            }
        }
    }

    pub fn set_fish_count(&mut self, count: usize) {
        self.fish_count = count;
        if self.has_size() {
            self.reset_fish();
        }
    }

    /// Picks the next wander target, usually ahead so fish don't keep turning.
    fn pick_target(&mut self, f: &mut Fish) {
        let (wt, st) = (self.water_top(), self.sand_top());
        let (lo, hi) = (f.w * 0.6, (self.width - f.w * 0.6).max(f.w * 0.6 + 1.0));
        let span = self.rng.range(0.15, 0.5) * self.width;
        let ahead = if self.rng.chance(0.8) { 1.0 } else { -1.0 };
        let mut tx = f.x + f.dir * ahead * span;
        if !(lo..=hi).contains(&tx) {
            // Would leave the tank: head the other way instead.
            tx = f.x - f.dir * ahead * span;
        }
        f.tx = tx.clamp(lo, hi);

        let (top, bottom) = (wt + f.h, (st - f.h * 0.7).max(wt + f.h + 1.0));
        f.ty = (f.y + self.rng.range(-0.25, 0.25) * self.height).clamp(top, bottom);
        f.cruise = self.rng.range(0.45, 1.0) * f.max_speed;
        f.mode = Mode::Wander;
    }

    fn clamp_to_water(&self, f: &mut Fish) {
        let half = f.w * 0.5;
        f.x = f.x.clamp(half, (self.width - half).max(half));
        let top = self.water_top() + f.h * 0.5;
        f.y = f.y.clamp(top, (self.sand_top() - f.h * 0.3).max(top));
    }

    fn reset_fish(&mut self) {
        let mut list = Vec::with_capacity(self.fish_count);
        for _ in 0..self.fish_count {
            let depth = self.rng.unit();
            let w = self.rng.range(150.0, 230.0) * (1.0 - 0.45 * depth);
            let dir = if self.rng.chance(0.5) { -1.0 } else { 1.0 };
            let personality = Personality::random(&mut self.rng);
            let mut f = Fish {
                x: self.rng.range(0.0, self.width),
                y: self.rng.range(self.water_top(), self.sand_top()),
                vx: 0.0,
                vy: 0.0,
                tx: 0.0,
                ty: 0.0,
                cruise: 0.0,
                mode: Mode::Wander,
                retarget: false,
                depth,
                w,
                h: w * 0.6,
                max_speed: self.rng.range(60.0, 115.0) * (1.0 - 0.3 * depth) * personality.speed,
                dir,
                facing: dir,
                turn_cooldown: 0.0,
                turn_intent: 0.0,
                phase: self.rng.range(0.0, TAU),
                speed: 0.0,
                personality,
            };
            self.clamp_to_water(&mut f);
            self.pick_target(&mut f);
            list.push(f);
        }
        // Someone should always be curious about the cursor.
        if !list_has_follower(&list)
            && let Some(first) = list.first_mut()
        {
            first.personality.curiosity = 0.8;
        }
        self.fish = list;
    }

    /// Drops a handful of food from the surface above `x`, or somewhere random.
    pub fn feed(&mut self, x: Option<f32>) {
        if !self.has_size() {
            return;
        }
        let cx = match x {
            Some(x) => x.clamp(0.0, self.width),
            None => self.rng.range(self.width * 0.15, self.width * 0.85),
        };
        let wt = self.water_top();
        for _ in 0..8 {
            let food = Food {
                x: cx + self.rng.range(-90.0, 90.0),
                y: wt + self.rng.range(-10.0, 30.0),
                seed: self.rng.range(0.0, TAU),
                age: 0.0,
                landed: false,
                eaten: false,
            };
            self.food.push(food);
        }
    }

    fn update_pointer(&mut self, dt: f32, pointer: Option<(f32, f32)>) {
        // The cursor only counts as interesting while it moves. Movement is
        // measured from an anchor rather than frame to frame, so slow drags count.
        match (pointer, self.pointer_anchor) {
            (Some((x, y)), Some((ax, ay))) if (x - ax).hypot(y - ay) < POINTER_MOVE_RADIUS => {
                self.pointer_still_for += dt
            }
            (Some(p), _) => {
                self.pointer_anchor = Some(p);
                self.pointer_still_for = 0.0;
            }
            (None, _) => {
                self.pointer_anchor = None;
                self.pointer_still_for = f32::INFINITY;
            }
        }
    }

    fn update_food(&mut self, dt: f32) {
        let (time, st) = (self.time, self.sand_top());
        for p in &mut self.food {
            if !p.landed {
                p.y += 38.0 * dt;
                p.x += (time * 2.2 + p.seed).sin() * 10.0 * dt;
                if p.y >= st + 4.0 {
                    p.y = st + 4.0;
                    p.landed = true;
                }
            } else {
                p.age += dt;
            }
        }
    }

    /// Advances the world by `dt` seconds. `pointer` is the cursor in scene
    /// coordinates when it is over the aquarium.
    pub fn step(&mut self, dt: f32, pointer: Option<(f32, f32)>) {
        if !self.has_size() {
            return;
        }
        let dt = dt.clamp(0.0, 0.1);
        self.time += dt;
        self.update_pointer(dt, pointer);
        self.update_food(dt);

        let (wt, st) = (self.water_top(), self.sand_top());
        let lure = pointer.filter(|&(_, y)| self.lure_active() && y > wt && y < st);

        let others: Vec<Neighbour> = self
            .fish
            .iter()
            .map(|g| Neighbour {
                x: g.x,
                y: g.y,
                vx: g.vx,
                vy: g.vy,
                w: g.w,
                depth: g.depth,
            })
            .collect();

        for i in 0..self.fish.len() {
            let mut f = self.fish[i].clone();
            self.step_fish(&mut f, i, &others, lure, dt);
            self.fish[i] = f;
        }

        self.food.retain(|p| !p.eaten && p.age < FOOD_LIFETIME);
    }

    fn step_fish(
        &mut self,
        f: &mut Fish,
        index: usize,
        others: &[Neighbour],
        lure: Option<(f32, f32)>,
        dt: f32,
    ) {
        let p = f.personality;
        let (mx, my) = f.mouth();

        // Hungry fish notice food from across the tank, picky ones only nearby.
        // Pellets behind the fish count as farther away, so it prefers to keep
        // going rather than turn around for every crumb.
        let notice = 300.0 + p.appetite * 1.5 * self.width;
        let nearest_food = self
            .food
            .iter()
            .enumerate()
            .filter(|(_, food)| !food.eaten)
            .map(|(j, food)| (j, (food.x - mx).hypot(food.y - my)))
            .filter(|&(_, d)| d < notice)
            .map(|(j, d)| {
                let behind = (self.food[j].x - f.x) * f.dir < 0.0;
                (j, if behind { d + f.w * 2.0 } else { d })
            })
            .min_by(|a, b| a.1.total_cmp(&b.1))
            .map(|(j, _)| j);

        let chase = if let Some(j) = nearest_food {
            Some((
                self.food[j].x,
                self.food[j].y,
                f.max_speed * (1.2 + 0.6 * p.appetite),
            ))
        } else if let (true, Some((px, py))) = (p.follows_cursor(), lure) {
            Some((px - f.dir * 40.0, py, f.max_speed * 1.1))
        } else {
            None
        };

        let (mut des_x, mut des_y);
        // Where the fish is ultimately going; decides whether it turns around.
        let goal_x;
        if let Some((tx, ty, speed)) = chase {
            goal_x = Some(tx);
            f.retarget = true;
            let (dx, dy) = (tx - mx, ty - my);
            let dist = dx.hypot(dy).max(0.001);
            if let Some(j) = nearest_food
                && dist < (f.w * 0.18).max(16.0)
            {
                self.food[j].eaten = true;
            }
            let arrive = (dist / 120.0).min(1.0);
            des_x = dx / dist * speed * arrive;
            des_y = dy / dist * speed * arrive;
        } else {
            goal_x = matches!(f.mode, Mode::Wander).then_some(f.tx);
            if f.retarget {
                f.retarget = false;
                self.pick_target(f);
            }
            match f.mode {
                Mode::Idle { left } => {
                    // Hover with a slow bob, then set off again.
                    des_x = 0.0;
                    des_y = (self.time * 0.8 + f.phase * 0.1).sin() * 6.0;
                    let left = left - dt;
                    if left <= 0.0 {
                        self.pick_target(f);
                    } else {
                        f.mode = Mode::Idle { left };
                    }
                }
                Mode::Wander => {
                    let (dx, dy) = (f.tx - f.x, f.ty - f.y);
                    let dist = dx.hypot(dy).max(0.001);
                    if dist < 30.0 {
                        if self.rng.chance(0.6 * (1.0 - p.restlessness)) {
                            f.mode = Mode::Idle {
                                left: self.rng.range(1.5, 5.0),
                            };
                        } else {
                            self.pick_target(f);
                        }
                    }
                    let arrive = (dist / 90.0).min(1.0);
                    des_x = dx / dist * f.cruise * arrive;
                    des_y = dy / dist * f.cruise * arrive;
                }
            }

            // Schooling: drift toward and align with nearby fish at a similar depth.
            let (mut n, mut cx, mut cy, mut cvx, mut cvy) = (0.0, 0.0, 0.0, 0.0, 0.0);
            for (k, g) in others.iter().enumerate() {
                if k == index || (g.depth - f.depth).abs() > 0.3 {
                    continue;
                }
                if (g.x - f.x).hypot(g.y - f.y) < SCHOOL_RADIUS {
                    n += 1.0;
                    cx += g.x;
                    cy += g.y;
                    cvx += g.vx;
                    cvy += g.vy;
                }
            }
            if n > 0.0 && matches!(f.mode, Mode::Wander) {
                let s = p.sociability;
                des_x += (cx / n - f.x) * 0.12 * s + (cvx / n - f.vx) * 0.5 * s;
                des_y += (cy / n - f.y) * 0.12 * s + (cvy / n - f.vy) * 0.5 * s;
            }
        }

        // Separation, always.
        for (k, g) in others.iter().enumerate() {
            if k == index || (g.depth - f.depth).abs() > 0.35 {
                continue;
            }
            let (sx, sy) = (f.x - g.x, (f.y - g.y) * 1.6);
            let d = sx.hypot(sy);
            let min_d = (f.w + g.w) * 0.38;
            if d > 0.001 && d < min_d {
                des_x += sx / d * (min_d - d) * 1.5;
                des_y += sy / d * (min_d - d) * 1.5;
            }
        }

        // Turning: only when the goal is clearly behind the body, for a moment,
        // and not right after the previous turn. Small corrections near the goal
        // (food under the chin) are done by backing up slowly instead, which is
        // what stops fish from flipping back and forth over a pellet.
        f.turn_cooldown = (f.turn_cooldown - dt).max(0.0);
        let goal_behind = goal_x.is_some_and(|gx| (gx - f.x) * f.dir < -f.w * 0.35);
        if goal_behind {
            f.turn_intent += dt;
        } else {
            f.turn_intent = (f.turn_intent - 2.0 * dt).max(0.0);
        }
        if f.turn_intent >= TURN_INTENT_SECS && f.turn_cooldown == 0.0 {
            f.dir = -f.dir;
            f.turn_cooldown = TURN_COOLDOWN;
            f.turn_intent = 0.0;
        }
        if des_x.signum() != f.dir {
            // About to turn: brake. Otherwise: back up, but only slowly.
            let backing = if goal_behind {
                0.0
            } else {
                BACKUP_SPEED * f.max_speed
            };
            des_x = des_x.clamp(-backing, backing);
        }

        let k = (1.6 * dt).min(1.0);
        f.vx += (des_x - f.vx) * k;
        f.vy += (des_y - f.vy) * k;
        f.vy = f.vy.clamp(-0.55 * f.max_speed, 0.55 * f.max_speed);

        f.x += f.vx * dt;
        f.y += f.vy * dt;
        self.clamp_to_water(f);

        let step = TURN_RATE * dt;
        f.facing = if (f.dir - f.facing).abs() <= step {
            f.dir
        } else {
            f.facing + step * (f.dir - f.facing).signum()
        };

        f.speed = f.vx.hypot(f.vy);
        f.phase = (f.phase + dt * (2.5 + f.speed * 0.07)) % (TAU * 64.0);
    }
}

fn list_has_follower(fish: &[Fish]) -> bool {
    fish.iter().any(|f| f.personality.follows_cursor())
}

#[cfg(test)]
mod tests {
    use super::*;

    const DT: f32 = 1.0 / 30.0;

    fn sim() -> Sim {
        let mut s = Sim::new(42);
        s.resize(1920.0, 1080.0);
        s
    }

    #[test]
    fn spawns_requested_fish_once_sized() {
        let mut s = Sim::new(1);
        assert!(s.fish().is_empty());
        s.resize(800.0, 600.0);
        assert_eq!(s.fish().len(), 7);
        s.set_fish_count(12);
        assert_eq!(s.fish().len(), 12);
    }

    #[test]
    fn always_someone_curious() {
        for seed in 1..200 {
            let mut s = Sim::new(seed);
            s.resize(800.0, 600.0);
            s.set_fish_count(2);
            assert!(list_has_follower(s.fish()), "seed {seed}");
        }
    }

    #[test]
    fn fish_stay_in_the_water() {
        let mut s = sim();
        for _ in 0..30 * 120 {
            s.step(DT, None);
        }
        for f in s.fish() {
            assert!(
                f.x >= f.w * 0.5 - 0.01 && f.x <= 1920.0 - f.w * 0.5 + 0.01,
                "x {}",
                f.x
            );
            assert!(
                f.y >= 1080.0 * WATER_TOP && f.y <= 1080.0 * SAND_TOP,
                "y {}",
                f.y
            );
            assert!(f.x.is_finite() && f.y.is_finite() && f.phase.is_finite());
        }
    }

    #[test]
    fn fish_keep_moving_around() {
        // Idling must end; over two minutes every fish should cover real distance.
        let mut s = sim();
        let start: Vec<(f32, f32)> = s.fish().iter().map(|f| (f.x, f.y)).collect();
        let mut travelled = vec![0.0f32; start.len()];
        let mut last = start.clone();
        for _ in 0..30 * 120 {
            s.step(DT, None);
            for ((t, l), f) in travelled.iter_mut().zip(last.iter_mut()).zip(s.fish()) {
                *t += (f.x - l.0).hypot(f.y - l.1);
                *l = (f.x, f.y);
            }
        }
        for (i, t) in travelled.iter().enumerate() {
            assert!(*t > 1500.0, "fish {i} only travelled {t:.0}px");
        }
    }

    #[test]
    fn turns_are_infrequent() {
        let mut s = sim();
        let mut flips = 0;
        let mut dirs: Vec<f32> = s.fish().iter().map(|f| f.dir).collect();
        let secs = 180;
        for _ in 0..30 * secs {
            s.step(DT, None);
            for (d, f) in dirs.iter_mut().zip(s.fish()) {
                if *d != f.dir {
                    flips += 1;
                    *d = f.dir;
                }
            }
        }
        let per_fish_per_min = flips as f32 / s.fish().len() as f32 / (secs as f32 / 60.0);
        assert!(
            per_fish_per_min < 6.0,
            "{per_fish_per_min:.1} turns per fish per minute"
        );
    }

    #[test]
    fn feeding_is_efficient_without_a_turning_frenzy() {
        // Regression guard: fish used to circle pellets they could not quite
        // reach, turning ~9 times a minute and still missing a third of the food.
        let (mut dropped, mut dissolved, mut flips, mut fish_minutes) = (0, 0, 0, 0.0);
        for seed in 1..6 {
            let mut s = Sim::new(seed);
            s.resize(2560.0, 1440.0);
            let mut dirs: Vec<f32> = s.fish().iter().map(|f| f.dir).collect();
            for n in 0..30 * 120 {
                if n % (30 * 6) == 0 {
                    s.feed(None);
                    dropped += 8;
                }
                dissolved += s
                    .food
                    .iter()
                    .filter(|p| !p.eaten && p.landed && p.age + DT >= FOOD_LIFETIME)
                    .count();
                s.step(DT, None);
                for (d, f) in dirs.iter_mut().zip(s.fish()) {
                    if *d != f.dir {
                        flips += 1;
                        *d = f.dir;
                    }
                }
            }
            fish_minutes += s.fish().len() as f32 * 2.0;
        }
        let eaten = 1.0 - dissolved as f32 / dropped as f32;
        let turns = flips as f32 / fish_minutes;
        assert!(
            eaten > 0.75,
            "only {:.0}% of the food was eaten",
            eaten * 100.0
        );
        assert!(
            turns < 5.0,
            "{turns:.1} turns per fish per minute while feeding"
        );
    }

    #[test]
    fn food_gets_eaten_or_dissolves() {
        let mut s = sim();
        s.feed(Some(960.0));
        assert_eq!(s.food().len(), 8);
        for _ in 0..30 * 40 {
            s.step(DT, None);
        }
        assert!(s.food().is_empty());
    }

    #[test]
    fn food_is_unlimited_and_clearable() {
        let mut s = sim();
        for _ in 0..50 {
            s.feed(None);
        }
        assert_eq!(s.food().len(), 400);
        s.clear_food();
        assert!(s.food().is_empty());
    }

    #[test]
    fn fish_never_stay_squashed() {
        let mut s = sim();
        s.set_fish_count(12);
        let mut squashed_for = [0.0f32; 12];
        let mut worst = 0.0f32;
        for n in 0..30 * 90 {
            // Keep dropping food at awkward spots: right above/below the school
            if n % 45 == 0 {
                let x = s.fish()[n / 45 % 12].x;
                s.feed(Some(x));
            }
            s.step(DT, None);
            for (t, f) in squashed_for.iter_mut().zip(s.fish()) {
                *t = if f.facing.abs() < 0.9 { *t + DT } else { 0.0 };
                worst = worst.max(*t);
            }
        }
        assert!(worst < 0.5, "a fish stayed mid-turn for {worst:.2}s");
    }

    #[test]
    fn curious_fish_approaches_moving_cursor_then_gets_bored() {
        let mut s = sim();
        let target = (300.0, 500.0);
        let follower = s
            .fish()
            .iter()
            .position(|f| f.personality.follows_cursor())
            .unwrap();
        // Slow circular drag (~1 px per frame) must still count as movement.
        for n in 0..30 * 25 {
            let a = n as f32 * 0.02;
            s.step(
                DT,
                Some((target.0 + 30.0 * a.cos(), target.1 + 30.0 * a.sin())),
            );
            if n > 30 {
                assert!(s.lure_active(), "slow movement not detected at frame {n}");
            }
        }
        let f = &s.fish()[follower];
        let near = (f.x - target.0).hypot(f.y - target.1);
        assert!(near < 150.0, "curious fish did not approach: {near}px away");

        // Cursor held still: interest fades after POINTER_INTEREST_SECS.
        for _ in 0..30 * 5 {
            s.step(DT, Some(target));
        }
        assert!(!s.lure_active());
    }

    #[test]
    fn resize_keeps_school() {
        let mut s = sim();
        s.resize(640.0, 480.0);
        assert_eq!(s.fish().len(), 7);
        for f in s.fish() {
            assert!(f.x <= 640.0 && f.y <= 480.0);
        }
    }
}
