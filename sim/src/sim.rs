use std::collections::{HashMap, HashSet};
use std::f32::consts::TAU;

use crate::relations::{FRIEND, RIVAL, Relations};
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
/// Downward pull on a newly added fish dropping in through the surface, px/s².
const ENTRY_GRAVITY: f32 = 260.0;
/// Hunger gained per second (full to starving in about half an hour), scaled by appetite.
const HUNGER_RATE: f32 = 1.0 / 1800.0;
/// Hunger one pellet takes away.
const PELLET_NOURISHMENT: f32 = 0.12;
/// Below this a fish is full and ignores food.
const FULL: f32 = 0.15;
/// Fish closer than this count as spending time together.
const NEAR: f32 = 260.0;
/// Affinity gained per second together, at full sociability on both sides.
const BONDING_RATE: f32 = 0.004;
/// Affinity lost when one fish snatches a pellet the other was going for.
const SNATCH_PENALTY: f32 = 0.08;
/// How long a rival chase lasts.
const CHASE_SECS: f32 = 2.5;
/// How long the others stay curious about a fish that just arrived.
const NEWCOMER_SECS: f32 = 8.0;
/// A fish sent to the surface gives up after this long.
const SURFACE_SECS: f32 = 15.0;
/// Bubbles rise this fast, px/s.
const BUBBLE_RISE: f32 = 70.0;
/// Bubbles at least this big leave a ripple when they pop (only the
/// notification burst makes them, so the ripple is its signature).
const RIPPLE_BUBBLE: f32 = 8.0;
/// How long a ripple spreads before it is gone.
pub const RIPPLE_SECS: f32 = 1.4;

/// One fish the host wants in the tank. The host owns the ids; a fish keeps
/// its position and personality across updates as long as its id stays.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct FishSpec {
    pub id: u64,
    /// Index into the host's sprite list; passed through untouched
    pub sprite: u32,
    /// Sprite height / width
    pub aspect: f32,
    /// Size multiplier on top of the depth-based size
    pub scale: f32,
}

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
    /// Chasing a rival away
    Chase { target: u64, left: f32 },
    /// Swimming up to blow bubbles at the surface (a notification arrived)
    Surface { left: f32 },
}

#[derive(Debug, Clone)]
pub struct Fish {
    pub id: u64,
    pub sprite: u32,
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
    /// Width before the spec's scale is applied
    base_w: f32,
    /// Still dropping in through the surface
    pub entering: bool,
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
    /// 0 = full, 1 = starving
    pub hunger: f32,
    /// Seconds until this fish may start another rival chase
    chase_cooldown: f32,
    /// Seconds until it lets out a few bubbles on its own
    bubble_timer: f32,
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
pub struct Bubble {
    pub x: f32,
    pub y: f32,
    pub radius: f32,
    seed: f32,
    age: f32,
}

impl Bubble {
    /// Fades in when born and out just below the surface.
    pub fn alpha(&self, water_top: f32) -> f32 {
        let fade_in = (self.age / 0.25).clamp(0.0, 1.0);
        let fade_out = ((self.y - water_top) / 40.0).clamp(0.0, 1.0);
        fade_in * fade_out * 0.85
    }
}

/// A ring spreading on the surface where a big bubble popped.
#[derive(Debug, Clone)]
pub struct Ripple {
    pub x: f32,
    pub y: f32,
    pub age: f32,
    /// Size of the bubble that made it
    pub strength: f32,
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
    id: u64,
    /// Rival this fish is chasing right now, if any
    chasing: Option<u64>,
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
    population: Vec<FishSpec>,
    fish: Vec<Fish>,
    food: Vec<Food>,
    bubbles: Vec<Bubble>,
    ripples: Vec<Ripple>,
    time: f32,
    rng: Rng,
    pointer_anchor: Option<(f32, f32)>,
    pointer_still_for: f32,
    /// 0 = night, 1 = day
    daylight: f32,
    relations: Relations,
    /// Recently added fish and how much longer the others care
    newcomers: Vec<(u64, f32)>,
    /// (eater id, pellet index) this step, to spot food snatching
    eaten_this_step: Vec<(u64, usize)>,
    /// Pellet each fish went for this step
    food_targets: Vec<Option<usize>>,
}

impl Sim {
    pub fn new(seed: u64) -> Self {
        Self {
            width: 0.0,
            height: 0.0,
            population: Vec::new(),
            fish: Vec::new(),
            food: Vec::new(),
            bubbles: Vec::new(),
            ripples: Vec::new(),
            time: 0.0,
            rng: Rng::new(seed),
            pointer_anchor: None,
            pointer_still_for: f32::INFINITY,
            daylight: 1.0,
            relations: Relations::default(),
            newcomers: Vec::new(),
            eaten_this_step: Vec::new(),
            food_targets: Vec::new(),
        }
    }

