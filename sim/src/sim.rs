use std::collections::{HashMap, HashSet};
use std::f32::consts::TAU;

use crate::relations::{FRIEND, RIVAL, Relations};
use crate::rng::Rng;

mod carry;
mod crawl;
mod eggs;
mod pet;
mod travel;
pub use carry::Carry;
pub use crawl::{Crawler, CrawlerSpec, Gait};
pub use eggs::{Egg, HATCH_SECS};
pub use pet::Heart;
pub use travel::Departure;

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
/// Fish within this distance of each other can form or join a school.
const SCHOOL_RADIUS: f32 = 700.0;
/// How often (seconds) the tank looks for fish that could form a school.
const SCHOOL_CHECK_SECS: f32 = 2.0;
/// At most this share of the fish swim in schools at once: some do, not all.
const SCHOOL_SHARE: f32 = 0.3;
/// Largest school; more fish make more schools rather than one big one.
const SCHOOL_MAX: usize = 5;
/// Schools steer away from each other's middle when closer than this.
const SCHOOL_KEEP_APART: f32 = 750.0;
/// Wander targets avoid spots within about this distance of other fish.
const CROWD_RADIUS: f32 = 420.0;
/// The cursor has to stay this close to a fish...
const CURSOR_NOTICE_RADIUS: f32 = 220.0;
/// ...for this long before a curious fish notices it.
const CURSOR_DWELL_SECS: f32 = 1.0;
/// A follower loses track of a cursor farther away than this.
const CURSOR_LOSE_RADIUS: f32 = 700.0;
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
/// A pellet is eaten when it is this close to the mouth (pellets are ~10 px).
const EAT_RADIUS: f32 = 12.0;
/// ...and only if the fish has slowed down to this fraction of its top speed,
/// so a fish racing past doesn't count as eating.
const EAT_MAX_SPEED: f32 = 0.75;

/// Bubbles rise this fast, px/s.
const BUBBLE_RISE: f32 = 70.0;
/// Bubbles at least this big leave a ripple when they pop (only the
/// notification burst makes them, so the ripple is its signature).
const RIPPLE_BUBBLE: f32 = 8.0;
/// How long a ripple spreads before it is gone.
pub const RIPPLE_SECS: f32 = 1.4;
/// Close friends this fond of each other may have a baby...
const PARENT_AFFINITY: f32 = 0.8;
/// ...about this often per second while together, fed and rested.
const BIRTH_CHANCE_PER_SEC: f32 = 1.0 / 900.0;
/// Seconds a parent rests before another baby (randomised up to twice this).
const BIRTH_COOLDOWN: f32 = 1800.0;

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
    /// Grown up; only adults have babies
    pub adult: bool,
    /// Where the mouth is on the drawing, as fractions of its width and height
    /// (head facing right); food is eaten only when it touches this point
    pub mouth: (f32, f32),
    /// What kind of swimmer: decides how it moves
    pub body: Body,
}

/// Swimmers that aren't shaped like a fish move their own way
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Body {
    #[default]
    Fish,
    /// Pulses up, drifts down; doesn't chase food or turn round
    Jelly,
    /// Upright and slow
    Seahorse,
    /// Glides near the bottom
    Ray,
    /// Slow paddler that comes up for air now and then
    Turtle,
}

impl Body {
    pub fn from_index(i: u32) -> Self {
        match i {
            1 => Body::Jelly,
            2 => Body::Seahorse,
            3 => Body::Ray,
            4 => Body::Turtle,
            _ => Body::Fish,
        }
    }

    fn speed(self) -> f32 {
        match self {
            Body::Fish => 1.0,
            Body::Jelly => 0.35,
            Body::Seahorse => 0.3,
            Body::Ray => 0.8,
            Body::Turtle => 0.5,
        }
    }

    /// Tall drawings are fine for these
    fn max_aspect(self) -> f32 {
        match self {
            Body::Jelly | Body::Seahorse => 3.0,
            _ => 1.5,
        }
    }
}

/// Mouth of a drawing nobody marked: near the tip of the nose, a bit low.
pub const DEFAULT_MOUTH: (f32, f32) = (0.92, 0.56);

/// What kind of fish it is at heart; shapes where it swims and how it behaves.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Character {
    /// Roams far across the whole tank, quick
    Explorer,
    /// Keeps to its own patch of the tank
    Homebody,
    /// Stays low, near the sand, unhurried
    Bottom,
    /// Stays in the upper water
    Skimmer,
    /// Rests a lot, swims slowly
    Lazy,
    /// Now and then darts off in a quick burst
    Playful,
    /// Keeps its distance from other fish and swims away from the cursor
    Shy,
}

impl Character {
    const ALL: [Character; 7] = [
        Character::Explorer,
        Character::Homebody,
        Character::Bottom,
        Character::Skimmer,
        Character::Lazy,
        Character::Playful,
        Character::Shy,
    ];

    fn random(rng: &mut Rng) -> Self {
        Self::ALL[(rng.unit() * Self::ALL.len() as f32) as usize % Self::ALL.len()]
    }

    /// Stable number for the C ABI (see DC_CHARACTER_* in the header)
    pub fn index(self) -> u32 {
        Self::ALL.iter().position(|c| *c == self).unwrap_or(0) as u32
    }

    /// Preferred slice of the water column, 0 = surface .. 1 = sand.
    fn band(self, rng: &mut Rng) -> (f32, f32) {
        match self {
            Character::Bottom => (0.6, 1.0),
            Character::Skimmer => (0.0, 0.4),
            // Everyone else gets a personal half of the column, centred
            // anywhere in it, so together they fill it evenly
            _ => {
                let centre = rng.unit();
                ((centre - 0.25).max(0.0), (centre + 0.25).min(1.0))
            }
        }
    }
}

/// Fixed traits rolled when a fish is created.
#[derive(Debug, Clone, Copy)]
pub struct Personality {
    pub character: Character,
    /// 0..1, how quickly it notices food appearing
    pub alertness: f32,
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
        let mut p = Self {
            character: Character::random(rng),
            alertness: rng.unit(),
            speed: rng.range(0.8, 1.2),
            sociability: rng.unit(),
            curiosity: rng.unit(),
            appetite: rng.range(0.2, 1.0),
            restlessness: rng.unit(),
        };
        // The character nudges the dice
        match p.character {
            Character::Explorer => {
                p.restlessness = p.restlessness.max(0.7);
                p.speed *= 1.1;
                p.curiosity += 0.2;
            }
            Character::Homebody => p.restlessness *= 0.7,
            Character::Bottom => {
                p.speed *= 0.85;
                p.curiosity *= 0.7;
            }
            Character::Skimmer => p.alertness += 0.2,
            Character::Lazy => {
                p.restlessness *= 0.3;
                p.speed *= 0.8;
                p.alertness *= 0.6;
            }
            Character::Playful => {
                p.restlessness = p.restlessness.max(0.6);
                p.curiosity += 0.3;
                p.sociability += 0.2;
            }
            Character::Shy => {
                p.curiosity *= 0.3;
                p.sociability *= 0.5;
            }
        }
        p.alertness = p.alertness.clamp(0.0, 1.0);
        p.curiosity = p.curiosity.clamp(0.0, 1.0);
        p.sociability = p.sociability.clamp(0.0, 1.0);
        p
    }

    /// Curious, not shy: notices a cursor that lingers nearby and follows it a while
    pub fn follows_cursor(&self) -> bool {
        self.curiosity >= 0.55 && self.character != Character::Shy
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
    /// Stopped to swallow a pellet
    Eating { left: f32 },
    /// Swimming up to blow bubbles at the surface (a notification arrived)
    Surface { left: f32 },
    /// Going into a shelter (a cave), then staying inside for `left` seconds
    Hide { shelter: usize, left: f32 },
    /// Off to a plant to lay eggs with `partner`
    Lay { partner: u64, count: u32, left: f32 },
    /// Being stroked with the cursor: holds still and enjoys it
    Petted { left: f32 },
    /// Swimming into, or riding in, the bubble (see carry.rs)
    Bubbled,
}

/// The opening of a cave or hollow, in scene pixels: fish can swim in there.
/// The host draws the decoration's front over fish that are `sheltered`.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Shelter {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

impl Shelter {
    fn centre(&self) -> (f32, f32) {
        (self.x + self.w * 0.5, self.y + self.h * 0.5)
    }

    /// The fish's body overlaps the opening
    fn overlaps(&self, f: &Fish) -> bool {
        (f.x - self.centre().0).abs() < (self.w + f.w) * 0.5
            && (f.y - self.centre().1).abs() < (self.h + f.h) * 0.5
    }

    fn fits(&self, f: &Fish) -> bool {
        f.w < self.w && f.h < self.h * 0.9
    }
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
    pub adult: bool,
    /// Seconds (of life time) until it may have another baby
    birth_cooldown: f32,
    /// Preferred slice of the water column (0 = surface .. 1 = sand)
    band: (f32, f32),
    /// Homebodies stay around here (fractions of the tank)
    home: (f32, f32),
    /// Has noticed the food that is in the tank right now
    food_aware: bool,
    /// Seconds until it notices food; negative while not counting
    notice_in: f32,
    /// How long the cursor has lingered near this fish
    cursor_near: f32,
    /// Seconds left following the cursor
    follow_left: f32,
    /// Seconds before it can take an interest in the cursor again
    follow_cooldown: f32,
    /// Shy fish: seconds left swimming away from the cursor
    flee_left: f32,
    /// The school it swims in, if any
    school: Option<u32>,
    /// Playful fish: seconds until the next dash, and seconds left in one
    dash_timer: f32,
    dash_left: f32,
    /// Mouth on the drawing, fractions of width and height (head facing right)
    mouth: (f32, f32),
    /// Size multiplier from the spec (1 = medium)
    scale: f32,
    /// In (or partly in) a cave: drawn behind the cave's front
    pub sheltered: bool,
    pub body: Body,
    /// Cursor stroking so far (px), see pet.rs
    pet: f32,
    /// 0 .. 1: grows with petting and with eating food you dropped
    trust: f32,
    heart_timer: f32,
    /// Heading out through a side edge to another screen: -1 left, 1 right
    travel: i32,
    /// Just came in through a side edge, still outside the tank
    arriving: bool,
}

impl Fish {
    /// The pellet size this fish eats: 0 small, 1 medium, 2 large (babies
    /// count as small until they have grown)
    pub fn bite(&self) -> u8 {
        if self.scale < 0.8 {
            0
        } else if self.scale <= 1.25 {
            1
        } else {
            2
        }
    }

    /// 0 for medium and bigger fish, rising to 1 for tiny ones (scale 0.4):
    /// small fish breed more and school more readily
    pub fn smallness(&self) -> f32 {
        ((1.0 - self.scale) / 0.6).clamp(0.0, 1.0)
    }

    /// Close enough in size to swim in one school
    fn same_size(&self, other: &Fish) -> bool {
        (0.87..1.15).contains(&(other.w / self.w.max(1.0)))
    }

    /// Sociability plus the pull small fish feel toward a school
    fn school_appetite(&self) -> f32 {
        (self.personality.sociability + 0.6 * self.smallness()).min(1.0)
    }
}

