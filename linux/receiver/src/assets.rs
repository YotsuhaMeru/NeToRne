use crate::crypto::{hex, sha256};
use anyhow::{bail, ensure, Context, Result};
use md5::{Digest, Md5};
use std::{fs, path::Path};

pub fn be16(data: &[u8]) -> u32 {
    u16::from_be_bytes(data[..2].try_into().unwrap()) as u32
}
pub fn be32(data: &[u8]) -> u32 {
    u32::from_be_bytes(data[..4].try_into().unwrap())
}
fn be64(data: &[u8]) -> u64 {
    u64::from_be_bytes(data[..8].try_into().unwrap())
}

struct Segment {
    va: u64,
    offset: usize,
    size: usize,
    memory: u64,
}
pub struct Elf {
    data: Vec<u8>,
    segments: Vec<Segment>,
}
impl Elf {
    pub fn load(path: &Path, ppc: bool) -> Result<Self> {
        let data = fs::read(path).with_context(|| format!("Read {}", path.display()))?;
        let digest = if ppc {
            sha256(&data)
        } else {
            hex(&Md5::digest(&data))
        };
        ensure!(
            digest
                == if ppc {
                    "49b0b5c068efa7808cc4c13cb6bb7a909e33802ee5dfbf5a1a7c2ccbfedcbf5e"
                } else {
                    "59f5649fe320fc5cc5b3889a4499caa2"
                },
            "Unexpected local module: {}",
            path.display()
        );
        Self::parse(data, ppc)
    }
    fn parse(data: Vec<u8>, ppc: bool) -> Result<Self> {
        ensure!(
            data.len() >= 64
                && &data[..4] == b"\x7fELF"
                && data[5] == 2
                && data[4] == if ppc { 2 } else { 1 },
            "Invalid ELF header"
        );
        let offset: usize = if ppc {
            be64(&data[32..]).try_into()?
        } else {
            be32(&data[28..]) as usize
        };
        let stride = be16(&data[if ppc { 54 } else { 42 }..]) as usize;
        let count = be16(&data[if ppc { 56 } else { 44 }..]) as usize;
        ensure!(
            stride >= if ppc { 56 } else { 32 }
                && offset <= data.len()
                && count <= (data.len() - offset) / stride,
            "Invalid ELF program headers"
        );
        let mut segments = Vec::new();
        for n in 0..count {
            let p = &data[offset + n * stride..];
            if be32(p) != 1 {
                continue;
            }
            let (va, off, size, memory) = if ppc {
                (
                    be64(&p[16..]),
                    be64(&p[8..]),
                    be64(&p[32..]),
                    be64(&p[40..]),
                )
            } else {
                (
                    be32(&p[8..]) as u64,
                    be32(&p[4..]) as u64,
                    be32(&p[16..]) as u64,
                    be32(&p[20..]) as u64,
                )
            };
            let off: usize = off.try_into()?;
            let size: usize = size.try_into()?;
            ensure!(
                off <= data.len()
                    && size <= data.len() - off
                    && size as u64 <= memory
                    && va.checked_add(memory).is_some(),
                "Invalid ELF segment"
            );
            segments.push(Segment {
                va,
                offset: off,
                size,
                memory,
            });
        }
        Ok(Self { data, segments })
    }
    pub fn read(&self, address: u32, length: usize) -> Result<Vec<u8>> {
        let address = address as u64;
        for s in &self.segments {
            if address < s.va
                || address - s.va > s.memory
                || length as u64 > s.memory - (address - s.va)
            {
                continue;
            }
            let relative: usize = (address - s.va).try_into()?;
            let available = length.min(s.size.saturating_sub(relative));
            let mut out = vec![0; length];
            if available > 0 {
                out[..available].copy_from_slice(
                    &self.data[s.offset + relative..s.offset + relative + available],
                );
            }
            return Ok(out);
        }
        bail!("Unmapped module address {address:x}")
    }
    pub fn pointer(&self, offset: i32) -> Result<u32> {
        Ok(be32(&self.read((0x45830i32 + offset) as u32, 4)?))
    }
}

