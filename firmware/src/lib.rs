//! Portable firmware logic. No FR machine code or board implementation yet.
//! TS enters only at the output of the unchanged legacy descrambler.
#![no_std]
#![forbid(unsafe_code)]

pub mod controller;
pub mod image;
pub mod stream;