impl Fish {
    /// The mouth on screen: the marked point on the drawing, mirrored with the
    /// fish's facing and turned with its pitch, the same way it is drawn.
    pub fn mouth(&self) -> (f32, f32) {
        let flip = if self.facing.abs() < 0.08 {
            0.08f32.copysign(self.facing)
        } else {
            self.facing
        };
        let ox = (self.mouth.0.clamp(0.0, 1.0) - 0.5) * self.w * flip;
        let oy = (self.mouth.1.clamp(0.0, 1.0) - 0.5) * self.h;
        let (sn, cs) = self.tilt().to_radians().sin_cos();
        (self.x + ox * cs - oy * sn, self.y + ox * sn + oy * cs)
    }

    /// Tail swing as a fraction of body height; livelier when swimming fast.
    pub fn amplitude(&self) -> f32 {
        0.04 + 0.08 * (self.speed / self.max_speed).min(1.0)
    }

    /// Body pitch in degrees, nose following the vertical velocity.
    pub fn tilt(&self) -> f32 {
        let deg = self.vy.atan2(self.vx.abs() + 20.0).to_degrees() * 0.7;
        let lean = match self.body {
            Body::Jelly => 0.0,
            Body::Seahorse => 0.15,
            Body::Ray => 0.5,
            _ => 1.0,
        };
        deg.clamp(-28.0, 28.0) * self.dir * lean
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

/// A few fish swimming together behind a leader.
#[derive(Debug, Clone)]
struct School {
    id: u32,
    leader: u64,
    /// Everyone except the leader, in formation order
    members: Vec<u64>,
    /// The kind (sprite) of the leader; same-kind fish are preferred
    kind: u32,
    left: f32,
}

/// Two fish had a baby at (x, y); the host decides what it looks like.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Birth {
    pub parent_a: u64,
    pub parent_b: u64,
    pub x: f32,
    pub y: f32,
    /// How many babies: small fish have litters
    pub count: u32,
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
    /// Pellet size class: 0 small, 1 medium, 2 large; each fish eats its own size
    pub size: u8,
    seed: f32,
    age: f32,
    landed: bool,
    eaten: bool,
}

impl Food {
    /// Diameter in scene pixels
    pub fn diameter(&self) -> f32 {
        [6.0, 10.0, 15.0][self.size.min(2) as usize]
    }

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
    school: Option<u32>,
    dir: f32,
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
    births: Vec<Birth>,
    shelters: Vec<Shelter>,
    /// Side edges that lead to another screen, and fish that left through them
    exits: (bool, bool),
    departures: Vec<Departure>,
    /// The bubble carrying a fish around, while the wand is in use
    carry: Option<Carry>,
    /// Hearts floating up from petted fish
    hearts: Vec<Heart>,
    last_pointer: Option<(f32, f32)>,
    /// Trust of fish that aren't in the tank (yet)
    saved_trust: HashMap<u64, f32>,
    /// Clutches waiting to hatch, and the spots on plants they are laid on
    eggs: Vec<Egg>,
    nests: Vec<(f32, f32)>,
    /// Bottom creatures, see crawl.rs
    crawlers: Vec<Crawler>,
    crawler_specs: Vec<CrawlerSpec>,
    /// Height of the ground every `ground_step` px (rocks crawlers climb)
    ground: Vec<f32>,
    ground_step: f32,
    schools: Vec<School>,
    next_school: u32,
    school_check: f32,
    /// Speeds up growing-up clocks (cooldowns, birth chance); 0 = no babies
    life_speed: f32,
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
            births: Vec::new(),
            shelters: Vec::new(),
            exits: (false, false),
            departures: Vec::new(),
            carry: None,
            hearts: Vec::new(),
            last_pointer: None,
            saved_trust: HashMap::new(),
            eggs: Vec::new(),
            nests: Vec::new(),
            crawlers: Vec::new(),
            crawler_specs: Vec::new(),
            ground: Vec::new(),
            ground_step: 8.0,
            schools: Vec::new(),
            next_school: 1,
            school_check: 0.0,
            life_speed: 1.0,
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