pub struct Assets {
    pub module: Elf,
    pub idle: Vec<u8>,
    pub operational: Vec<u8>,
    pub iv: Vec<u8>,
    pub firmware_sha256: String,
    pub variant: &'static str,
}
impl Assets {
    #[cfg(test)]
    pub fn fixture() -> Self {
        Self {
            module: Elf {
                data: Vec::new(),
                segments: Vec::new(),
            },
            idle: Vec::new(),
            operational: Vec::new(),
            iv: vec![0; 16],
            firmware_sha256: String::new(),
            variant: "test",
        }
    }
    pub fn load(root: &Path, operational: &Path) -> Result<Self> {
        let idle_path = root.join("BCJB95006/USRDIR/mb86h47_aac_idle.bin");
        let idle = fs::read(&idle_path).with_context(|| format!("Read {}", idle_path.display()))?;
        ensure!(
            sha256(&idle) == "d9acae79f9e0ecaecb4b491af8d30e95ba7ca949adf5c2b6397d25a00508691d",
            "Unexpected idle firmware"
        );
        let path = root.join(operational);
        let operational = fs::read(&path).with_context(|| format!("Read {}", path.display()))?;
        let firmware_sha256 = sha256(&operational);
        let variant = match firmware_sha256.as_str() {
            "653f2377c838a4e7276b447fd992084d5e6e84a90ac4d389851f82309b662a04" => "original",
            "5aa34270a7647264a5676867fdbbdd22b2c0cb6bb0644cf7c136e5d60bf7edcc" => "psi-passthrough",
            _ => bail!(
                "Unexpected operational firmware: {} (SHA-256 {firmware_sha256})",
                path.display()
            ),
        };
        let module = Elf::load(&root.join("research/decrypted/PvrRecSvr.prx"), true)?;
        let media = Elf::load(&root.join("research/decrypted/libM2tsAes.elf"), false)?;
        let iv = media.read(0x6b50, 16)?;
        Ok(Self {
            module,
            idle,
            operational,
            iv,
            firmware_sha256,
            variant,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn elf_bounds_and_zero_fill() {
        let mut data = vec![0; 124];
        data[..6].copy_from_slice(b"\x7fELF\x02\x02");
        data[32..40].copy_from_slice(&64u64.to_be_bytes());
        data[54..56].copy_from_slice(&56u16.to_be_bytes());
        data[56..58].copy_from_slice(&1u16.to_be_bytes());
        data[64..68].copy_from_slice(&1u32.to_be_bytes());
        data[72..80].copy_from_slice(&120u64.to_be_bytes());
        data[80..88].copy_from_slice(&0x1000u64.to_be_bytes());
        data[96..104].copy_from_slice(&4u64.to_be_bytes());
        data[104..112].copy_from_slice(&8u64.to_be_bytes());
        data[120..].copy_from_slice(&[1, 2, 3, 4]);
        let elf = Elf::parse(data.clone(), true).unwrap();
        assert_eq!(elf.read(0x1002, 6).unwrap(), [3, 4, 0, 0, 0, 0]);
        assert!(elf.read(0x1007, 2).is_err());
        data[96..104].copy_from_slice(&5u64.to_be_bytes());
        assert!(Elf::parse(data, true).is_err());
    }

    #[test]
    #[ignore = "requires user-provided torne 1.10 files and TORNE_TEST_ROOT"]
    fn validate_user_assets() {
        let root = std::env::var_os("TORNE_TEST_ROOT")
            .expect("Set TORNE_TEST_ROOT to the local asset directory");
        let root = Path::new(&root);
        let assets = Assets::load(root, Path::new("BCJB95006/USRDIR/mb86h47_aac_trc.bin")).unwrap();
        assert_eq!(assets.variant, "original");
        assert_eq!(assets.iv.len(), 16);
        for (ptr, length) in [
            (-0x7dcc, 0x25c),
            (-0x7db0, 16),
            (-0x7d5c, 0x500),
            (-0x7d58, 0x210),
            (-0x7cf0, 0xa4),
            (-0x7d04, 0x5c),
            (-0x7cf8, 0x2a0),
        ] {
            assets
                .module
                .read(assets.module.pointer(ptr).unwrap(), length)
                .unwrap();
        }
    }
}
