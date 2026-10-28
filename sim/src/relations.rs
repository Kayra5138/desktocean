//! How much each pair of fish likes each other.
//!
//! Affinity lives in -1..1: it rises while two fish swim near each other and
//! drops when one snatches food the other was after, and slowly fades back
//! toward 0 otherwise. Above [`FRIEND`] they are friends, below [`RIVAL`] rivals.

use std::collections::{HashMap, HashSet};

pub const FRIEND: f32 = 0.5;
pub const RIVAL: f32 = -0.4;
/// Fraction of affinity lost per second, so old feelings fade over hours.
const FADE_PER_SEC: f32 = 0.0002;

#[derive(Debug, Default, Clone)]
pub struct Relations {
    affinity: HashMap<(u64, u64), f32>,
}

fn key(a: u64, b: u64) -> (u64, u64) {
    if a < b { (a, b) } else { (b, a) }
}

impl Relations {
    pub fn get(&self, a: u64, b: u64) -> f32 {
        self.affinity.get(&key(a, b)).copied().unwrap_or(0.0)
    }

    pub fn adjust(&mut self, a: u64, b: u64, delta: f32) {
        if a == b {
            return;
        }
        let v = self.affinity.entry(key(a, b)).or_insert(0.0);
        *v = (*v + delta).clamp(-1.0, 1.0);
    }

    pub fn fade(&mut self, dt: f32) {
        let keep = 1.0 - FADE_PER_SEC * dt;
        // Only drop values far below what a single step of bonding adds (~1e-4)
        self.affinity.retain(|_, v| {
            *v *= keep;
            v.abs() > 1e-6
        });
    }

    /// Forgets fish that are no longer in the tank.
    pub fn retain_ids(&mut self, ids: &HashSet<u64>) {
        self.affinity
            .retain(|(a, b), _| ids.contains(a) && ids.contains(b));
    }

    pub fn export(&self) -> Vec<(u64, u64, f32)> {
        let mut list: Vec<_> = self
            .affinity
            .iter()
            .map(|(&(a, b), &v)| (a, b, v))
            .collect();
        list.sort_by_key(|&(a, b, _)| (a, b));
        list
    }

    pub fn import(&mut self, list: &[(u64, u64, f32)]) {
        self.affinity = list
            .iter()
            .filter(|(a, b, v)| a != b && v.is_finite())
            .map(|&(a, b, v)| (key(a, b), v.clamp(-1.0, 1.0)))
            .collect();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn symmetric_and_clamped() {
        let mut r = Relations::default();
        r.adjust(3, 1, 0.7);
        r.adjust(1, 3, 0.7);
        assert_eq!(r.get(1, 3), 1.0);
        assert_eq!(r.get(3, 1), 1.0);
        r.adjust(2, 2, 0.5);
        assert_eq!(r.get(2, 2), 0.0);
    }

    #[test]
    fn small_steps_accumulate() {
        let mut r = Relations::default();
        for _ in 0..100 {
            r.adjust(1, 2, 1e-4);
            r.fade(1.0 / 30.0);
        }
        assert!(r.get(1, 2) > 0.009);
    }

    #[test]
    fn export_import_roundtrip() {
        let mut r = Relations::default();
        r.adjust(1, 2, 0.6);
        r.adjust(5, 4, -0.5);
        let mut back = Relations::default();
        back.import(&r.export());
        assert_eq!(back.get(2, 1), 0.6);
        assert_eq!(back.get(4, 5), -0.5);
    }

    #[test]
    fn forgets_missing_fish() {
        let mut r = Relations::default();
        r.adjust(1, 2, 0.6);
        r.adjust(1, 3, 0.6);
        r.retain_ids(&[1, 2].into_iter().collect());
        assert_eq!(r.get(1, 3), 0.0);
        assert_eq!(r.get(1, 2), 0.6);
    }
}
