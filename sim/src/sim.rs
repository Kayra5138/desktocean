use crate::rng::Rng;

/// Fraction of the height where the sand starts.
pub const SAND_TOP: f32 = 0.86;
/// Fraction of the height where the water surface is.
pub const WATER_TOP: f32 = 0.05;
pub const MAX_FOOD: usize = 24;
/// Food lies on the sand this long before it dissolves.
const FOOD_LIFETIME: f32 = 10.0;
/// The curious fish loses interest once the cursor has been still this long.
const POINTER_INTEREST_SECS: f32 = 4.0;

#[derive(Debug, Clone)]
pub struct Fish {
    pub x: f32,
    pub y: f32,
    pub vx: f32,
    pub vy: f32,
    /// Wander target
    pub tx: f32,
    pub ty: f32,
    pub cruise: f32,
    /// 0 = front, 1 = back; drives size, speed, fog and z-order
    pub depth: f32,
    pub w: f32,
    pub h: f32,
    pub max_speed: f32,
    /// -1 = facing left, 1 = facing right; in between while turning
    pub facing: f32,
    /// Tail-wave phase, radians
    pub phase: f32,
    pub speed: f32,
    pub curious: bool,
}

impl Fish {
    fn mouth(&self) -> (f32, f32) {
        (self.x + self.facing * self.w * 0.42, self.y + self.h * 0.06)
    }

    /// Tail swing as a fraction of body height; livelier when swimming fast.
    pub fn amplitude(&self) -> f32 {
        0.05 + 0.07 * (self.speed / self.max_speed).min(1.0)
    }

    /// Body pitch in degrees, nose following the vertical velocity.
    pub fn tilt(&self) -> f32 {
        let deg = self.vy.atan2(self.vx.abs() + 20.0).to_degrees() * 0.7;
        deg.clamp(-28.0, 28.0) * if self.facing < 0.0 { -1.0 } else { 1.0 }
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

#[derive(Debug)]
pub struct Sim {
    width: f32,
    height: f32,
    fish_count: usize,
    fish: Vec<Fish>,
    food: Vec<Food>,
    time: f32,
    rng: Rng,
    last_pointer: Option<(f32, f32)>,
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
            last_pointer: None,
            pointer_still_for: f32::INFINITY,
        }
    }

    pub fn fish(&self) -> &[Fish] {
        &self.fish
    }