    /// 0 = night (fish rest near the bottom), 1 = day.
    pub fn set_daylight(&mut self, daylight: f32) {
        self.daylight = if daylight.is_finite() {
            daylight.clamp(0.0, 1.0)
        } else {
            1.0
        };
    }

    pub fn relations(&self) -> Vec<(u64, u64, f32)> {
        self.relations.export()
    }

    pub fn set_relations(&mut self, list: &[(u64, u64, f32)]) {
        self.relations.import(list);
        let ids: HashSet<u64> = self.population.iter().map(|s| s.id).collect();
        if !ids.is_empty() {
            self.relations.retain_ids(&ids);
        }
    }

    /// How active fish are: slower and lazier at night.
    fn activity(&self) -> f32 {
        0.4 + 0.6 * self.daylight
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

    pub fn bubbles(&self) -> &[Bubble] {
        &self.bubbles
    }

    pub fn ripples(&self) -> &[Ripple] {
        &self.ripples
    }

    pub fn water_level(&self) -> f32 {
        self.water_top()
    }

    /// Something happened (a notification): a curious fish that is not too deep
    /// swims up and blows bubbles at the surface. Returns false if nobody is free.
    pub fn announce(&mut self) -> bool {
        let height = self.height.max(1.0);
        let score = |f: &Fish| f.personality.curiosity * 0.5 + (1.0 - f.y / height);
        let pick = self
            .fish
            .iter_mut()
            .filter(|f| !f.entering && !matches!(f.mode, Mode::Surface { .. }))
            .max_by(|a, b| score(a).total_cmp(&score(b)));
        match pick {
            Some(f) => {
                f.mode = Mode::Surface { left: SURFACE_SECS };
                f.retarget = true;
                true
            }
            None => false,
        }
    }

    /// `spread`: seconds over which the bubbles come out, a stream rather than a clump.
    fn blow_bubbles(&mut self, x: f32, y: f32, count: usize, size: (f32, f32), spread: f32) {
        for _ in 0..count {
            let bubble = Bubble {
                x: x + self.rng.range(-6.0, 6.0),
                y: y + self.rng.range(-4.0, 4.0),
                radius: self.rng.range(size.0, size.1),
                seed: self.rng.range(0.0, TAU),
                age: -self.rng.range(0.0, spread),
            };
            self.bubbles.push(bubble);
        }
    }

    fn update_bubbles(&mut self, dt: f32) {
        let wt = self.water_top();
        for b in &mut self.bubbles {
            b.age += dt;
            if b.age < 0.0 {
                continue;
            }
            // Bigger bubbles rise faster and wobble less
            b.y -= BUBBLE_RISE * (0.7 + b.radius * 0.06) * dt;
            b.x += (b.age * 3.0 + b.seed).sin() * 14.0 / (1.0 + b.radius * 0.1) * dt;
            b.radius *= 1.0 + 0.04 * dt;
        }
        // Big bubbles reaching the surface leave a spreading ring
        for b in self
            .bubbles
            .iter()
            .filter(|b| b.y <= wt - 4.0 && b.radius >= RIPPLE_BUBBLE)
        {
            self.ripples.push(Ripple {
                x: b.x,
                y: wt,
                age: 0.0,
                strength: b.radius,
            });
        }
        self.bubbles.retain(|b| b.y > wt - 4.0);
        for r in &mut self.ripples {
            r.age += dt;
        }
        self.ripples.retain(|r| r.age < RIPPLE_SECS);
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
        if self.fish.len() != self.population.len() {
            self.sync_population(false);
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

    /// Makes the tank hold exactly these fish. Fish whose id is already present
    /// keep swimming where they are; new ones drop in through the surface when
    /// `animate_new` is set (otherwise they just appear somewhere in the water).
    pub fn set_population(&mut self, specs: &[FishSpec], animate_new: bool) {
        self.population = specs.to_vec();
        if self.has_size() {
            self.sync_population(animate_new);
        }
    }

    fn sync_population(&mut self, animate_new: bool) {
        let mut existing: HashMap<u64, Fish> = std::mem::take(&mut self.fish)
            .into_iter()
            .map(|f| (f.id, f))
            .collect();
        let specs = self.population.clone();
        let mut list = Vec::with_capacity(specs.len());
        for spec in &specs {
            let fish = match existing.remove(&spec.id) {
                Some(mut f) => {
                    f.sprite = spec.sprite;
                    Self::apply_size(&mut f, spec);
                    self.clamp_to_water(&mut f);
                    f
                }
                None => {
                    if animate_new {
                        self.newcomers.push((spec.id, NEWCOMER_SECS));
                    }
                    self.spawn(spec, animate_new)
                }
            };
            list.push(fish);
        }
        let ids: HashSet<u64> = specs.iter().map(|s| s.id).collect();
        self.relations.retain_ids(&ids);
        self.newcomers.retain(|(id, _)| ids.contains(id));
        // Someone should always be curious about the cursor.
        if !list_has_follower(&list)
            && let Some(first) = list.first_mut()
        {
            first.personality.curiosity = 0.8;
        }
        self.fish = list;
    }

    fn apply_size(f: &mut Fish, spec: &FishSpec) {
        f.w = f.base_w * spec.scale.clamp(0.3, 3.0);
        f.h = f.w * spec.aspect.clamp(0.15, 1.5);
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
        let mut ty = f.y + self.rng.range(-0.25, 0.25) * self.height;
        let night = 1.0 - self.daylight;
        if night > 0.5 {
            // Rest in the lower part of the tank
            ty = bottom - self.rng.range(0.0, 0.3) * (bottom - top);
        } else if f.hunger > 0.7 {
            // Hungry fish look for food near the surface
            ty = top + self.rng.range(0.0, 0.35) * (bottom - top);
        }
        f.ty = ty.clamp(top, bottom);
        f.cruise = self.rng.range(0.45, 1.0) * f.max_speed;
        f.mode = Mode::Wander;
    }

    fn clamp_to_water(&self, f: &mut Fish) {
        let half = f.w * 0.5;
        f.x = f.x.clamp(half, (self.width - half).max(half));
        // A fish dropping in may still be above the surface.
        let top = if f.entering {
            -f.h
        } else {
            self.water_top() + f.h * 0.5
        };
        f.y = f.y.clamp(top, (self.sand_top() - f.h * 0.3).max(top));
    }

    fn spawn(&mut self, spec: &FishSpec, entering: bool) -> Fish {
        let depth = self.rng.unit();
        let base_w = self.rng.range(150.0, 230.0) * (1.0 - 0.45 * depth);
        let dir = if self.rng.chance(0.5) { -1.0 } else { 1.0 };
        let personality = Personality::random(&mut self.rng);
        let mut f = Fish {
            id: spec.id,
            sprite: spec.sprite,
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
            base_w,
            entering,
            w: 0.0,
            h: 0.0,
            max_speed: self.rng.range(60.0, 115.0) * (1.0 - 0.3 * depth) * personality.speed,
            dir,
            facing: dir,
            turn_cooldown: 0.0,
            turn_intent: 0.0,
            phase: self.rng.range(0.0, TAU),
            speed: 0.0,
            personality,
            hunger: self.rng.range(0.3, 0.6),
            chase_cooldown: self.rng.range(20.0, 60.0),
            bubble_timer: self.rng.range(20.0, 120.0),
        };
        Self::apply_size(&mut f, spec);
        if entering {
            // Start just above the surface and plop in.
            f.x = self.rng.range(self.width * 0.15, self.width * 0.85);
            f.y = self.water_top() - f.h * 0.6;
            f.vy = 60.0;
        }
        self.clamp_to_water(&mut f);
        self.pick_target(&mut f);
        f
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
        self.update_bubbles(dt);

        let (wt, st) = (self.water_top(), self.sand_top());
        // Nobody plays with the cursor at night
        let lure =
            pointer.filter(|&(_, y)| self.lure_active() && self.daylight > 0.3 && y > wt && y < st);

        for (_, left) in &mut self.newcomers {
            *left -= dt;
        }
        self.newcomers.retain(|(_, left)| *left > 0.0);
        self.eaten_this_step.clear();
        self.food_targets.clear();
        self.food_targets.resize(self.fish.len(), None);

        let others: Vec<Neighbour> = self
            .fish
            .iter()
            .map(|g| Neighbour {
                id: g.id,
                chasing: match g.mode {
                    Mode::Chase { target, .. } => Some(target),
                    _ => None,
                },
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

        self.update_relations(&others, dt);
        self.food.retain(|p| !p.eaten && p.age < FOOD_LIFETIME);
    }

    fn update_relations(&mut self, others: &[Neighbour], dt: f32) {
        // Time together builds friendship, more so between sociable fish
        for i in 0..self.fish.len() {
            for j in i + 1..self.fish.len() {
                let (a, b) = (&self.fish[i], &self.fish[j]);
                if a.entering || b.entering || (a.depth - b.depth).abs() > 0.35 {
                    continue;
                }
                if (a.x - b.x).hypot(a.y - b.y) < NEAR {
                    let social = (a.personality.sociability + b.personality.sociability) * 0.5;
                    self.relations
                        .adjust(a.id, b.id, dt * BONDING_RATE * social);
                }
            }
        }
        // Snatching a pellet someone nearby was going for sours things
        for &(eater, pellet) in &self.eaten_this_step {
            let Some(e) = others.iter().find(|n| n.id == eater) else {
                continue;
            };
            for (k, target) in self.food_targets.iter().enumerate() {
                let other = &others[k];
                if *target == Some(pellet)
                    && other.id != eater
                    && (other.x - e.x).hypot(other.y - e.y) < NEAR
                {
                    self.relations.adjust(eater, other.id, -SNATCH_PENALTY);
                }
            }
        }
        self.relations.fade(dt);
    }

    fn step_fish(
        &mut self,
        f: &mut Fish,
        index: usize,
        others: &[Neighbour],
        lure: Option<(f32, f32)>,
        dt: f32,
    ) {
        if f.entering {
            self.step_entering(f, dt);
            return;
        }
        let p = f.personality;
        let (mx, my) = f.mouth();
        let activity = self.activity();

        // Now and then a few bubbles slip out (fewer at night)
        f.bubble_timer -= dt * (0.3 + 0.7 * self.daylight);
        if f.bubble_timer <= 0.0 {
            f.bubble_timer = self.rng.range(40.0, 140.0);
            let n = 1 + (self.rng.unit() * 3.0) as usize;
            self.blow_bubbles(mx, my, n, (2.0, 4.5), 0.6);
        }
        f.hunger = (f.hunger + dt * HUNGER_RATE * (0.6 + 0.8 * p.appetite)).min(1.0);
        f.chase_cooldown = (f.chase_cooldown - dt).max(0.0);

        // Full fish ignore food. Hungry fish notice it from across the tank,
        // picky ones only nearby. Pellets behind the fish count as farther
        // away, so it prefers to keep going rather than turn for every crumb.
        let notice = if f.hunger > FULL {
            250.0 + (0.5 * p.appetite + f.hunger) * self.width
        } else {
            0.0
        };
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

        self.food_targets[index] = nearest_food;

        // A rival close by may get chased off now and then
        if matches!(f.mode, Mode::Wander)
            && nearest_food.is_none()
            && f.chase_cooldown == 0.0
            && self.daylight > 0.4
            && let Some(rival) = others.iter().find(|g| {
                g.id != f.id
                    && (g.x - f.x).hypot(g.y - f.y) < 450.0
                    && self.relations.get(f.id, g.id) < RIVAL
            })
        {
            f.mode = Mode::Chase {
                target: rival.id,
                left: CHASE_SECS,
            };
            f.chase_cooldown = self.rng.range(25.0, 70.0);
        }
        let rival = match f.mode {
            Mode::Chase { target, .. } => others.iter().find(|g| g.id == target),
            _ => None,
        };
        // Curious fish go and have a look at whoever just arrived
        let newcomer = (p.curiosity > 0.35 && self.daylight > 0.3)
            .then(|| self.newcomers.iter().find(|(id, _)| *id != f.id))
            .flatten()
            .and_then(|(id, _)| others.iter().find(|g| g.id == *id));

        let surface = match f.mode {
            Mode::Surface { left } => Some(left),
            _ => None,
        };
        let chase = if surface.is_some() {
            Some((
                f.x + f.dir * 30.0,
                // A little below the surface, so the bubbles have a way to rise
                self.water_top() + f.h * 2.0,
                f.max_speed * 1.8,
            ))
        } else if let Some(j) = nearest_food {
            let eager = 1.0 + 0.5 * p.appetite + 0.4 * f.hunger;
            Some((
                self.food[j].x,
                self.food[j].y,
                f.max_speed * eager * (0.6 + 0.4 * self.daylight),
            ))
        } else if let Some(r) = rival {
            Some((r.x, r.y, f.max_speed * 1.3))
        } else if let (true, Some((px, py))) = (p.follows_cursor(), lure) {
            Some((px - f.dir * 40.0, py, f.max_speed * 1.1))
        } else if let Some(n) = newcomer {
            // Gather in a loose ring around the newcomer
            let angle = index as f32 * 2.4;
            let r = 120.0 + f.w * 0.5;
            Some((
                n.x + angle.cos() * r,
                n.y + angle.sin() * r * 0.5,
                f.cruise * 1.3,
            ))
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
                f.hunger = (f.hunger - PELLET_NOURISHMENT).max(0.0);
                self.eaten_this_step.push((f.id, j));
            }
            if let Mode::Chase { target, left } = f.mode {
                let left = left - dt;
                f.mode = if left > 0.0 {
                    Mode::Chase { target, left }
                } else {
                    Mode::Wander
                };
            }
            if let Some(left) = surface {
                // At the top: a burst of bubbles, then a short rest up there
                if dist < 30.0 + f.h * 0.3 {
                    // Much bigger than everyday bubbles, and each one leaves a ripple
                    // at the surface: the burst should read as "something happened"
                    self.blow_bubbles(mx, my, 9, (10.0, 18.0), 1.8);
                    f.mode = Mode::Idle { left: 2.5 };
                } else if left - dt <= 0.0 {
                    f.mode = Mode::Wander;
                } else {
                    f.mode = Mode::Surface { left: left - dt };
                }
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
                Mode::Wander | Mode::Chase { .. } | Mode::Surface { .. } => {
                    // (a chase whose rival vanished just falls back to wandering)
                    f.mode = Mode::Wander;
                    let (dx, dy) = (f.tx - f.x, f.ty - f.y);
                    let dist = dx.hypot(dy).max(0.001);
                    if dist < 30.0 {
                        // Full fish and fish at night take more breaks, and longer ones
                        let night = 1.0 - self.daylight;
                        let rest = 0.6 * (1.0 - p.restlessness)
                            + 0.35 * night
                            + if f.hunger < FULL { 0.15 } else { 0.0 };
                        if self.rng.chance(rest.min(0.95)) {
                            f.mode = Mode::Idle {
                                left: self.rng.range(1.5, 5.0) * (1.0 + 2.0 * night),
                            };
                        } else {
                            self.pick_target(f);
                        }
                    }
                    let arrive = (dist / 90.0).min(1.0);
                    des_x = dx / dist * f.cruise * activity * arrive;
                    des_y = dy / dist * f.cruise * activity * arrive;
                }
            }

            // Friends swim side by side
            if matches!(f.mode, Mode::Wander)
                && let Some((friend, affinity)) = others
                    .iter()
                    .filter(|g| g.id != f.id && (g.x - f.x).hypot(g.y - f.y) < 700.0)
                    .map(|g| (g, self.relations.get(f.id, g.id)))
                    .filter(|&(_, a)| a > FRIEND)
                    .max_by(|a, b| a.1.total_cmp(&b.1))
            {
                let side = if f.id < friend.id { -1.0 } else { 1.0 };
                let (gx, gy) = (
                    friend.x - friend.vx.signum() * f.w * 0.3,
                    friend.y + side * f.h * 0.9,
                );
                des_x += (gx - f.x) * 0.25 * affinity + (friend.vx - f.vx) * 0.6 * affinity;
                des_y += (gy - f.y) * 0.25 * affinity + (friend.vy - f.vy) * 0.6 * affinity;
            }

            // Get away from anyone chasing us
            for g in others.iter().filter(|g| g.chasing == Some(f.id)) {
                let (sx, sy) = (f.x - g.x, f.y - g.y);
                let d = sx.hypot(sy).max(1.0);
                if d < 300.0 {
                    des_x += sx / d * f.max_speed * 1.2;
                    des_y += sy / d * f.max_speed * 0.6;
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
        // Mostly horizontal swimmers, except when heading up to the surface
        let climb = if surface.is_some() { 1.5 } else { 0.55 };
        f.vy = f.vy.clamp(-climb * f.max_speed, climb * f.max_speed);

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

impl Sim {
    /// A new fish sinks in through the surface, slows down, then starts swimming.
    fn step_entering(&mut self, f: &mut Fish, dt: f32) {
        let settled_y = self.water_top() + f.h * 1.2;
        if f.y < settled_y {
            f.vy = (f.vy + ENTRY_GRAVITY * dt).min(f.max_speed * 2.5);
        } else {
            // Water brakes the fall.
            f.vy *= 1.0 - (4.0 * dt).min(1.0);
        }
        f.vx *= 1.0 - (2.0 * dt).min(1.0);
        f.y += f.vy * dt;
        self.clamp_to_water(f);
        f.speed = f.vx.hypot(f.vy);
        f.phase = (f.phase + dt * 6.0) % (TAU * 64.0);
        if f.y >= settled_y && f.vy < 25.0 {
            f.entering = false;
            self.pick_target(f);
        }
    }
}

fn list_has_follower(fish: &[Fish]) -> bool {
    fish.iter().any(|f| f.personality.follows_cursor())
}

#[cfg(test)]
mod tests {
    use super::*;

    const DT: f32 = 1.0 / 30.0;

    fn specs(n: u64) -> Vec<FishSpec> {
        (1..=n)
            .map(|id| FishSpec {
                id,
                sprite: (id % 4) as u32,
                aspect: 0.6,
                scale: 1.0,
            })
            .collect()
    }

    fn sim_with(seed: u64, n: u64, w: f32, h: f32) -> Sim {
        let mut s = Sim::new(seed);
        s.set_population(&specs(n), false);
        s.resize(w, h);
        s
    }

    fn sim() -> Sim {
        sim_with(42, 7, 1920.0, 1080.0)
    }

    #[test]
    fn spawns_requested_fish_once_sized() {
        let mut s = Sim::new(1);
        s.set_population(&specs(7), false);
        assert!(s.fish().is_empty());
        s.resize(800.0, 600.0);
        assert_eq!(s.fish().len(), 7);
        s.set_population(&specs(12), false);
        assert_eq!(s.fish().len(), 12);
    }

    #[test]
    fn population_updates_keep_existing_fish() {
        let mut s = sim();
        for _ in 0..30 * 5 {
            s.step(DT, None);
        }
        let before: Vec<(u64, f32, f32)> = s.fish().iter().map(|f| (f.id, f.x, f.y)).collect();

        // Drop fish 2, add fish 100, keep the rest
        let mut next: Vec<FishSpec> = specs(7).into_iter().filter(|f| f.id != 2).collect();
        next.push(FishSpec {
            id: 100,
            sprite: 9,
            aspect: 0.9,
            scale: 1.5,
        });
        s.set_population(&next, true);

        assert_eq!(s.fish().len(), 7);
        assert!(s.fish().iter().all(|f| f.id != 2));
        for (id, x, y) in before.into_iter().filter(|b| b.0 != 2) {
            let f = s.fish().iter().find(|f| f.id == id).unwrap();
            assert_eq!((f.x, f.y), (x, y), "fish {id} moved on update");
        }
        let new = s.fish().iter().find(|f| f.id == 100).unwrap();
        assert_eq!(new.sprite, 9);
        assert!((new.h / new.w - 0.9).abs() < 1e-4);
    }

    #[test]
    fn new_fish_drop_in_through_the_surface() {
        let mut s = sim();
        let mut next = specs(7);
        next.push(FishSpec {
            id: 50,
            sprite: 0,
            aspect: 0.6,
            scale: 1.0,
        });
        s.set_population(&next, true);
        let f = s.fish().iter().find(|f| f.id == 50).unwrap();
        assert!(
            f.entering && f.y < 1080.0 * WATER_TOP,
            "starts above the surface"
        );
        for _ in 0..30 * 6 {
            s.step(DT, None);
        }
        let f = s.fish().iter().find(|f| f.id == 50).unwrap();
        assert!(!f.entering, "settled into the water");
        assert!(f.y > 1080.0 * WATER_TOP);
    }

    #[test]
    fn sizes_follow_the_spec() {
        let mut s = Sim::new(3);
        s.set_population(
            &[
                FishSpec {
                    id: 1,
                    sprite: 0,
                    aspect: 0.5,
                    scale: 0.5,
                },
                FishSpec {
                    id: 2,
                    sprite: 0,
                    aspect: 1.2,
                    scale: 2.0,
                },
            ],
            false,
        );
        s.resize(1920.0, 1080.0);
        let (a, b) = (&s.fish()[0], &s.fish()[1]);
        assert!((a.h / a.w - 0.5).abs() < 1e-4 && (b.h / b.w - 1.2).abs() < 1e-4);
        assert!(a.w < 150.0 && b.w > 150.0);
    }

    #[test]
    fn always_someone_curious() {
        for seed in 1..200 {
            let s = sim_with(seed, 2, 800.0, 600.0);
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
        // Fish are kept starving so this measures reaching food, not appetite.
        let (mut dropped, mut dissolved, mut flips, mut fish_minutes) = (0, 0, 0, 0.0);
        for seed in 1..6 {
            let mut s = sim_with(seed, 7, 2560.0, 1440.0);
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
                s.fish.iter_mut().for_each(|f| f.hunger = 1.0);
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
    fn hunger_grows_and_eating_satisfies() {
        let mut s = sim();
        let before: Vec<f32> = s.fish().iter().map(|f| f.hunger).collect();
        for _ in 0..30 * 60 {
            s.step(DT, None);
        }
        for (b, f) in before.iter().zip(s.fish()) {
            assert!(f.hunger > *b, "hunger should grow over time");
        }
        s.fish.iter_mut().for_each(|f| f.hunger = 1.0);
        let start: f32 = s.fish().iter().map(|f| f.hunger).sum();
        for _ in 0..4 {
            s.feed(None);
        }
        for _ in 0..30 * 40 {
            s.step(DT, None);
        }
        let end: f32 = s.fish().iter().map(|f| f.hunger).sum();
        assert!(
            end < start - 1.0,
            "eating should satisfy: {start:.2} -> {end:.2}"
        );
    }

    #[test]
    fn full_fish_ignore_food() {
        let mut s = sim();
        s.fish.iter_mut().for_each(|f| f.hunger = 0.0);
        s.feed(Some(960.0));
        for _ in 0..30 * 30 {
            s.step(DT, None);
            s.fish.iter_mut().for_each(|f| f.hunger = 0.0);
        }
        // Nothing eaten: every pellet is still there (or has dissolved untouched)
        assert!(s.food().iter().all(|p| !p.eaten));
    }

    #[test]
    fn friends_swim_closer_than_strangers() {
        let mean_distance = |affinity: f32| {
            let mut total = 0.0;
            for seed in 1..6 {
                let mut s = sim_with(seed, 2, 2560.0, 1440.0);
                // Same depth so they interact; strangers wander independently
                s.fish.iter_mut().for_each(|f| {
                    f.depth = 0.2;
                    f.personality.sociability = 0.0;
                });
                s.relations.adjust(1, 2, affinity);
                let mut sum = 0.0;
                for n in 0..30 * 120 {
                    s.step(DT, None);
                    // Keep the bond from drifting during the measurement
                    s.relations.import(&[(1, 2, affinity)]);
                    if n > 30 * 20 {
                        let (a, b) = (&s.fish()[0], &s.fish()[1]);
                        sum += (a.x - b.x).hypot(a.y - b.y);
                    }
                }
                total += sum / (30.0 * 100.0);
            }
            total / 5.0
        };
        let (friends, strangers) = (mean_distance(0.9), mean_distance(0.0));
        assert!(
            friends < strangers * 0.6,
            "friends {friends:.0}px apart vs strangers {strangers:.0}px"
        );
    }

    #[test]
    fn time_together_makes_friends() {
        let mut s = sim_with(3, 2, 1920.0, 1080.0);
        s.fish.iter_mut().for_each(|f| {
            f.depth = 0.2;
            f.personality.sociability = 1.0;
        });
        for _ in 0..30 * 400 {
            // Keep them together
            let (x, y) = (s.fish[0].x, s.fish[0].y);
            s.fish[1].x = x + 120.0;
            s.fish[1].y = y;
            s.step(DT, None);
        }
        assert!(
            s.relations.get(1, 2) > FRIEND,
            "affinity {}",
            s.relations.get(1, 2)
        );
    }

    #[test]
    fn rivals_chase_each_other() {
        let mut s = sim_with(5, 2, 1920.0, 1080.0);
        s.relations.adjust(1, 2, -0.9);
        let mut chases = 0;
        let mut was_chasing = false;
        for _ in 0..30 * 240 {
            // Keep them within sight of each other
            let (x, y) = (s.fish[0].x, s.fish[0].y);
            if (s.fish[1].x - x).hypot(s.fish[1].y - y) > 400.0 {
                s.fish[1].x = x + 300.0 * s.fish[1].dir.signum();
                s.fish[1].y = y;
            }
            s.step(DT, None);
            let chasing = s
                .fish()
                .iter()
                .any(|f| matches!(f.mode, Mode::Chase { .. }));
            if chasing && !was_chasing {
                chases += 1;
            }
            was_chasing = chasing;
        }
        assert!(chases >= 2, "only {chases} chases in four minutes");
    }

    #[test]
    fn fish_rest_low_and_slow_at_night() {
        // Averaged over several seeds so it measures behaviour, not luck
        let stats = |daylight: f32| {
            let (mut y, mut speed, mut n) = (0.0, 0.0, 0.0);
            for seed in 1..6 {
                let mut s = sim_with(seed, 12, 1920.0, 1080.0);
                s.set_daylight(daylight);
                s.fish.iter_mut().for_each(|f| f.hunger = 0.0);
                for k in 0..30 * 180 {
                    s.step(DT, None);
                    if k > 30 * 60 {
                        for f in s.fish() {
                            y += f.y;
                            speed += f.speed;
                            n += 1.0;
                        }
                    }
                }
            }
            (y / n, speed / n)
        };
        let ((day_y, day_speed), (night_y, night_speed)) = (stats(1.0), stats(0.0));
        assert!(
            night_y > day_y + 80.0,
            "night fish should stay lower: {day_y:.0} vs {night_y:.0}"
        );
        assert!(
            night_speed < day_speed * 0.7,
            "night fish should be slower: {day_speed:.1} vs {night_speed:.1}"
        );
    }

    #[test]
    fn curious_fish_greet_newcomers() {
        let mut s = sim_with(11, 8, 2560.0, 1440.0);
        s.fish.iter_mut().for_each(|f| {
            f.personality.curiosity = 0.9;
            f.hunger = 0.0;
        });
        let mut next = specs(8);
        next.push(FishSpec {
            id: 99,
            sprite: 0,
            aspect: 0.6,
            scale: 1.0,
        });
        s.set_population(&next, true);
        let mean_distance = |s: &Sim| {
            let n = s.fish().iter().find(|f| f.id == 99).unwrap();
            let d: f32 = s
                .fish()
                .iter()
                .filter(|f| f.id != 99)
                .map(|f| (f.x - n.x).hypot(f.y - n.y))
                .sum();
            d / 8.0
        };
        let before = mean_distance(&s);
        for _ in 0..30 * 6 {
            s.step(DT, None);
        }
        let after = mean_distance(&s);
        assert!(
            after < before * 0.7,
            "the others did not come closer: {before:.0} -> {after:.0}"
        );
    }

    #[test]
    fn relations_survive_export_and_forget_removed_fish() {
        let mut s = sim();
        s.relations.adjust(1, 2, 0.7);
        s.relations.adjust(3, 4, -0.6);
        let saved = s.relations();
        let mut t = sim();
        t.set_relations(&saved);
        assert_eq!(t.relations.get(2, 1), 0.7);
        t.set_population(&specs(3), false);
        assert_eq!(t.relations.get(3, 4), 0.0, "fish 4 left the tank");
    }

    #[test]
    fn announce_sends_a_fish_up_to_blow_bubbles() {
        let mut s = sim();
        s.fish.iter_mut().for_each(|f| {
            f.y = 900.0;
            f.hunger = 0.0;
            f.bubble_timer = 1000.0;
        });
        assert!(s.announce());
        let mut burst = false;
        for _ in 0..30 * 15 {
            s.step(DT, None);
            if s.bubbles().len() >= 5 {
                burst = true;
                break;
            }
        }
        assert!(burst, "no bubble burst at the surface");
        assert!(
            s.bubbles().iter().any(|b| b.radius >= RIPPLE_BUBBLE),
            "the burst should be big bubbles"
        );
        let top = s
            .bubbles()
            .iter()
            .map(|b| b.y)
            .fold(f32::INFINITY, f32::min);
        assert!(
            top < 1080.0 * 0.3,
            "bubbles should come from near the surface, highest at y {top}"
        );
        let mut rippled = false;
        for _ in 0..30 * 6 {
            s.step(DT, None);
            rippled |= !s.ripples().is_empty();
        }
        assert!(rippled, "big bubbles should leave ripples at the surface");
    }

    #[test]
    fn bubbles_rise_and_pop_at_the_surface() {
        let mut s = sim();
        s.fish.iter_mut().for_each(|f| f.bubble_timer = 1000.0);
        s.blow_bubbles(500.0, 800.0, 5, (3.0, 6.0), 0.6);
        let start: f32 = s.bubbles().iter().map(|b| b.y).sum();
        for _ in 0..30 {
            s.step(DT, None);
        }
        let later: f32 = s.bubbles().iter().map(|b| b.y).sum();
        assert!(later < start, "bubbles should rise");
        for _ in 0..30 * 20 {
            s.step(DT, None);
        }
        assert!(s.bubbles().is_empty(), "bubbles should pop at the surface");
        assert!(
            s.ripples().is_empty(),
            "small everyday bubbles leave no ripples"
        );
    }

    #[test]
    fn fish_blow_bubbles_now_and_then() {
        let mut s = sim();
        let mut seen = 0;
        for _ in 0..30 * 240 {
            s.step(DT, None);
            seen = seen.max(s.bubbles().len());
        }
        assert!(seen > 0, "no fish blew a bubble in four minutes");
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
        let mut s = sim_with(42, 12, 1920.0, 1080.0);
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
