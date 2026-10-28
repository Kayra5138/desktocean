//! Desktocean simulation core.
//!
//! Pure Rust, no dependencies. `sim` holds the model; `ffi` exposes it to the
//! C++ QML plugin through a small C ABI.

pub mod ffi;
pub mod rng;
pub mod sim;

pub use sim::{Fish, Food, Sim};