    pub fn food(&self) -> &[Food] {
        &self.food
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

    fn pick_target(&mut self, f: &mut Fish) {
        let (wt, st) = (self.water_top(), self.sand_top());
        f.tx = self
            .rng
            .range(f.w * 0.6, (self.width - f.w * 0.6).max(f.w * 0.6 + 1.0));
        f.ty = self
            .rng
            .range(wt + f.h, (st - f.h * 0.7).max(wt + f.h + 1.0));
        f.cruise = self.rng.range(0.45, 1.0) * f.max_speed;
    }

    fn clamp_to_water(&self, f: &mut Fish) {
        let half = f.w * 0.5;
        f.x = f.x.clamp(half, (self.width - half).max(half));
        let top = self.water_top() + f.h * 0.5;
        f.y = f.y.clamp(top, (self.sand_top() - f.h * 0.3).max(top));
    }

    fn reset_fish(&mut self) {
        let mut list = Vec::with_capacity(self.fish_count);
        for i in 0..self.fish_count {
            let depth = self.rng.unit();
            let w = self.rng.range(150.0, 230.0) * (1.0 - 0.45 * depth);
            let mut f = Fish {
                x: self.rng.range(0.0, self.width),
                y: self.rng.range(self.water_top(), self.sand_top()),
                vx: 0.0,
                vy: 0.0,
                tx: 0.0,
                ty: 0.0,
                cruise: 0.0,
                depth,
                w,
                h: w * 0.6,
                max_speed: self.rng.range(60.0, 115.0) * (1.0 - 0.3 * depth),
                facing: if self.rng.chance(0.5) { -1.0 } else { 1.0 },
                phase: self.rng.range(0.0, std::f32::consts::TAU),
                speed: 0.0,
                curious: i == 0,
            };
            self.clamp_to_water(&mut f);
            self.pick_target(&mut f);
            list.push(f);
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
            if self.food.len() >= MAX_FOOD {
                break;
            }
            let food = Food {
                x: cx + self.rng.range(-90.0, 90.0),
                y: wt + self.rng.range(-10.0, 30.0),
                seed: self.rng.range(0.0, std::f32::consts::TAU),
                age: 0.0,
                landed: false,
                eaten: false,
            };
            self.food.push(food);
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

        // The cursor only counts as interesting while it moves.
        match (pointer, self.last_pointer) {
            (Some((x, y)), Some((lx, ly))) if (x - lx).hypot(y - ly) < 2.0 => {
                self.pointer_still_for += dt
            }
            (Some(_), _) => self.pointer_still_for = 0.0,
            (None, _) => self.pointer_still_for = f32::INFINITY,
        }
        self.last_pointer = pointer;
        let wt = self.water_top();
        let st = self.sand_top();
        let lure = pointer
            .filter(|&(_, y)| self.pointer_still_for < POINTER_INTEREST_SECS && y > wt && y < st);

        let time = self.time;
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

        // Neighbour snapshot so separation reads a consistent frame.
        let others: Vec<(f32, f32, f32, f32)> =
            self.fish.iter().map(|g| (g.x, g.y, g.w, g.depth)).collect();

        for i in 0..self.fish.len() {
            let mut f = self.fish[i].clone();
            let (mx, my) = f.mouth();

            let nearest_food = self
                .food
                .iter()
                .enumerate()
                .filter(|(_, p)| !p.eaten)
                .map(|(j, p)| (j, (p.x - mx).hypot(p.y - my)))
                .min_by(|a, b| a.1.total_cmp(&b.1))
                .map(|(j, _)| j);

            let (tx, ty, speed, from_mouth) = if let Some(j) = nearest_food {
                (self.food[j].x, self.food[j].y, f.max_speed * 1.6, true)
            } else if let (true, Some((px, py))) = (f.curious, lure) {
                (px - f.facing * 40.0, py, f.max_speed * 0.8, true)
            } else {
                (f.tx, f.ty, f.cruise, false)
            };

            let (ox, oy) = if from_mouth { (mx, my) } else { (f.x, f.y) };
            let (dx, dy) = (tx - ox, ty - oy);
            let dist = dx.hypot(dy).max(0.001);

            if !from_mouth && dist < 30.0 {
                self.pick_target(&mut f);
            }
            if let Some(j) = nearest_food
                && dist < (f.w * 0.12).max(14.0)
            {
                self.food[j].eaten = true;
            }

            let arrive = (dist / 90.0).min(1.0);
            let mut des_x = dx / dist * speed * arrive;
            let mut des_y = dy / dist * speed * arrive;

            for (k, &(gx, gy, gw, gdepth)) in others.iter().enumerate() {
                if k == i || (gdepth - f.depth).abs() > 0.35 {
                    continue;
                }
                let (sx, sy) = (f.x - gx, (f.y - gy) * 1.6);
                let d = sx.hypot(sy);
                let min_d = (f.w + gw) * 0.38;
                if d > 0.001 && d < min_d {
                    des_x += sx / d * (min_d - d) * 1.5;
                    des_y += sy / d * (min_d - d) * 1.5;
                }
            }

            let k = (1.6 * dt).min(1.0);
            f.vx += (des_x - f.vx) * k;
            f.vy += (des_y - f.vy) * k;
            f.vy = f.vy.clamp(-0.55 * f.max_speed, 0.55 * f.max_speed);

            f.x += f.vx * dt;
            f.y += f.vy * dt;
            self.clamp_to_water(&mut f);

            if f.vx.abs() > 10.0 {
                let want = if f.vx > 0.0 { 1.0 } else { -1.0 };
                f.facing += (want - f.facing) * (3.5 * dt).min(1.0);
            }

            f.speed = f.vx.hypot(f.vy);
            f.phase = (f.phase + dt * (3.0 + f.speed * 0.07)) % (std::f32::consts::TAU * 64.0);
            self.fish[i] = f;
        }

        self.food.retain(|p| !p.eaten && p.age < FOOD_LIFETIME);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

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
    fn fish_stay_in_the_water() {
        let mut s = sim();
        for _ in 0..30 * 120 {
            s.step(1.0 / 30.0, None);
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
    fn food_gets_eaten_or_dissolves() {
        let mut s = sim();
        s.feed(Some(960.0));
        assert_eq!(s.food().len(), 8);
        for _ in 0..30 * 40 {
            s.step(1.0 / 30.0, None);
        }
        assert!(s.food().is_empty());
    }

    #[test]
    fn food_is_capped() {
        let mut s = sim();
        for _ in 0..10 {
            s.feed(None);
        }
        assert_eq!(s.food().len(), MAX_FOOD);
    }

    #[test]
    fn curious_fish_approaches_moving_cursor_then_gets_bored() {
        let mut s = sim();
        let target = (300.0, 500.0);
        // Wiggle the cursor so it counts as moving; even the slowest fish gets there in 25 s.
        for n in 0..30 * 25 {
            let wiggle = if n % 2 == 0 { 3.0 } else { -3.0 };
            s.step(1.0 / 30.0, Some((target.0 + wiggle, target.1)));
        }
        let f = &s.fish()[0];
        let near = (f.x - target.0).hypot(f.y - target.1);
        assert!(near < 150.0, "curious fish did not approach: {near}px away");

        // Cursor held still: interest fades after POINTER_INTEREST_SECS.
        for _ in 0..30 * 5 {
            s.step(1.0 / 30.0, Some(target));
        }
        assert!(s.pointer_still_for >= POINTER_INTEREST_SECS);
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
