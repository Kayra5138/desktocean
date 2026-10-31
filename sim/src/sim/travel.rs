//! Fish travelling between screens. The host says which side edges have
//! another screen next to them; now and then a fish swims off through one of
//! those edges and the host hands it to the tank on the other side, where it
//! swims in from the edge.

use super::{Character, Fish, Sim};

/// A fish swam out of the tank through a side edge.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Departure {
    pub id: u64,
    /// -1 left edge, 1 right edge
    pub side: i32,
    /// Where it was, scene pixels
    pub y: f32,
}

impl Sim {
    /// Which side edges lead to another screen
    pub fn set_exits(&mut self, left: bool, right: bool) {
        self.exits = (left, right);
        // A fish heading for an edge that closed turns back
        for f in &mut self.fish {
            if (f.travel < 0 && !left) || (f.travel > 0 && !right) {
                f.travel = 0;
                f.retarget = true;
            }
        }
    }

    pub fn take_departures(&mut self) -> Vec<Departure> {
        std::mem::take(&mut self.departures)
    }

    /// A fish (already in the population) arrives through a side edge at `y`
    /// and swims in
    pub fn arrive(&mut self, id: u64, side: i32, y: f32) -> bool {
        let width = self.width;
        let Some(f) = self.fish.iter_mut().find(|f| f.id == id) else {
            return false;
        };
        let inward = if side < 0 { 1.0 } else { -1.0 };
        f.x = if side < 0 {
            -f.w * 0.5
        } else {
            width + f.w * 0.5
        };
        f.y = y;
        f.dir = inward;
        f.facing = inward;
        f.vx = inward * f.max_speed * 0.6;
        f.vy = 0.0;
        f.entering = false;
        f.arriving = true;
        f.travel = 0;
        f.tx = if side < 0 {
            f.w * 2.0
        } else {
            width - f.w * 2.0
        };
        f.ty = y;
        f.mode = super::Mode::Wander;
        f.cruise = f.max_speed * 0.7;
        true
    }

    /// Maybe set off for another screen instead of a new spot in this one
    pub(super) fn maybe_travel(&mut self, f: &mut Fish) -> bool {
        if !(self.exits.0 || self.exits.1)
            || f.school.is_some()
            || !f.adult
            || f.entering
            || f.arriving
        {
            return false;
        }
        let chance = match f.personality.character {
            Character::Explorer => 0.05,
            Character::Homebody | Character::Shy => 0.004,
            _ => 0.015,
        };
        if !self.rng.chance(chance) {
            return false;
        }
        // The open edge, or the nearer one when both are
        let side = match self.exits {
            (true, false) => -1,
            (false, true) => 1,
            _ if f.x < self.width * 0.5 => -1,
            _ => 1,
        };
        f.travel = side;
        f.tx = if side < 0 {
            -f.w * 2.0
        } else {
            self.width + f.w * 2.0
        };
        f.ty = f.y;
        f.cruise = f.max_speed * self.rng.range(0.6, 0.9);
        f.mode = super::Mode::Wander;
        true
    }

    /// Fish that left the tank go to the departures; arrivals that are in stop arriving
    pub(super) fn update_travel(&mut self) {
        let width = self.width;
        let mut gone = Vec::new();
        for f in &mut self.fish {
            if f.arriving && f.x > f.w * 0.5 && f.x < width - f.w * 0.5 {
                f.arriving = false;
            }
            if f.travel != 0 && (f.x < -f.w * 0.5 || f.x > width + f.w * 0.5) {
                gone.push(Departure {
                    id: f.id,
                    side: f.travel,
                    y: f.y,
                });
            }
        }
        if gone.is_empty() {
            return;
        }
        self.fish.retain(|f| !gone.iter().any(|d| d.id == f.id));
        self.population
            .retain(|s| !gone.iter().any(|d| d.id == s.id));
        self.departures.extend(gone);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sim::{DEFAULT_MOUTH, FishSpec};

    const DT: f32 = 1.0 / 30.0;

    fn tank(n: u64) -> Sim {
        let mut s = Sim::new(8);
        let specs: Vec<FishSpec> = (0..n)
            .map(|k| FishSpec {
                id: 40 + k,
                sprite: 0,
                aspect: 0.6,
                scale: 1.0,
                adult: true,
                mouth: DEFAULT_MOUTH,
                body: Default::default(),
                speed: 1.0,
            })
            .collect();
        s.set_population(&specs, false);
        s.resize(1920.0, 1080.0);
        s
    }

    #[test]
    fn fish_leave_only_through_open_edges() {
        let mut s = tank(8);
        s.set_exits(false, true);
        let mut left = Vec::new();
        for _ in 0..30 * 600 {
            s.step(DT, None);
            left.extend(s.take_departures());
        }
        assert!(
            !left.is_empty(),
            "nobody went over to the next screen in 10 minutes"
        );
        assert!(
            left.iter().all(|d| d.side == 1),
            "fish may only leave on the right"
        );
        assert_eq!(s.fish().len() + left.len(), 8, "every fish is somewhere");
    }

    #[test]
    fn no_neighbours_no_travel() {
        let mut s = tank(8);
        for _ in 0..30 * 300 {
            s.step(DT, None);
            assert!(s.take_departures().is_empty());
            assert!(s.fish().iter().all(|f| f.x >= 0.0 && f.x <= 1920.0));
        }
    }

    #[test]
    fn an_arriving_fish_swims_in_from_the_edge() {
        let mut s = tank(1);
        assert!(s.arrive(40, -1, 500.0));
        assert!(s.fish()[0].x < 0.0, "it starts just outside the left edge");
        for _ in 0..30 * 4 {
            s.step(DT, None);
        }
        let f = &s.fish()[0];
        assert!(f.x > f.w * 0.5, "it should have swum in (x {})", f.x);
        assert!(!f.arriving);
    }
}