    /// Whether any fish is following the cursor right now.
    pub fn lure_active(&self) -> bool {
        self.fish.iter().any(|f| f.follow_left > 0.0)
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

    /// Babies born since the last call.
    /// Caves and hollows fish can swim into; replaces the previous list
    pub fn set_shelters(&mut self, shelters: &[Shelter]) {
        if shelters == self.shelters.as_slice() {
            return;
        }
        self.shelters = shelters.to_vec();
        for f in &mut self.fish {
            if matches!(f.mode, Mode::Hide { .. }) {
                f.mode = Mode::Wander;
                f.retarget = true;
            }
            f.sheltered = false;
        }
    }

    /// The nearest shelter within `radius` that `f` fits in and that has room
    fn find_shelter(&self, f: &Fish, radius: f32) -> Option<usize> {
        self.shelters
            .iter()
            .enumerate()
            .filter(|(_, s)| s.fits(f) && f.body != Body::Jelly)
            .filter(|(k, s)| {
                let taken: f32 = self
                    .fish
                    .iter()
                    .filter(|g| {
                        g.id != f.id
                            && matches!(g.mode, Mode::Hide { shelter, .. } if shelter == *k)
                    })
                    .map(|g| g.w)
                    .sum();
                taken + f.w < s.w * 1.3
            })
            .map(|(k, s)| (k, (s.centre().0 - f.x).hypot(s.centre().1 - f.y)))
            .filter(|&(_, d)| d < radius)
            .min_by(|a, b| a.1.total_cmp(&b.1))
            .map(|(k, _)| k)
    }

    pub fn take_births(&mut self) -> Vec<Birth> {
        std::mem::take(&mut self.births)
    }

    /// 1 = normal, higher for testing, 0 = no babies at all.
    pub fn set_life_speed(&mut self, speed: f32) {
        self.life_speed = if speed.is_finite() {
            speed.max(0.0)
        } else {
            1.0
        };
    }

    /// Puts a fish somewhere (a baby next to its parents) instead of letting it drop in.
    pub fn place_fish(&mut self, id: u64, x: f32, y: f32) -> bool {
        let Some(i) = self.fish.iter().position(|f| f.id == id) else {
            return false;
        };
        let mut f = self.fish[i].clone();
        f.x = x;
        f.y = y;
        f.vx = 0.0;
        f.vy = 0.0;
        f.entering = false;
        self.clamp_to_water(&mut f);
        self.pick_target(&mut f);
        self.fish[i] = f;
        true
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
        let (old_w, old_h) = (self.width, self.height);
        self.width = width;
        self.height = height;
        if !self.has_size() {
            return;
        }
        if old_w > 0.0 && old_h > 0.0 {
            self.resize_crawlers(width / old_w, height / old_h);
        } else {
            // Placed before the tank had a size: start them afresh
            self.crawlers.clear();
            let specs = self.crawler_specs.clone();
            self.set_crawlers(&specs);
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
                    f.adult = spec.adult;
                    f.mouth = spec.mouth;
                    f.scale = spec.scale;
                    if f.body != spec.body {
                        f.max_speed *= spec.body.speed() / f.body.speed();
                        f.body = spec.body;
                    }
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
            if first.personality.character == Character::Shy {
                first.personality.character = Character::Explorer;
            }
        }
        self.fish = list;
    }

    fn apply_size(f: &mut Fish, spec: &FishSpec) {
        f.w = f.base_w * spec.scale.clamp(0.3, 3.0);
        f.h = f.w * spec.aspect.clamp(0.15, spec.body.max_aspect());
    }

    /// Picks the next wander target from a few candidates: within the fish's
    /// range and depth band (both from its character), mostly ahead so it
    /// doesn't keep turning, and in the least crowded spot so the fish spread
    /// out over the whole tank instead of bunching up.
    fn pick_target(&mut self, f: &mut Fish) {
        let (wt, st) = (self.water_top(), self.sand_top());
        let (lo, hi) = (f.w * 0.6, (self.width - f.w * 0.6).max(f.w * 0.6 + 1.0));
        let (top, bottom) = (wt + f.h, (st - f.h * 0.7).max(wt + f.h + 1.0));
        let water = bottom - top;

        let (mut band_lo, mut band_hi) = f.band;
        if self.daylight < 0.5 {
            // Night: everyone settles into the lower part
            band_lo = band_lo.max(0.55);
            band_hi = band_hi.max(band_lo + 0.25).min(1.0);
        } else if f.hunger > 0.7 {
            // Hungry fish look for food higher up
            band_lo = 0.0;
            band_hi = band_hi.clamp(0.25, 0.5);
        }
        let character = f.personality.character;
        let (span_lo, span_hi) = match character {
            Character::Explorer => (0.35, 0.85),
            Character::Lazy => (0.08, 0.25),
            _ => (0.15, 0.5),
        };

        if f.travel != 0 || self.maybe_travel(f) {
            return;
        }
        // Parents keep an eye on their eggs
        if let Some((ex, ey)) = self.own_eggs(f.id)
            && self.rng.chance(0.5)
        {
            f.tx = (ex + self.rng.range(-220.0, 220.0)).clamp(lo, hi);
            f.ty = (ey - self.rng.range(0.0, 160.0)).clamp(top, bottom);
            f.cruise = self.rng.range(0.4, 0.7) * f.max_speed;
            f.mode = Mode::Wander;
            return;
        }
        let candidates = if character == Character::Explorer {
            8
        } else {
            6
        };
        let mut best = (f.x, f.y, f32::INFINITY);
        for _ in 0..candidates {
            let x = if character == Character::Homebody {
                f.home.0 * self.width + self.rng.range(-0.15, 0.15) * self.width
            } else if self.rng.chance(0.5) {
                // Anywhere across the tank, weighted toward the sides: fish
                // travelling between random points pass through the middle
                // far more often, so the sides need the extra pull
                lo + (hi - lo) * 0.5 * (1.0 - (std::f32::consts::PI * self.rng.unit()).cos())
            } else {
                let span = self.rng.range(span_lo, span_hi) * self.width;
                let ahead = if self.rng.chance(0.75) { 1.0 } else { -1.0 };
                let x = f.x + f.dir * ahead * span;
                // Would leave the tank: head the other way instead
                if (lo..=hi).contains(&x) {
                    x
                } else {
                    f.x - f.dir * ahead * span
                }
            }
            .clamp(lo, hi);
            let y = if character == Character::Homebody
                && self.daylight >= 0.5
                && f.body != Body::Ray
            {
                top + (f.home.1 + self.rng.range(-0.15, 0.15)).clamp(0.0, 1.0) * water
            } else {
                top + self.rng.range(band_lo, band_hi) * water
            };
            let score = self.crowding(f.id, x, y) + self.rng.range(0.0, 0.15);
            if score < best.2 {
                best = (x, y, score);
            }
        }
        f.tx = best.0;
        f.ty = best.1.clamp(top, bottom);
        let lazy = if character == Character::Lazy {
            0.7
        } else {
            1.0
        };
        f.cruise = self.rng.range(0.45, 1.0) * f.max_speed * lazy;
        f.mode = Mode::Wander;
    }

    /// How busy a spot is: ~1 per fish right there, fading out over CROWD_RADIUS.
    /// Fish of my own school don't count, other schools do.
    fn crowding(&self, me: u64, x: f32, y: f32) -> f32 {
        let r2 = 2.0 * CROWD_RADIUS * CROWD_RADIUS;
        let my_school = self.fish.iter().find(|g| g.id == me).and_then(|g| g.school);
        self.fish
            .iter()
            .filter(|g| g.id != me && (my_school.is_none() || g.school != my_school))
            .map(|g| {
                // Where it is now and where it is heading both count
                let d2 = (g.x - x).powi(2) + (g.y - y).powi(2);
                let t2 = (g.tx - x).powi(2) + (g.ty - y).powi(2);
                0.5 * (-d2 / r2).exp() + 0.5 * (-t2 / r2).exp()
            })
            .sum()
    }

    fn clamp_to_water(&self, f: &mut Fish) {
        let half = f.w * 0.5;
        // (on the way to or from another screen it may be past a side edge)
        if f.travel == 0 && !f.arriving {
            f.x = f.x.clamp(half, (self.width - half).max(half));
        }
        // A fish dropping in may still be above the surface.
        let top = if f.entering {
            -f.h
        } else {
            self.water_top() + f.h * 0.5
        };
        let bottom = match f.mode {
            Mode::Hide { shelter, .. } if shelter < self.shelters.len() => {
                let s = self.shelters[shelter];
                (self.sand_top() - f.h * 0.3).max(s.y + s.h - f.h * 0.4)
            }
            _ => self.sand_top() - f.h * 0.3,
        };
        f.y = f.y.clamp(top, bottom.max(top));
    }

    fn spawn(&mut self, spec: &FishSpec, entering: bool) -> Fish {
        let depth = self.rng.unit();
        let base_w = self.rng.range(150.0, 230.0) * (1.0 - 0.45 * depth);
        let dir = if self.rng.chance(0.5) { -1.0 } else { 1.0 };
        // Who a fish is comes from its id, so it keeps its character, depth
        // band and home across restarts (the editor shows the character)
        let mut traits =
            Rng::new(spec.id.wrapping_mul(0x9E37_79B9_7F4A_7C15) ^ 0xD6E8_FEB8_6659_FD93);
        let personality = Personality::random(&mut traits);
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
            max_speed: self.rng.range(60.0, 115.0)
                * (1.0 - 0.3 * depth)
                * personality.speed
                * spec.body.speed(),
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
            adult: spec.adult,
            birth_cooldown: self.rng.range(0.3, 1.0) * BIRTH_COOLDOWN,
            band: if spec.body == Body::Ray {
                (0.75, 1.0)
            } else {
                personality.character.band(&mut traits)
            },
            home: (traits.range(0.1, 0.9), traits.range(0.15, 0.85)),
            food_aware: false,
            notice_in: -1.0,
            cursor_near: 0.0,
            follow_left: 0.0,
            follow_cooldown: 0.0,
            flee_left: 0.0,
            school: None,
            dash_timer: self.rng.range(8.0, 25.0),
            dash_left: 0.0,
            mouth: spec.mouth,
            scale: spec.scale,
            sheltered: false,
            body: spec.body,
            pet: 0.0,
            trust: 0.0,
            heart_timer: 0.0,
            travel: 0,
            arriving: false,
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
        // A mix of pellet sizes for the fish that are here, at least one of
        // each size someone eats
        let mut eaters = [0usize; 3];
        for f in &self.fish {
            eaters[f.bite() as usize] += 1;
        }
        let total = eaters.iter().sum::<usize>();
        let sizes: Vec<u8> = if total == 0 {
            vec![1; 8]
        } else {
            (0..3u8)
                .flat_map(|c| {
                    let n = eaters[c as usize];
                    let k = if n == 0 {
                        0
                    } else {
                        ((8 * n) as f32 / total as f32).round().max(1.0) as usize
                    };
                    std::iter::repeat_n(c, k)
                })
                .collect()
        };
        for size in sizes {
            let food = Food {
                x: cx + self.rng.range(-90.0, 90.0),
                y: wt + self.rng.range(-10.0, 30.0),
                size,
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
        // While the bubble is out, the cursor is busy with it: nobody follows,
        // flees from or gets petted by it
        self.update_carry(dt);
        let pointer = pointer.filter(|_| self.carry.is_none());
        self.update_pointer(dt, pointer);
        self.update_petting(dt, pointer);
        self.update_food(dt);
        self.update_bubbles(dt);
        self.update_schools(dt);

        let (wt, st) = (self.water_top(), self.sand_top());
        // The cursor, when it is in the water; nobody plays with it at night
        let lure = pointer.filter(|&(_, y)| self.daylight > 0.3 && y > wt && y < st);

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
                school: g.school,
                dir: g.dir,
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
        self.update_eggs(dt * self.life_speed);
        self.update_travel();
        self.update_crawlers(dt, pointer);
        self.food.retain(|p| !p.eaten && p.age < FOOD_LIFETIME);
    }

    fn update_relations(&mut self, others: &[Neighbour], dt: f32) {
        let life_dt = dt * self.life_speed;
        for f in &mut self.fish {
            f.birth_cooldown = (f.birth_cooldown - life_dt).max(0.0);
        }
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
                    self.maybe_have_baby(i, j, life_dt);
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
        if let Mode::Eating { left } = f.mode {
            self.step_eating(f, left, dt);
            return;
        }
        if let Mode::Petted { left } = f.mode {
            self.step_petted(f, left, dt);
            return;
        }
        if let Mode::Bubbled = f.mode {
            self.step_bubbled(f, dt);
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

        // Food is noticed with a delay that depends on how alert the fish is
        // and how far away the food is, so not everyone rushes at once.
        let closest_pellet = self
            .food
            .iter()
            .filter(|food| !food.eaten && food.size == f.bite())
            .map(|food| (food.x - mx).hypot(food.y - my))
            .fold(f32::INFINITY, f32::min);
        if !closest_pellet.is_finite() || f.hunger <= FULL {
            f.food_aware = false;
            f.notice_in = -1.0;
        } else if !f.food_aware {
            if f.notice_in < 0.0 {
                let reaction = 0.3 + (1.0 - p.alertness) * 3.5 + self.rng.range(0.0, 2.0);
                f.notice_in = reaction * (1.0 + closest_pellet / self.width);
            } else {
                f.notice_in -= dt;
                if f.notice_in <= 0.0 {
                    // Only fish that feel like eating come over; the others
                    // carry on and may think again a little later
                    // (a starving fish always comes)
                    let keen = ((f.hunger - FULL) / 0.65).clamp(0.0, 1.0).powi(2)
                        * (0.6 + 0.4 * p.appetite)
                        + if f.hunger > 0.8 { 1.0 } else { 0.0 };
                    if self.rng.chance(keen.max(0.05)) {
                        f.food_aware = true;
                    } else {
                        f.notice_in = self.rng.range(20.0, 40.0);
                    }
                }
            }
        }

        // Full fish ignore food. Hungry fish go for it from across the tank,
        // picky ones only nearby. Pellets behind the fish count as farther
        // away, so it prefers to keep going rather than turn for every crumb,
        // and pellets others are already after count as farther too.
        let notice = if f.food_aware && f.body != Body::Jelly {
            250.0 + (0.5 * p.appetite + f.hunger) * self.width
        } else {
            0.0
        };
        let nearest_food = self
            .food
            .iter()
            .enumerate()
            .filter(|(_, food)| !food.eaten && food.size == f.bite())
            .map(|(j, food)| (j, (food.x - mx).hypot(food.y - my)))
            .filter(|&(_, d)| d < notice)
            .map(|(j, d)| {
                let behind = (self.food[j].x - f.x) * f.dir < 0.0;
                let taken = self.food_targets[..index]
                    .iter()
                    .filter(|t| **t == Some(j))
                    .count() as f32;
                (j, d + if behind { f.w * 2.0 } else { 0.0 } + taken * 150.0)
            })
            .min_by(|a, b| a.1.total_cmp(&b.1))
            .map(|(j, _)| j);

        // The cursor: a curious fish notices it only once it has lingered
        // close by for a moment, then follows it for a while; shy fish swim off.
        let cursor_dist = lure.map(|(px, py)| (px - f.x).hypot(py - f.y));
        match cursor_dist {
            Some(d) if d < CURSOR_NOTICE_RADIUS => f.cursor_near += dt,
            Some(_) => f.cursor_near = (f.cursor_near - 2.0 * dt).max(0.0),
            None => f.cursor_near = 0.0,
        }
        f.follow_cooldown = (f.follow_cooldown - dt).max(0.0);
        f.flee_left = (f.flee_left - dt).max(0.0);
        if f.follow_left > 0.0 {
            // Losing track: the cursor left the water, went far away or stopped moving
            let lost = cursor_dist.is_none_or(|d| d > CURSOR_LOSE_RADIUS)
                || self.pointer_still_for > POINTER_INTEREST_SECS;
            f.follow_left -= dt * if lost { 4.0 } else { 1.0 };
            if f.follow_left <= 0.0 {
                f.follow_left = 0.0;
                f.follow_cooldown = 8.0;
                f.cursor_near = 0.0;
            }
        } else if (p.follows_cursor() || f.trust > 0.5)
            && matches!(f.body, Body::Fish | Body::Turtle | Body::Ray)
            && f.cursor_near >= CURSOR_DWELL_SECS
            && f.follow_cooldown == 0.0
        {
            f.follow_left = 5.0 + 10.0 * p.curiosity + 10.0 * f.trust;
        } else if p.character == Character::Shy && f.cursor_near >= 0.6 && f.trust < 0.4 {
            f.cursor_near = 0.0;
            // Into a cave if there is one close by, otherwise just away
            match self.find_shelter(f, 900.0).filter(|_| f.school.is_none()) {
                Some(shelter) => {
                    f.mode = Mode::Hide {
                        shelter,
                        left: self.rng.range(6.0, 15.0),
                    }
                }
                None => f.flee_left = 2.0,
            }
        }

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
        let newcomer = (p.curiosity > 0.55 && self.daylight > 0.3)
            .then(|| self.newcomers.iter().find(|(id, _)| *id != f.id))
            .flatten()
            .and_then(|(id, _)| others.iter().find(|g| g.id == *id))
            .filter(|n| (n.x - f.x).hypot(n.y - f.y) < 700.0);

        let surface = match f.mode {
            Mode::Surface { left } => Some(left),
            _ => None,
        };
        // Off to lay eggs: swim to the nearest plant and leave them there
        let laying = match f.mode {
            Mode::Lay {
                partner,
                count,
                left,
            } => match self.nearest_nest(f.x, f.y) {
                Some((nx, ny)) if left > 0.0 && (nx - f.x).hypot(ny - f.y) > 25.0 => {
                    f.mode = Mode::Lay {
                        partner,
                        count,
                        left: left - dt,
                    };
                    Some((nx, ny, f.cruise.max(f.max_speed * 0.6)))
                }
                nest => {
                    // There (or took too long): lay them here
                    let (x, y) = nest.filter(|_| left > 0.0).unwrap_or((f.x, f.y));
                    self.lay_eggs(f.id, partner, x, y, count);
                    f.mode = Mode::Wander;
                    f.retarget = true;
                    None
                }
            },
            _ => None,
        };
        // In a cave: drift slowly about inside; on the way: head for it
        let hiding = match f.mode {
            Mode::Hide { shelter, left } if shelter < self.shelters.len() => {
                let s = self.shelters[shelter];
                let (cx, cy) = s.centre();
                let inside = (f.x - cx).abs() < (s.w - f.w).max(0.0) * 0.5 + 4.0
                    && (f.y - cy).abs() < (s.h - f.h).max(0.0) * 0.5 + 4.0;
                if inside {
                    let left = left - dt;
                    f.mode = if left > 0.0 {
                        Mode::Hide { shelter, left }
                    } else {
                        f.retarget = true;
                        Mode::Wander
                    };
                    let room = ((s.w - f.w) * 0.5).max(0.0);
                    let drift = (self.time * 0.25 + f.phase * 0.05).sin();
                    Some((
                        cx + drift * room,
                        cy + (s.h - f.h).max(0.0) * 0.15,
                        f.max_speed * 0.2,
                    ))
                } else {
                    // A frightened fish dashes for cover
                    let speed = if p.character == Character::Shy {
                        f.max_speed * 1.6
                    } else {
                        f.cruise * 1.2
                    };
                    Some((cx, cy, speed))
                }
            }
            Mode::Hide { .. } => {
                f.mode = Mode::Wander;
                None
            }
            _ => None,
        };
        let chase = if surface.is_some() {
            Some((
                f.x + f.dir * 30.0,
                // A little below the surface, so the bubbles have a way to rise
                self.water_top() + f.h * 2.0,
                f.max_speed * 1.8,
            ))
        } else if let Some((sx, sy, speed)) = laying {
            Some((sx + (mx - f.x), sy + (my - f.y), speed))
        } else if let Some((sx, sy, speed)) =
            hiding.filter(|_| nearest_food.is_none() || f.hunger < 0.8)
        {
            // Steer the body (not the mouth) to the spot in the cave
            Some((sx + (mx - f.x), sy + (my - f.y), speed))
        } else if let Some(j) = nearest_food {
            let eager = 1.0 + 0.5 * p.appetite + 0.4 * f.hunger;
            Some((
                self.food[j].x,
                self.food[j].y,
                f.max_speed * eager * (0.6 + 0.4 * self.daylight),
            ))
        } else if let Some(r) = rival {
            Some((r.x, r.y, f.max_speed * 1.3))
        } else if let (true, Some((px, py))) = (f.follow_left > 0.0, lure) {
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
        let mut goal_x;
        // A follower heading for its slot in a school turns toward the slot
        let mut goal_x_override = None;
        if let Some((tx, ty, speed)) = chase {
            goal_x = Some(tx);
            f.retarget = true;
            let (dx, dy) = (tx - mx, ty - my);
            let dist = dx.hypot(dy).max(0.001);
            if let Some(j) = nearest_food
                && dist < EAT_RADIUS
                && f.speed < f.max_speed * EAT_MAX_SPEED
            {
                self.food[j].eaten = true;
                f.hunger = (f.hunger - PELLET_NOURISHMENT).max(0.0);
                Self::ate_your_food(f);
                self.eaten_this_step.push((f.id, j));
                // Stop for a moment to swallow, with a little bubble, so it
                // reads as eating rather than bumping into the pellet
                f.mode = Mode::Eating {
                    left: self.rng.range(0.7, 1.2),
                };
                self.blow_bubbles(mx, my, 1, (2.5, 3.5), 0.2);
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
                    if f.body == Body::Turtle {
                        // Just a breath
                        self.blow_bubbles(mx, my, 2, (4.0, 6.0), 0.4);
                    } else {
                        self.blow_bubbles(mx, my, 9, (10.0, 18.0), 1.8);
                    }
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
                Mode::Wander
                | Mode::Chase { .. }
                | Mode::Surface { .. }
                | Mode::Eating { .. }
                | Mode::Hide { .. }
                | Mode::Lay { .. }
                | Mode::Petted { .. }
                | Mode::Bubbled => {
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
                        // Some fish spend the night in a cave; curious ones and
                        // youngsters pop in for a look now and then
                        let cave_chance = if night > 0.5 {
                            match p.character {
                                Character::Homebody | Character::Shy | Character::Lazy => 0.4,
                                _ if !f.adult => 0.4,
                                _ => 0.1,
                            }
                        } else {
                            match p.character {
                                Character::Explorer | Character::Playful => 0.06,
                                _ if !f.adult => 0.1,
                                _ => 0.02,
                            }
                        };
                        let shelter = (f.school.is_none() && self.rng.chance(cave_chance))
                            .then(|| self.find_shelter(f, self.width * 0.5))
                            .flatten();
                        if f.body == Body::Turtle && night < 0.5 && self.rng.chance(0.12) {
                            // Up for a breath of air
                            f.mode = Mode::Surface { left: 25.0 };
                        } else if let Some(shelter) = shelter {
                            let left = if night > 0.5 {
                                self.rng.range(40.0, 150.0)
                            } else {
                                self.rng.range(3.0, 9.0)
                            };
                            f.mode = Mode::Hide { shelter, left };
                        } else if self.rng.chance(rest.min(0.95)) {
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

                    // Playful fish dash off now and then
                    if p.character == Character::Playful && self.daylight > 0.4 {
                        f.dash_timer -= dt;
                        if f.dash_timer <= 0.0 {
                            f.dash_timer = self.rng.range(8.0, 25.0);
                            f.dash_left = 0.8;
                        }
                    }
                    if f.dash_left > 0.0 {
                        f.dash_left -= dt;
                        des_x = f.dir * f.max_speed * 2.2;
                    }
                }
            }

            // Shy fish swim away from a cursor that comes too close
            if f.flee_left > 0.0
                && let Some((px, py)) = lure
            {
                // A startled burst, faster than everyday swimming
                let (sx, sy) = (f.x - px, f.y - py);
                let d = sx.hypot(sy).max(1.0);
                des_x += sx / d * f.max_speed * 2.0;
                des_y += sy / d * f.max_speed * 1.2;
            }

            // Give others some room: drift away from fish close by that
            // aren't school mates or friends, so the tank stays evenly filled
            if matches!(f.mode, Mode::Wander)
                && chase.is_none()
                && f.school.is_none()
                && self.daylight > 0.3
            {
                const ROOM: f32 = 400.0;
                let (mut px, mut py) = (0.0, 0.0);
                for g in others.iter().filter(|g| g.id != f.id) {
                    let (sx, sy) = (f.x - g.x, f.y - g.y);
                    let d = sx.hypot(sy).max(1.0);
                    if d < ROOM && self.relations.get(f.id, g.id) <= FRIEND {
                        let push = (1.0 - d / ROOM) * if g.school.is_some() { 1.5 } else { 1.0 };
                        px += sx / d * push;
                        py += sy / d * push;
                    }
                }
                // Mostly sideways: up and down is where characters like to be
                let day = ((self.daylight - 0.3) / 0.4).clamp(0.0, 1.0);
                des_x += px * f.cruise * 1.2 * day;
                des_y += py * f.cruise * 0.5 * day;
            }

            // Friends swim side by side
            if matches!(f.mode, Mode::Wander)
                && let Some((friend, affinity)) = others
                    .iter()
                    .filter(|g| g.id != f.id && (g.x - f.x).hypot(g.y - f.y) < 450.0)
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

            // Schools: followers keep a slot in formation behind their leader;
            // a leader keeps its school away from other schools
            if let Some(id) = f.school
                && matches!(f.mode, Mode::Wander | Mode::Idle { .. })
                && let Some(school) = self.schools.iter().find(|sc| sc.id == id)
            {
                if school.leader == f.id {
                    for other in self.schools.iter().filter(|sc| sc.id != id) {
                        let (mut cx, mut cy, mut n) = (0.0, 0.0, 0.0);
                        for g in others.iter().filter(|g| g.school == Some(other.id)) {
                            cx += g.x;
                            cy += g.y;
                            n += 1.0;
                        }
                        if n > 0.0 {
                            let (sx, sy) = (f.x - cx / n, f.y - cy / n);
                            let d = sx.hypot(sy).max(1.0);
                            if d < SCHOOL_KEEP_APART {
                                let push =
                                    (SCHOOL_KEEP_APART - d) / SCHOOL_KEEP_APART * f.max_speed;
                                des_x += sx / d * push;
                                des_y += sy / d * push * 0.6;
                            }
                        }
                    }
                    // Lead at an easy pace so the others can keep up
                    des_x *= 0.8;
                    des_y *= 0.8;
                } else if let (Some(slot), Some(leader)) = (
                    school.members.iter().position(|m| *m == f.id),
                    others.iter().find(|g| g.id == school.leader),
                ) {
                    // Rows of two behind the leader, staggered above and below,
                    // spaced by body size so nobody overlaps
                    let row = (slot / 2 + 1) as f32;
                    let side = if slot % 2 == 0 { -1.0 } else { 1.0 };
                    let spacing = f.w.max(leader.w) * 0.85;
                    let gx = leader.x - leader.dir * row * spacing * 0.9;
                    let gy = leader.y + side * spacing * 0.55 * (1.0 + 0.15 * row);
                    let (dx, dy) = (gx - f.x, gy - f.y);
                    des_x = dx * 0.9 + leader.vx;
                    des_y = dy * 0.9 + leader.vy;
                    let limit = f.max_speed * 1.4;
                    let speed = des_x.hypot(des_y);
                    if speed > limit {
                        des_x *= limit / speed;
                        des_y *= limit / speed;
                    }
                    if dx.abs() > f.w * 0.5 {
                        goal_x_override = Some(gx);
                    }
                    f.mode = Mode::Wander;
                }
            }
        }

        if goal_x_override.is_some() {
            goal_x = goal_x_override;
        }

        // Separation, always: full strength for fish at a similar depth, and a
        // gentler push for ones further back or in front, which would otherwise
        // be drawn right over each other
        for (k, g) in others.iter().enumerate() {
            let depth_gap = (g.depth - f.depth).abs();
            if k == index || depth_gap > 0.7 {
                continue;
            }
            let strength = if depth_gap <= 0.35 { 1.0 } else { 0.35 }
                // Fish crowd in around food, as real ones do
                * if nearest_food.is_some() { 0.5 } else { 1.0 };
            let (sx, sy) = (f.x - g.x, (f.y - g.y) * 1.6);
            let d = sx.hypot(sy);
            // Shy fish keep a wider berth
            let shy = if p.character == Character::Shy {
                1.7
            } else {
                1.0
            };
            let min_d = (f.w + g.w) * 0.42 * shy;
            if d > 0.001 && d < min_d {
                des_x += sx / d * (min_d - d) * 1.6 * strength;
                des_y += sy / d * (min_d - d) * 1.6 * strength;
            }
        }

        if f.body == Body::Jelly {
            // A pulse of the bell pushes it up, then it sinks slowly; it
            // drifts sideways only a little and never turns round
            let pulse = (f.phase * 0.5).sin().max(0.0);
            let up = if f.ty < f.y - 20.0 { 1.3 } else { 0.45 };
            des_x *= 0.4;
            des_y = if pulse > 0.0 {
                -f.max_speed * 2.2 * pulse * up
            } else {
                f.max_speed * 0.35
            };
            goal_x = None;
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
        let climb = if surface.is_some() {
            1.5
        } else if f.body == Body::Jelly {
            2.5
        } else if f.flee_left > 0.0 {
            1.0
        } else {
            0.55
        };
        f.vy = f.vy.clamp(-climb * f.max_speed, climb * f.max_speed);

        f.x += f.vx * dt;
        f.y += f.vy * dt;
        self.clamp_to_water(f);
        // Behind the cave's front while going in, inside, and until it is
        // clear of the opening again on the way out
        f.sheltered = (f.sheltered || matches!(f.mode, Mode::Hide { .. }))
            && self.shelters.iter().any(|s| s.overlaps(f));

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
    /// Close, well-fed, rested adult friends now and then have a baby.
    fn maybe_have_baby(&mut self, i: usize, j: usize, life_dt: f32) {
        let (a, b) = (&self.fish[i], &self.fish[j]);
        let ready = |f: &Fish| f.adult && f.hunger < 0.4 && f.birth_cooldown == 0.0;
        if !ready(a)
            || !ready(b)
            || self.daylight < 0.3
            || self.relations.get(a.id, b.id) < PARENT_AFFINITY
        {
            return;
        }
        // Small fish breed more often, rest less in between and have litters
        let small = (a.smallness() + b.smallness()) * 0.5;
        if !self
            .rng
            .chance(BIRTH_CHANCE_PER_SEC * (1.0 + 3.0 * small) * life_dt)
        {
            return;
        }
        let count = if small > 0.5 {
            2 + self.rng.chance(small - 0.4) as u32
        } else {
            1
        };
        // Eggs, laid on the nearest plant (or right here when there are none)
        let (a_id, b_id, ax, ay) = (a.id, b.id, a.x, a.y);
        if self.nests.is_empty() {
            self.lay_eggs(a_id, b_id, ax, ay, count);
        } else {
            self.fish[i].mode = Mode::Lay {
                partner: b_id,
                count,
                left: 45.0,
            };
        }
        for k in [i, j] {
            self.fish[k].birth_cooldown =
                BIRTH_COOLDOWN * self.rng.range(1.0, 2.0) * (1.0 - 0.6 * small);
        }
    }

    /// Schools form now and then among nearby sociable fish, preferring their
    /// own kind, stay small (more fish make more schools), and break up after
    /// a minute or two. At most SCHOOL_SHARE of the fish school at once.
    fn update_schools(&mut self, dt: f32) {
        // Age schools; drop members that left the tank; disband small or old ones
        let present: HashSet<u64> = self.fish.iter().map(|f| f.id).collect();
        for school in &mut self.schools {
            school.left -= dt;
            school.members.retain(|m| present.contains(m));
        }
        // Fish of another size never school together (babies grow out of it)
        for school in &mut self.schools {
            if let Some(leader) = self.fish.iter().find(|f| f.id == school.leader) {
                let fish = &self.fish;
                school.members.retain(|m| {
                    fish.iter()
                        .find(|g| g.id == *m)
                        .is_some_and(|g| leader.same_size(g))
                });
            }
        }
        let schooled: HashSet<u64> = self
            .schools
            .iter()
            .flat_map(|sc| sc.members.iter().copied().chain([sc.leader]))
            .collect();
        for f in &mut self.fish {
            if f.school.is_some() && !schooled.contains(&f.id) {
                f.school = None;
            }
        }
        let mut gone: Vec<u32> = Vec::new();
        for school in &mut self.schools {
            if !present.contains(&school.leader) && !school.members.is_empty() {
                school.leader = school.members.remove(0);
            }
            if school.left <= 0.0 || school.members.is_empty() || !present.contains(&school.leader)
            {
                gone.push(school.id);
            }
        }
        if !gone.is_empty() {
            self.schools.retain(|sc| !gone.contains(&sc.id));
            for f in &mut self.fish {
                if f.school.is_some_and(|id| gone.contains(&id)) {
                    f.school = None;
                }
            }
        }

        self.school_check -= dt;
        if self.school_check > 0.0 || self.daylight < 0.4 {
            return;
        }
        self.school_check = SCHOOL_CHECK_SECS;

        let willing = |f: &Fish| {
            f.school.is_none()
                && !f.entering
                && f.body == Body::Fish
                && !matches!(f.mode, Mode::Hide { .. })
                && f.personality.character != Character::Shy
                && f.school_appetite() > 0.35
        };
        // Small fish make up more of the schooling share
        let small =
            self.fish.iter().map(|f| f.smallness()).sum::<f32>() / self.fish.len().max(1) as f32;
        let cap = (self.fish.len() as f32 * (SCHOOL_SHARE + 0.15 * small)) as usize;
        let max_size = |leader: &Fish| SCHOOL_MAX + if leader.smallness() > 0.4 { 2 } else { 0 };
        let schooling = |fish: &[Fish]| fish.iter().filter(|f| f.school.is_some()).count();

        // Lone fish join a nearby school of their kind that has room
        for i in 0..self.fish.len() {
            if schooling(&self.fish) >= cap {
                return;
            }
            let f = &self.fish[i];
            if !willing(f) || !self.rng.chance(0.15 * f.school_appetite()) {
                continue;
            }
            let (fx, fy, kind, id) = (f.x, f.y, f.sprite, f.id);
            let target = self.schools.iter().position(|sc| {
                sc.kind == kind
                    && self
                        .fish
                        .iter()
                        .find(|g| g.id == sc.leader)
                        .is_some_and(|l| {
                            sc.members.len() + 1 < max_size(l)
                                && l.same_size(f)
                                && (l.x - fx).hypot(l.y - fy) < SCHOOL_RADIUS * 0.7
                        })
            });
            if let Some(k) = target {
                self.schools[k].members.push(id);
                let school = self.schools[k].id;
                self.fish[i].school = Some(school);
            }
        }

        // A sociable lone fish may gather a new school around itself
        let start = (self.rng.unit() * self.fish.len().max(1) as f32) as usize;
        for step in 0..self.fish.len() {
            if schooling(&self.fish) + 2 > cap {
                return;
            }
            let i = (start + step) % self.fish.len();
            let f = &self.fish[i];
            if !willing(f) || !self.rng.chance(0.12 * f.school_appetite()) {
                continue;
            }
            let crowded = self.schools.iter().any(|sc| {
                self.fish
                    .iter()
                    .find(|g| g.id == sc.leader)
                    .is_some_and(|l| (l.x - f.x).hypot(l.y - f.y) < SCHOOL_KEEP_APART)
            });
            if crowded {
                continue;
            }
            let (fx, fy, kind, leader) = (f.x, f.y, f.sprite, f.id);
            let biggest = max_size(f);
            let mut nearby: Vec<(bool, f32, u64)> = self
                .fish
                .iter()
                .filter(|g| g.id != leader && willing(g) && f.same_size(g))
                .map(|g| (g.sprite == kind, (g.x - fx).hypot(g.y - fy), g.id))
                .filter(|&(_, d, _)| d < SCHOOL_RADIUS)
                .collect();
            // Own kind first, nearest first
            nearby.sort_by(|a, b| b.0.cmp(&a.0).then(a.1.total_cmp(&b.1)));
            let same_kind = nearby.iter().filter(|c| c.0).count();
            if same_kind == 0 {
                // No kin around: a mixed school (of one size) once in a while
                if nearby.is_empty() || !self.rng.chance(0.1) {
                    continue;
                }
            } else {
                nearby.truncate(same_kind);
            }
            let room = cap.saturating_sub(schooling(&self.fish) + 1);
            let size = ((self.rng.range(2.0, biggest as f32 + 0.99)) as usize)
                .min(room)
                .min(nearby.len());
            if size == 0 {
                continue;
            }
            let id = self.next_school;
            self.next_school = self.next_school.wrapping_add(1).max(1);
            let members: Vec<u64> = nearby.iter().take(size).map(|c| c.2).collect();
            for f in &mut self.fish {
                if f.id == leader || members.contains(&f.id) {
                    f.school = Some(id);
                }
            }
            self.schools.push(School {
                id,
                leader,
                members,
                kind,
                left: self.rng.range(40.0, 120.0),
            });
        }
    }

    /// Swallowing: the fish holds still for a moment, tail barely moving.
    fn step_eating(&mut self, f: &mut Fish, left: f32, dt: f32) {
        f.hunger = (f.hunger + dt * HUNGER_RATE * (0.6 + 0.8 * f.personality.appetite)).min(1.0);
        let brake = 1.0 - (6.0 * dt).min(1.0);
        f.vx *= brake;
        f.vy *= brake;
        f.x += f.vx * dt;
        f.y += f.vy * dt;
        self.clamp_to_water(f);
        f.speed = f.vx.hypot(f.vy);
        f.phase = (f.phase + dt * 1.5) % (TAU * 64.0);
        let left = left - dt;
        if left > 0.0 {
            f.mode = Mode::Eating { left };
        } else {
            // Back to whatever it was doing; food still around is noticed again
            f.mode = Mode::Wander;
            f.retarget = true;
        }
    }

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
                adult: true,
                mouth: DEFAULT_MOUTH,
                body: Body::Fish,
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
    fn a_fish_keeps_its_character_across_restarts() {
        let a = sim_with(1, 7, 1920.0, 1080.0);
        let b = sim_with(999, 7, 1920.0, 1080.0);
        for (fa, fb) in a.fish().iter().zip(b.fish()) {
            assert_eq!(fa.personality.character, fb.personality.character);
            assert_eq!(fa.band, fb.band);
        }
        // ...while different fish still get different characters
        let s = sim_with(1, 30, 1920.0, 1080.0);
        let kinds: HashSet<u32> = s
            .fish()
            .iter()
            .map(|f| f.personality.character.index())
            .collect();
        assert!(
            kinds.len() >= 5,
            "only {} different characters among 30 fish",
            kinds.len()
        );
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
            adult: true,
            mouth: DEFAULT_MOUTH,
            body: Body::Fish,
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
            adult: true,
            mouth: DEFAULT_MOUTH,
            body: Body::Fish,
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
                    adult: true,
                    mouth: DEFAULT_MOUTH,
                    body: Body::Fish,
                },
                FishSpec {
                    id: 2,
                    sprite: 0,
                    aspect: 1.2,
                    scale: 2.0,
                    adult: true,
                    mouth: DEFAULT_MOUTH,
                    body: Body::Fish,
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
        for seed in 1..21 {
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
            eaten > 0.7,
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
            adult: true,
            mouth: DEFAULT_MOUTH,
            body: Body::Fish,
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
        // For as long as a newcomer stays interesting
        for _ in 0..30 * 8 {
            s.step(DT, None);
        }
        let after = mean_distance(&s);
        assert!(
            after < before * 0.75,
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

    /// Two adults next to each other, best friends, fed and rested.
    fn happy_couple(adult: bool) -> Sim {
        happy_couple_sized(adult, 1.0)
    }

    fn happy_couple_sized(adult: bool, scale: f32) -> Sim {
        let mut s = Sim::new(21);
        s.set_population(
            &[
                FishSpec {
                    id: 1,
                    sprite: 0,
                    aspect: 0.6,
                    scale,
                    adult,
                    mouth: DEFAULT_MOUTH,
                    body: Body::Fish,
                },
                FishSpec {
                    id: 2,
                    sprite: 0,
                    aspect: 0.6,
                    scale,
                    adult,
                    mouth: DEFAULT_MOUTH,
                    body: Body::Fish,
                },
            ],
            false,
        );
        s.resize(1920.0, 1080.0);
        s.relations.adjust(1, 2, 0.95);
        s.fish.iter_mut().for_each(|f| {
            f.hunger = 0.0;
            f.birth_cooldown = 0.0;
            f.depth = 0.3;
        });
        s
    }

    fn keep_together(s: &mut Sim) {
        let (x, y) = (s.fish[0].x, s.fish[0].y);
        s.fish[1].x = x + 120.0;
        s.fish[1].y = y;
        s.fish.iter_mut().for_each(|f| f.hunger = 0.0);
    }

    #[test]
    fn best_friends_have_a_baby() {
        let mut s = happy_couple(true);
        s.set_life_speed(30.0);
        let mut first = None;
        for _ in 0..30 * 300 {
            keep_together(&mut s);
            s.step(DT, None);
            if let Some(b) = s.take_births().first() {
                first = Some(*b);
                break;
            }
        }
        let b = first.expect("no baby in what amounts to two and a half hours together");
        assert_eq!(
            (b.parent_a.min(b.parent_b), b.parent_a.max(b.parent_b)),
            (1, 2)
        );

        // Then the parents rest: at least BIRTH_COOLDOWN of life time (60 s here)
        for _ in 0..30 * 55 {
            keep_together(&mut s);
            s.step(DT, None);
            assert!(
                s.take_births().is_empty(),
                "a second baby before the parents rested"
            );
        }
    }

    #[test]
    fn eggs_are_laid_on_a_plant_and_hatch_there() {
        let mut s = happy_couple(true);
        let nest = (300.0, 800.0);
        s.set_nests(&[nest]);
        s.set_life_speed(30.0);
        let mut laid = None;
        for _ in 0..30 * 600 {
            // Keep them together until the mother sets off for the plant
            if !s.fish.iter().any(|f| matches!(f.mode, Mode::Lay { .. })) && s.eggs().is_empty() {
                keep_together(&mut s);
            }
            s.step(DT, None);
            if let Some(e) = s.eggs().first() {
                laid = Some(*e);
                break;
            }
        }
        let egg = laid.expect("no eggs were laid");
        assert!(
            (egg.x - nest.0).hypot(egg.y - nest.1) < 40.0,
            "eggs should be on the plant: {egg:?}"
        );
        assert!(
            s.take_births().is_empty(),
            "babies come out of the eggs, not straight away"
        );
        // Hatching takes HATCH_SECS of life time; parents stay around meanwhile
        let mut near = 0;
        let mut born = Vec::new();
        for n in 0..30 * 60 {
            s.step(DT, None);
            if n % 30 == 0
                && s.fish()
                    .iter()
                    .any(|f| (f.x - egg.x).hypot(f.y - egg.y) < 600.0)
            {
                near += 1;
            }
            born.extend(s.take_births());
            if !born.is_empty() {
                break;
            }
        }
        let b = born.first().expect("the eggs never hatched");
        assert!(
            (b.x - egg.x).abs() < 1.0 && (b.y - egg.y).abs() < 1.0,
            "babies hatch where the eggs were"
        );
        assert!(near > 5, "the parents should stay near their eggs ({near})");
    }

    #[test]
    fn small_fish_breed_more_and_have_litters() {
        let babies = |scale: f32| {
            let (mut events, mut babies) = (0, 0);
            for seed in 0..4 {
                let mut s = happy_couple_sized(true, scale);
                s.rng = Rng::new(100 + seed);
                s.set_life_speed(30.0);
                for _ in 0..30 * 600 {
                    keep_together(&mut s);
                    s.step(DT, None);
                    for b in s.take_births() {
                        events += 1;
                        babies += b.count;
                    }
                }
            }
            (events, babies)
        };
        let (big_events, big_babies) = babies(1.4);
        let (small_events, small_babies) = babies(0.4);
        assert!(big_babies == big_events, "big fish have one baby at a time");
        assert!(small_babies > small_events, "tiny fish should have litters");
        assert!(
            small_babies >= big_babies * 3,
            "tiny fish: {small_babies} babies, big fish: {big_babies}"
        );
    }

    #[test]
    fn small_fish_school_more() {
        let schooled = |scale: f32| {
            let mut share = 0.0;
            for seed in 1..4 {
                let mut s = Sim::new(seed);
                let specs: Vec<FishSpec> = (0..12u64)
                    .map(|k| FishSpec {
                        id: seed * 1000 + k,
                        sprite: (k % 2) as u32,
                        aspect: 0.6,
                        scale,
                        adult: true,
                        mouth: DEFAULT_MOUTH,
                        body: Body::Fish,
                    })
                    .collect();
                s.set_population(&specs, false);
                s.resize(2560.0, 1440.0);
                // Same middling sociability for everyone: only size differs
                s.fish.iter_mut().for_each(|f| {
                    f.personality.sociability = 0.4;
                    if f.personality.character == Character::Shy {
                        f.personality.character = Character::Explorer;
                    }
                    f.hunger = 0.0;
                });
                for _ in 0..30 * 240 {
                    s.step(DT, None);
                    share += s.fish().iter().filter(|f| f.school.is_some()).count() as f32;
                }
            }
            share / (3.0 * 30.0 * 240.0 * 12.0)
        };
        let (small, big) = (schooled(0.4), schooled(1.4));
        assert!(
            small > big * 1.3,
            "small fish schooled {:.0}% of the time, big {:.0}%",
            small * 100.0,
            big * 100.0
        );
    }

    #[test]
    fn young_fish_and_no_life_speed_mean_no_babies() {
        for (adult, speed) in [(false, 30.0), (true, 0.0)] {
            let mut s = happy_couple(adult);
            s.set_life_speed(speed);
            for _ in 0..30 * 300 {
                keep_together(&mut s);
                s.step(DT, None);
            }
            assert!(s.take_births().is_empty(), "adult={adult} speed={speed}");
        }
    }

    #[test]
    fn place_fish_moves_a_newcomer_next_to_its_parents() {
        let mut s = sim();
        let mut next = specs(7);
        next.push(FishSpec {
            id: 77,
            sprite: 0,
            aspect: 0.6,
            scale: 0.4,
            adult: false,
            mouth: DEFAULT_MOUTH,
            body: Body::Fish,
        });
        s.set_population(&next, true);
        assert!(s.place_fish(77, 900.0, 600.0));
        let baby = s.fish().iter().find(|f| f.id == 77).unwrap();
        assert!(!baby.entering && (baby.x - 900.0).abs() < 1.0 && (baby.y - 600.0).abs() < 1.0);
        assert!(!s.place_fish(12345, 0.0, 0.0));
    }

    /// One hungry fish and one pellet held at a spot
    fn fish_and_pellet(mouth: (f32, f32)) -> Sim {
        let mut s = Sim::new(31);
        s.set_population(
            &[FishSpec {
                id: 1,
                sprite: 0,
                aspect: 0.6,
                scale: 1.0,
                adult: true,
                mouth,
                body: Body::Fish,
            }],
            false,
        );
        s.resize(1920.0, 1080.0);
        let f = &mut s.fish[0];
        f.hunger = 1.0;
        f.food_aware = true;
        s
    }

    fn hold_pellet(s: &mut Sim, x: f32, y: f32) {
        if s.food.is_empty() {
            s.feed(Some(x));
            s.food.truncate(1);
        }
        let p = &mut s.food[0];
        p.x = x;
        p.y = y;
        p.landed = false;
    }

    #[test]
    fn a_pellet_on_the_body_is_not_eaten() {
        let mut s = fish_and_pellet(DEFAULT_MOUTH);
        // Pin the fish and keep a pellet just behind its mouth, on the head:
        // close enough that the old body-sized eating range swallowed it
        for _ in 0..30 * 3 {
            let (x, y) = (s.fish[0].x, s.fish[0].y);
            let (mx, my) = s.fish[0].mouth();
            let facing = s.fish[0].facing;
            hold_pellet(&mut s, mx - 25.0 * facing, my);
            s.step(DT, None);
            assert!(
                !s.food.is_empty() && !s.food[0].eaten,
                "eaten by touching the head, not the mouth"
            );
            s.fish[0].x = x;
            s.fish[0].y = y;
        }
    }

    #[test]
    fn eating_happens_at_the_mouth_and_the_fish_pauses() {
        // A fish whose mouth is marked on its belly, not its nose
        let mut s = fish_and_pellet((0.6, 0.9));
        let (px, py) = (900.0, 600.0);
        let mut eaten_at = None;
        for n in 0..30 * 20 {
            hold_pellet(&mut s, px, py);
            let (mx, my) = s.fish[0].mouth();
            s.step(DT, None);
            if s.food.is_empty() {
                eaten_at = Some((n, mx, my));
                break;
            }
        }
        let (_, mx, my) = eaten_at.expect("the fish never ate the pellet");
        assert!(
            (mx - px).hypot(my - py) < EAT_RADIUS + 3.0,
            "eaten away from the mouth"
        );
        assert!(
            matches!(s.fish[0].mode, Mode::Eating { .. }),
            "the fish should stop to eat"
        );
        let start = (s.fish[0].x, s.fish[0].y);
        for _ in 0..15 {
            s.step(DT, None);
        }
        let moved = (s.fish[0].x - start.0).hypot(s.fish[0].y - start.1);
        assert!(
            moved < 15.0,
            "the fish kept swimming while eating ({moved:.0}px)"
        );
    }

    #[test]
    fn the_mouth_turns_with_the_fish() {
        let mut s = fish_and_pellet(DEFAULT_MOUTH);
        let f = &mut s.fish[0];
        f.vx = 0.0;
        f.vy = 0.0;
        f.facing = 1.0;
        f.dir = 1.0;
        let right = f.mouth().0 - f.x;
        f.facing = -1.0;
        f.dir = -1.0;
        let left = f.mouth().0 - f.x;
        assert!(
            right > 0.3 * f.w && left < -0.3 * f.w,
            "mouth should be at the front: {right:.0} / {left:.0}"
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

    /// A cursor circling slowly around (x, y), as a hand on the mouse would
    fn wiggle(n: usize, x: f32, y: f32) -> Option<(f32, f32)> {
        let a = n as f32 * 0.15;
        Some((x + 25.0 * a.cos(), y + 25.0 * a.sin()))
    }

    fn make(s: &mut Sim, id: u64, character: Character, curiosity: f32) {
        let f = s.fish.iter_mut().find(|f| f.id == id).unwrap();
        f.personality.character = character;
        f.personality.curiosity = curiosity;
        f.hunger = 0.0;
    }

    #[test]
    fn cursor_must_linger_before_a_curious_fish_follows() {
        let mut s = sim_with(5, 2, 1920.0, 1080.0);
        make(&mut s, 1, Character::Explorer, 0.9);
        make(&mut s, 2, Character::Lazy, 0.1);
        // Park both fish together and hold the cursor right on them
        for f in s.fish.iter_mut() {
            f.x = 900.0;
            f.y = 500.0;
        }
        let (x, y) = (900.0, 500.0);
        for n in 0..15 {
            s.fish.iter_mut().for_each(|f| (f.x, f.y) = (x, y));
            s.step(DT, wiggle(n, x, y));
        }
        assert!(
            !s.lure_active(),
            "half a second is not enough to notice the cursor"
        );
        for n in 15..45 {
            s.fish.iter_mut().for_each(|f| (f.x, f.y) = (x, y));
            s.step(DT, wiggle(n, x, y));
        }
        let follows = |s: &Sim, id| s.fish().iter().find(|f| f.id == id).unwrap().follow_left > 0.0;
        assert!(
            follows(&s, 1),
            "the curious fish should follow after a second"
        );
        assert!(!follows(&s, 2), "an incurious fish never follows");

        // It follows the cursor as it moves away...
        let target = (400.0, 300.0);
        for n in 0..30 * 6 {
            let t = (n as f32 / 60.0).min(1.0);
            let (cx, cy) = (x + (target.0 - x) * t, y + (target.1 - y) * t);
            s.step(DT, wiggle(n, cx, cy));
        }
        let f = s.fish().iter().find(|f| f.id == 1).unwrap();
        assert!(
            (f.x - target.0).hypot(f.y - target.1) < 250.0,
            "follower should have come along"
        );
        // ...and gives up once the cursor stops moving for a while
        for _ in 0..30 * 8 {
            s.step(DT, Some(target));
        }
        assert!(
            !follows(&s, 1),
            "interest fades when the cursor is left alone"
        );
    }

    #[test]
    fn shy_fish_swim_away_from_a_lingering_cursor() {
        let mut s = sim_with(6, 1, 1920.0, 1080.0);
        make(&mut s, 1, Character::Shy, 0.9);
        s.fish[0].x = 900.0;
        s.fish[0].y = 500.0;
        for n in 0..30 * 3 {
            s.step(DT, wiggle(n, 900.0, 500.0));
        }
        let f = &s.fish()[0];
        assert!(f.follow_left == 0.0, "shy fish never follow");
        assert!(
            (f.x - 900.0).hypot(f.y - 500.0) > 150.0,
            "shy fish should have moved off"
        );
    }

    fn one_of(body: Body, seed: u64) -> Sim {
        let mut s = Sim::new(seed);
        s.set_population(
            &[FishSpec {
                id: 77 + seed,
                sprite: 0,
                aspect: if body == Body::Seahorse { 2.0 } else { 0.6 },
                scale: 1.0,
                adult: true,
                mouth: DEFAULT_MOUTH,
                body,
            }],
            false,
        );
        s.resize(2560.0, 1440.0);
        s.fish[0].hunger = 0.0;
        s
    }

    #[test]
    fn jellyfish_bob_up_and_down_and_drift_slowly() {
        let mut s = one_of(Body::Jelly, 1);
        let (mut dx, mut dy, mut ups) = (0.0, 0.0, 0);
        let mut last = (s.fish[0].x, s.fish[0].y);
        for n in 0..30 * 180 {
            s.step(DT, None);
            let f = &s.fish()[0];
            dx += (f.x - last.0).abs();
            dy += (f.y - last.1).abs();
            if n % 30 == 0 && f.vy < -10.0 {
                ups += 1;
            }
            assert_eq!(f.tilt(), 0.0, "jellyfish don't pitch");
            last = (f.x, f.y);
        }
        assert!(
            dy > dx,
            "mostly up and down: {dx:.0} px across, {dy:.0} px up/down"
        );
        assert!(ups > 10, "it should pulse upward regularly ({ups})");
        assert!(
            s.fish()[0].h > s.fish()[0].w * 0.5,
            "tall drawings stay tall"
        );
    }

    #[test]
    fn rays_glide_low_and_seahorses_are_slow() {
        let (mut ray_y, mut n) = (0.0, 0.0);
        let mut sea_speed: f32 = 0.0;
        let mut fish_speed: f32 = 0.0;
        for seed in 1..4 {
            let (mut ray, mut sea, mut fish) = (
                one_of(Body::Ray, seed),
                one_of(Body::Seahorse, seed),
                one_of(Body::Fish, seed),
            );
            for _ in 0..30 * 120 {
                ray.step(DT, None);
                sea.step(DT, None);
                fish.step(DT, None);
                ray_y += ray.fish()[0].y / 1440.0;
                n += 1.0;
                sea_speed = sea_speed.max(sea.fish()[0].speed);
                fish_speed = fish_speed.max(fish.fish()[0].speed);
            }
        }
        assert!(
            ray_y / n > 0.6,
            "rays should keep low (mean y {:.2})",
            ray_y / n
        );
        assert!(
            sea_speed < fish_speed * 0.6,
            "seahorses should be slow: {sea_speed:.0} vs {fish_speed:.0}"
        );
    }

    #[test]
    fn turtles_come_up_for_air() {
        let mut surfaced = false;
        for seed in 1..4 {
            let mut s = one_of(Body::Turtle, seed);
            for _ in 0..30 * 600 {
                s.step(DT, None);
                surfaced |= s.fish()[0].y < 1440.0 * (WATER_TOP + 0.1);
            }
        }
        assert!(surfaced, "a turtle never came up for air in 30 minutes");
    }

    /// A cave on the sand, big enough for medium fish
    const CAVE: Shelter = Shelter {
        x: 1300.0,
        y: 800.0,
        w: 320.0,
        h: 170.0,
    };

    #[test]
    fn a_shy_fish_hides_in_a_cave_close_by() {
        let mut s = sim_with(6, 1, 1920.0, 1080.0);
        s.set_shelters(&[CAVE]);
        make(&mut s, 1, Character::Shy, 0.9);
        s.fish[0].x = 900.0;
        s.fish[0].y = 600.0;
        let mut inside = 0;
        for n in 0..30 * 12 {
            s.step(DT, (n < 30 * 2).then(|| wiggle(n, 900.0, 600.0)).flatten());
            let f = &s.fish()[0];
            if f.sheltered && CAVE.overlaps(f) && (f.x - 1460.0).abs() < 160.0 {
                inside += 1;
            }
        }
        assert!(
            inside > 30 * 4,
            "the shy fish should spend a while in the cave ({inside} frames)"
        );
    }

    #[test]
    fn caves_are_used_at_night_and_fish_come_back_out() {
        let mut s = sim_with(12, 10, 2560.0, 1440.0);
        let cave = Shelter {
            x: 1100.0,
            y: 1080.0,
            w: 360.0,
            h: 200.0,
        };
        s.set_shelters(&[cave]);
        s.set_daylight(0.0);
        let (mut most_inside, mut too_big_inside) = (0, false);
        for _ in 0..30 * 300 {
            s.step(DT, None);
            let inside: Vec<&Fish> = s.fish().iter().filter(|f| f.sheltered).collect();
            most_inside = most_inside.max(inside.len());
            too_big_inside |= inside
                .iter()
                .any(|f| matches!(f.mode, Mode::Hide { .. }) && !cave.fits(f));
        }
        assert!(most_inside >= 1, "nobody went into the cave at night");
        assert!(!too_big_inside, "a fish too big for the opening went in");
        // Morning: everyone comes out within a few minutes
        s.set_daylight(1.0);
        for _ in 0..30 * 200 {
            s.step(DT, None);
        }
        let still = s
            .fish()
            .iter()
            .filter(|f| matches!(f.mode, Mode::Hide { left, .. } if left > 10.0))
            .count();
        assert!(
            still <= 2,
            "{still} fish stayed in for a long time after the night"
        );
    }

    #[test]
    fn a_full_cave_turns_fish_away() {
        let mut s = sim_with(3, 6, 1920.0, 1080.0);
        let small = Shelter {
            x: 800.0,
            y: 800.0,
            w: 200.0,
            h: 150.0,
        };
        s.set_shelters(&[small]);
        let mut fits = 0;
        for i in 0..s.fish.len() {
            let f = s.fish[i].clone();
            if let Some(k) = s.find_shelter(&f, 5000.0) {
                s.fish[i].mode = Mode::Hide {
                    shelter: k,
                    left: 100.0,
                };
                fits += 1;
            }
        }
        let taken: f32 = s
            .fish
            .iter()
            .filter(|f| matches!(f.mode, Mode::Hide { .. }))
            .map(|f| f.w)
            .sum();
        assert!(
            fits >= 1 && taken < small.w * 1.3 + 1.0,
            "{fits} fish, {taken:.0}px of fish in a 200px cave"
        );
    }

    #[test]
    fn fish_spread_over_the_whole_tank() {
        // Fraction of fish-time spent in each third of the width and height
        let (mut cols, mut rows, mut n) = ([0.0f32; 3], [0.0f32; 3], 0.0);
        for seed in 1..6 {
            // Characters come from the fish ids: use different fish every
            // round so this averages over many mixes of characters
            let mut s = Sim::new(seed);
            let specs: Vec<FishSpec> = (0..12)
                .map(|k| FishSpec {
                    id: seed * 100 + k,
                    sprite: (k % 4) as u32,
                    aspect: 0.6,
                    scale: 1.0,
                    adult: true,
                    mouth: DEFAULT_MOUTH,
                    body: Body::Fish,
                })
                .collect();
            s.set_population(&specs, false);
            s.resize(2560.0, 1440.0);
            for k in 0..30 * 180 {
                s.step(DT, None);
                if k % 15 == 0 {
                    for f in s.fish() {
                        cols[((f.x / 2560.0 * 3.0) as usize).min(2)] += 1.0;
                        let water = (f.y - 1440.0 * WATER_TOP) / (1440.0 * (SAND_TOP - WATER_TOP));
                        rows[((water * 3.0) as usize).min(2)] += 1.0;
                        n += 1.0;
                    }
                }
            }
        }
        // Regression guard: fish used to spend 55% of their time in the middle third
        for (name, parts) in [("columns", cols), ("rows", rows)] {
            for share in parts.map(|c| c / n) {
                assert!(
                    (0.22..0.45).contains(&share),
                    "{name} are uneven: {:?}",
                    parts.map(|c| (c / n * 100.0).round())
                );
            }
        }
    }

    /// A mixed tank like a real one: several drawings, a few sizes, many copies
    fn mixed_tank(seed: u64) -> Sim {
        let mut s = Sim::new(seed);
        let scales = [0.4, 0.6, 1.0, 1.0, 1.4, 1.9];
        let specs: Vec<FishSpec> = (0..30u64)
            .map(|k| FishSpec {
                id: seed * 1000 + k,
                sprite: (k % 6) as u32,
                aspect: 0.6,
                scale: scales[(k % 6) as usize],
                adult: true,
                mouth: DEFAULT_MOUTH,
                body: Body::Fish,
            })
            .collect();
        s.set_population(&specs, false);
        s.resize(2560.0, 1440.0);
        for (i, f) in s.fish.iter_mut().enumerate() {
            f.hunger = 0.1 + 0.6 * (i as f32 * 0.37).fract();
        }
        s
    }

    /// (neighbours within 300 px per fish, outside its own school; share of
    /// fish with 4+ neighbours of any school; share of fish heading for food
    /// 10 s after a feeding)
    fn clustering_and_food_rush(seed: u64) -> (f32, f32, f32) {
        let mut s = mixed_tank(seed);
        let (mut near, mut clumped, mut samples) = (0.0, 0.0, 0.0);
        for k in 0..30 * 300 {
            s.step(DT, None);
            if k % 30 == 0 {
                for f in s.fish() {
                    let n = s
                        .fish()
                        .iter()
                        .filter(|g| g.id != f.id && (f.school.is_none() || g.school != f.school))
                        .filter(|g| (g.x - f.x).hypot(g.y - f.y) < 300.0)
                        .count() as f32;
                    near += n;
                    let all = s
                        .fish()
                        .iter()
                        .filter(|g| g.id != f.id && (g.x - f.x).hypot(g.y - f.y) < 300.0)
                        .count();
                    clumped += (all >= 4) as u32 as f32;
                    samples += 1.0;
                }
            }
        }
        s.feed(Some(1280.0));
        for _ in 0..30 * 10 {
            s.step(DT, None);
        }
        let rushing =
            s.fish().iter().filter(|f| f.food_aware).count() as f32 / s.fish().len() as f32;
        (near / samples, clumped / samples, rushing)
    }

    #[test]
    fn fish_spread_out_and_dont_all_rush_to_food() {
        let (mut near, mut clump, mut rush) = (0.0, 0.0, 0.0);
        for seed in 1..5 {
            let (n, c, r) = clustering_and_food_rush(seed);
            near += n / 4.0;
            clump += c / 4.0;
            rush += r / 4.0;
        }
        // The same numbers for 30 points scattered uniformly over the water
        let mut rng = Rng::new(5);
        let (mut rnear, mut rclump, mut rn) = (0.0, 0.0, 0.0);
        for _ in 0..200 {
            let pts: Vec<(f32, f32)> = (0..30)
                .map(|_| {
                    (
                        rng.range(60.0, 2500.0),
                        rng.range(1440.0 * WATER_TOP + 40.0, 1440.0 * SAND_TOP - 30.0),
                    )
                })
                .collect();
            for (i, a) in pts.iter().enumerate() {
                let n = pts
                    .iter()
                    .enumerate()
                    .filter(|(j, b)| *j != i && (a.0 - b.0).hypot(a.1 - b.1) < 300.0)
                    .count();
                rnear += n as f32;
                rclump += (n >= 4) as u32 as f32;
                rn += 1.0;
            }
        }
        let report = format!(
            "neighbours within 300px: {near:.2} (random {:.2}), in clumps: {:.0}% (random {:.0}%), rushing to food: {:.0}%",
            rnear / rn,
            clump * 100.0,
            rclump / rn * 100.0,
            rush * 100.0
        );
        // Regression guard: 43% of fish used to be in clumps of 4+ (counting
        // schools) and 76% swam for the food at once
        assert!(near < rnear / rn * 0.8, "{report}");
        assert!(clump < 0.2, "{report}");
        assert!(rush < 0.55, "{report}");
    }

    #[test]
    fn schools_are_one_size() {
        for seed in 1..4 {
            let mut s = mixed_tank(seed);
            for _ in 0..30 * 240 {
                s.step(DT, None);
                for sc in &s.schools {
                    let leader = s.fish.iter().find(|f| f.id == sc.leader).unwrap();
                    for m in &sc.members {
                        let g = s.fish.iter().find(|f| f.id == *m).unwrap();
                        let ratio = g.w / leader.w;
                        assert!(
                            (0.85..1.18).contains(&ratio),
                            "a school mixes sizes: {ratio:.2}"
                        );
                    }
                }
            }
        }
    }

    #[test]
    fn every_fish_eats_its_own_size_of_food() {
        let mut s = Sim::new(12);
        let specs: Vec<FishSpec> = [0.4, 0.6, 1.0, 1.0, 1.9, 1.6]
            .iter()
            .enumerate()
            .map(|(k, scale)| FishSpec {
                id: 70 + k as u64,
                sprite: k as u32,
                aspect: 0.6,
                scale: *scale,
                adult: true,
                mouth: DEFAULT_MOUTH,
                body: Body::Fish,
            })
            .collect();
        s.set_population(&specs, false);
        s.resize(2560.0, 1440.0);
        s.feed(Some(1280.0));
        let sizes: std::collections::HashSet<u8> = s.food.iter().map(|p| p.size).collect();
        assert_eq!(
            sizes.len(),
            3,
            "small, medium and large fish: all three sizes fall"
        );
        // Track who eats what
        let mut eaten = Vec::new();
        for _ in 0..30 * 60 {
            s.fish.iter_mut().for_each(|f| f.hunger = 1.0);
            let before: Vec<(usize, u8)> = s
                .food
                .iter()
                .enumerate()
                .map(|(i, p)| (i, p.size))
                .collect();
            s.step(DT, None);
            for &(eater, pellet) in &s.eaten_this_step {
                let size = before[pellet].1;
                let f = s.fish.iter().find(|f| f.id == eater).unwrap();
                eaten.push((f.bite(), size));
            }
            if s.food.is_empty() {
                s.feed(None);
            }
        }
        assert!(
            eaten.len() > 10,
            "hardly anything was eaten ({})",
            eaten.len()
        );
        assert!(
            eaten.iter().all(|(bite, size)| bite == size),
            "someone ate the wrong size: {eaten:?}"
        );
    }

    #[test]
    fn food_is_noticed_one_fish_at_a_time() {
        let mut s = sim_with(8, 12, 2560.0, 1440.0);
        s.fish.iter_mut().for_each(|f| f.hunger = 0.8);
        s.feed(Some(1280.0));
        let aware = |s: &Sim| s.fish().iter().filter(|f| f.food_aware).count();
        for _ in 0..10 {
            s.step(DT, None);
        }
        let early = aware(&s);
        for _ in 0..30 * 8 {
            s.step(DT, None);
            s.fish.iter_mut().for_each(|f| f.hunger = 0.8);
        }
        let later = aware(&s);
        assert!(
            early <= 2,
            "{early} fish noticed the food within a third of a second"
        );
        assert!(
            later >= 6,
            "only {later} of 12 hungry fish noticed the food after 8 s"
        );
    }

    /// `kinds` drawings, `per_kind` copies of each, all sociable and full
    fn crowd(seed: u64, kinds: u32, per_kind: u64) -> Sim {
        let mut s = Sim::new(seed);
        let specs: Vec<FishSpec> = (0..kinds as u64 * per_kind)
            .map(|k| FishSpec {
                id: seed * 1000 + k,
                sprite: (k % kinds as u64) as u32,
                aspect: 0.6,
                scale: 1.0,
                adult: true,
                mouth: DEFAULT_MOUTH,
                body: Body::Fish,
            })
            .collect();
        s.set_population(&specs, false);
        s.resize(2560.0, 1440.0);
        s.fish.iter_mut().for_each(|f| {
            f.personality.sociability = 0.9;
            if f.personality.character == Character::Shy {
                f.personality.character = Character::Explorer;
            }
            f.hunger = 0.0;
        });
        s
    }

    #[test]
    fn some_fish_school_mostly_with_their_own_kind() {
        let (mut schooled, mut same_kind, mut samples) = (0.0, 0.0, 0.0);
        let mut most_schools = 0;
        for seed in 1..4 {
            let mut s = crowd(seed, 4, 6);
            for _ in 0..30 * 240 {
                s.step(DT, None);
                most_schools = most_schools.max(s.schools.len());
                for f in s.fish() {
                    samples += 1.0;
                    if let Some(id) = f.school {
                        schooled += 1.0;
                        let school = s.schools.iter().find(|sc| sc.id == id).unwrap();
                        if school.kind == f.sprite {
                            same_kind += 1.0;
                        }
                    }
                }
            }
        }
        let share = schooled / samples;
        assert!(
            (0.15..=0.55).contains(&share),
            "{:.0}% of fish-time in schools",
            share * 100.0
        );
        assert!(
            same_kind / schooled > 0.8,
            "schools should be mostly one kind"
        );
        assert!(
            most_schools >= 2,
            "24 fish should make more than one school"
        );
    }

    #[test]
    fn schools_keep_apart() {
        let (mut apart, mut close) = (0, 0);
        for seed in 1..4 {
            let mut s = crowd(seed, 2, 10);
            for k in 0..30 * 240 {
                s.step(DT, None);
                if k % 30 != 0 || s.schools.len() < 2 {
                    continue;
                }
                let centres: Vec<(f32, f32)> = s
                    .schools
                    .iter()
                    .map(|sc| {
                        let pts: Vec<&Fish> = s
                            .fish()
                            .iter()
                            .filter(|f| f.school == Some(sc.id))
                            .collect();
                        let n = pts.len() as f32;
                        (
                            pts.iter().map(|f| f.x).sum::<f32>() / n,
                            pts.iter().map(|f| f.y).sum::<f32>() / n,
                        )
                    })
                    .collect();
                for a in 0..centres.len() {
                    for b in a + 1..centres.len() {
                        let d = (centres[a].0 - centres[b].0).hypot(centres[a].1 - centres[b].1);
                        if d < 250.0 {
                            close += 1;
                        } else {
                            apart += 1;
                        }
                    }
                }
            }
        }
        assert!(
            apart + close > 20,
            "not enough moments with two schools to judge"
        );
        assert!(
            close * 10 < apart + close,
            "schools overlapped {close} of {} times",
            apart + close
        );
    }

    #[test]
    fn fish_rarely_overlap() {
        // Pairs at a similar depth whose bodies overlap by more than half
        let (mut overlaps, mut pairs) = (0.0, 0.0);
        for seed in 1..4 {
            let mut s = crowd(seed, 3, 6);
            for k in 0..30 * 180 {
                s.step(DT, None);
                if k % 15 != 0 {
                    continue;
                }
                let fish = s.fish();
                for a in 0..fish.len() {
                    for b in a + 1..fish.len() {
                        let (fa, fb) = (&fish[a], &fish[b]);
                        if (fa.depth - fb.depth).abs() > 0.35 {
                            continue;
                        }
                        pairs += 1.0;
                        let dx = (fa.x - fb.x).abs() / ((fa.w + fb.w) * 0.5);
                        let dy = (fa.y - fb.y).abs() / ((fa.h + fb.h) * 0.5);
                        if dx < 0.5 && dy < 0.5 {
                            overlaps += 1.0;
                        }
                    }
                }
            }
        }
        assert!(
            overlaps / pairs < 0.01,
            "{:.2}% of nearby pairs overlapped",
            overlaps / pairs * 100.0
        );
    }

    #[test]
    fn characters_shape_where_fish_swim() {
        let mean_y = |character: Character| {
            let mut total = 0.0;
            for seed in 1..4 {
                let mut s = sim_with(seed, 6, 1920.0, 1080.0);
                s.fish.iter_mut().for_each(|f| {
                    f.personality.character = character;
                    f.band = Character::band(character, &mut Rng::new(1));
                    f.hunger = 0.0;
                });
                let (mut y, mut n) = (0.0, 0.0);
                for k in 0..30 * 120 {
                    s.step(DT, None);
                    if k > 30 * 30 {
                        y += s.fish().iter().map(|f| f.y).sum::<f32>();
                        n += 6.0;
                    }
                }
                total += y / n;
            }
            total / 3.0
        };
        let (bottom, skimmer) = (mean_y(Character::Bottom), mean_y(Character::Skimmer));
        assert!(
            bottom > skimmer + 250.0,
            "bottom fish at {bottom:.0}, skimmers at {skimmer:.0}"
        );
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
