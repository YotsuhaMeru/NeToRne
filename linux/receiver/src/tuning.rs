// RF/demodulator tables are read from the validated local PvrRecSvr module.
// Register order follows bondriver/TorneNative.cpp (DiB8000C/UHF path).
use crate::{
    assets::{be16, be32},
    protocol::{Session, Transport},
};
use anyhow::{bail, ensure, Result};
use std::time::{Duration, Instant};

#[derive(Clone, Copy)]
struct Layer {
    segments: u32,
    modulation: u32,
    rate: u32,
    interleave: u32,
}
#[derive(Clone, Copy)]
struct Tmcc {
    mode: u32,
    guard: u32,
    inversion: u32,
    partial: u32,
    parameter: u32,
    layers: [Layer; 3],
}

impl<T: Transport> Session<T> {
    fn gpio(&self, address: u32, bit: u32, on: bool) -> Result<()> {
        let mask = 1u32 << bit;
        let value = if on { mask } else { 0 };
        let mut data = Vec::new();
        for n in [value, value >> 16, mask, mask >> 16] {
            data.extend((n as u16).to_be_bytes());
        }
        self.control(
            0x40,
            0xbc,
            ((((address >> 16) & 15) << 8) | 1) as u16,
            address as u16,
            data,
        )?;
        Ok(())
    }
    fn i2c_read(&self, dev: u32, reg: u32) -> Result<u32> {
        self.control(
            0x40,
            0xbd,
            0,
            (dev << 7) as u16,
            (reg as u16).to_be_bytes().to_vec(),
        )?;
        let b = self.control(0xc0, 0xbd, 0, (dev << 7) as u16, vec![0; 2])?;
        let bus = self.control(0xc0, 0xbd, 15, 0, vec![0; 2])?;
        ensure!(be16(&bus) == 0x0808, "I2C read failed");
        Ok(be16(&b))
    }
    fn i2c_write(&self, dev: u32, reg: u32, value: u32) -> Result<()> {
        let mut b = (reg as u16).to_be_bytes().to_vec();
        b.extend((value as u16).to_be_bytes());
        self.control(0x40, 0xbd, 0, (dev << 7) as u16, b)?;
        let bus = self.control(0xc0, 0xbd, 15, 0, vec![0; 2])?;
        ensure!(bus[0] == 8, "I2C write failed");
        Ok(())
    }
    pub fn dr(&self, reg: u32) -> Result<u32> {
        self.i2c_read(0x90, reg)
    }
    pub fn dw(&self, reg: u32, value: u32) -> Result<()> {
        self.i2c_write(0x90, reg, value)
    }
    fn dg(&self, bit: u32, on: bool) -> Result<()> {
        self.dw(0x405, self.dr(0x405)? & !(1 << bit))?;
        self.dw(
            0x406,
            (self.dr(0x406)? & !(1 << bit)) | if on { 1 << bit } else { 0 },
        )
    }
    fn mode(&self, n: u32) -> Result<()> {
        let mut a = self.dr(0x38b)?;
        let mut b = self.dr(0x38c)?;
        match n {
            4 => a &= 0x7fff,
            2 => {
                a &= 0x0fff;
                b &= 3;
            }
            0 => {
                self.dw(0x38c, b | 3)?;
                b = (b | 3) & !2;
            }
            1 => b |= 3,
            _ => bail!("Invalid demodulator mode"),
        }
        self.dw(0x38b, a)?;
        self.dw(0x38c, b)
    }
    fn demod_output(&self, on: bool) -> Result<()> {
        let a = self.dr(0x384)?;
        let b = self.dr(0x500)?;
        for (r, v) in [
            (0x306, if on { 0x3fff } else { 0 }),
            (0x307, if on { 0xffff } else { 0 }),
            (0x308, if on { 0xffff } else { 0 }),
            (0x384, if on { a | 3 } else { a & !3 }),
            (0x500, b & 255),
        ] {
            self.dw(r, v)?;
        }
        Ok(())
    }
    fn table(&self, mut cursor: u32, tuner: bool) -> Result<()> {
        for _ in 0..100 {
            self.check_cancel()?;
            let b = self.assets.module.read(cursor, 4)?;
            let count = be16(&b);
            let reg = be16(&b[2..]);
            if count == 0 {
                return Ok(());
            }
            ensure!(count <= 256, "Invalid register table");
            cursor += 4;
            let b = self.assets.module.read(cursor, count as usize * 2)?;
            for n in 0..count {
                if tuner {
                    self.tw((reg + n) & 255, be16(&b[n as usize * 2..]))?;
                } else {
                    self.dw(reg + n, be16(&b[n as usize * 2..]))?;
                }
            }
            cursor += count * 2;
        }
        bail!("Register table has no terminator")
    }
    pub fn demod_init(&self) -> Result<()> {
        self.gpio(0x90018, 0, false)?;
        self.gpio(0x90014, 0, true)?;
        self.gpio(0x90018, 6, true)?;
        self.gpio(0x90014, 6, true)?;
        self.sleep(10)?;
        self.gpio(0x90018, 0, true)?;
        let mut found = None;
        for dev in [0x90, 0x12] {
            let probe = (|| -> Result<bool> {
                self.i2c_write(dev, 0x507, 3)?;
                self.i2c_read(dev, 0x380)?;
                let ident = self.i2c_read(dev, 0x380)?;
                let revision = self.i2c_read(dev, 0x381)?;
                Ok(ident == 0x1b3 && revision & 0xfffc == 0x8000 && revision & 3 != 3)
            })();
            if matches!(probe, Ok(true)) {
                found = Some(dev);
                break;
            }
        }
        let found = found.ok_or_else(|| anyhow::anyhow!("Demodulator not found"))?;
        self.i2c_write(found, 0x506, 0x500)?;
        self.i2c_write(found, 0x505, (0x90 << 2) | 2)?;
        self.dr(0x380)?;
        let ident = self.dr(0x380)?;
        let revision = self.dr(0x381)?;
        ensure!(
            ident == 0x1b3 && revision & 0xfffc == 0x8000 && revision & 3 != 3,
            "Unexpected demodulator identity"
        );
        self.dw(0x505, 0x90 << 2)?;
        self.dw(0x506, 0)?;
        let def = self
            .assets
            .module
            .read(self.assets.module.pointer(-0x7cf0)? + 0x84, 32)?;
        let d: Vec<u32> = def.iter().map(|&n| n as u32).collect();
        let base = self.assets.module.pointer(-0x7d04)?;
        self.dw(0x507, 3)?;
        ensure!(self.dr(0x380)? == 0x1b3, "Demodulator reset failed");
        self.dw(0x401, 0x100)?;
        self.dw(0x403, 60000 / 0x154)?;
        self.dw(0x404, 0)?;
        self.demod_output(false)?;
        self.mode(4)?;
        let rows = self.assets.module.read(base + 0x2c, 48)?;
        for row in rows.chunks_exact(4) {
            self.dw(be16(row), be16(&row[2..]))?;
        }
        self.dw(0x38a, 0x2d98)?;
        self.dw(0x385, be16(&def[8..]))?;
        let value = (d[15] << 8) | (d[14] << 5) | d[11] | (d[13] << 4) | (d[10] << 1) | 8;
        self.dw(0x386, value)?;
        self.dw(0x386, (value & !8) | (d[12] << 3))?;
        self.dw(
            0x388,
            (d[17] << 8) | if d[16] != 0 { (d[16] << 7) | 0x1c00 } else { 0 },
        )?;
        let clock = be32(&def) * 1000;
        let flags = be32(&def[20..]);
        for (r, v) in [
            (0x17, clock >> 16),
            (0x18, clock),
            (0x1b, (flags >> 16) & 0x1ff),
            (0x1c, flags),
            (0x1a, (flags >> 25) & 3),
            (0x39a, be16(&def[18..])),
            (0x405, 0xffff),
            (0x406, 0),
            (0x408, 0xffff),
            (0x40d, 0),
        ] {
            self.dw(r, v)?;
        }
        self.dw(0x12b, (self.dr(0x12b)? & 0x50) | 0x22)?;
        for (r, v) in [
            (0x12c, 0x700),
            (0x506, 0),
            (0x50b, 0x205),
            (0x28, if flags != 0 { 0x1f55 } else { 0x755 }),
        ] {
            self.dw(r, v)?;
        }
        self.table(base + 0x5c, false)?;
        self.dw(0x387, 0x31)?;
        self.dw(0x505, self.dr(0x505)? & !2)?;
        let clock = be32(&def[24..]);
        self.dw(0x1d, clock >> 16)?;
        self.dw(0x1e, clock)?;
        self.mode(0)?;
        for (r, v) in [(0x39b, 0), (0x39c, 0x308), (0x39b, 1), (0x39b, 0)] {
            self.dw(r, v)?;
        }
        self.sleep(1)?;
        self.mode(1)?;
        self.dw(0x11d, (self.dr(0x11d)? & 0xff9f) | 0x20)?;
        self.demod_output(true)?;
        self.dg(0, false)?;
        self.sleep(10)?;
        self.dg(5, false)?;
        self.sleep(10)?;
        self.dg(5, true)?;
        self.sleep(10)
    }
    fn bridge_wait(&self) -> Result<()> {
        let end = Instant::now() + Duration::from_secs(2);
        for _ in 0..100 {
            if Instant::now() >= end {
                break;
            }
            let s = self.dr(0x402)?;
            if s & 0x100 != 0 {
                ensure!(s & 0x80 != 0, "Tuner bridge I/O error");
                return Ok(());
            }
        }
        bail!("Tuner bridge timed out")
    }
    fn tr(&self, reg: u32) -> Result<u32> {
        self.dr(0x402)?;
        self.dw(0x400, reg << 8)?;
        self.dw(0x401, 0xc180 | 4 | 0x20)?;
        self.bridge_wait()?;
        self.dw(0x401, 0xc182 | 8 | 0x20 | 0x40)?;
        self.bridge_wait()?;
        self.dr(0x400)
    }
    fn tw(&self, reg: u32, value: u32) -> Result<()> {
        let value = value & 65535;
        self.dr(0x402)?;
        self.dw(0x400, (reg << 8) | (value >> 8))?;
        self.dw(0x400, (value & 255) << 8)?;
        self.dw(0x401, 0xc180 | 12 | 0x20 | 0x40)?;
        self.bridge_wait()
    }
    fn calibration_difference(&self) -> Result<i32> {
        self.tw(0x1f, 7)?;
        self.sleep(10)?;
        let first = self.tr(0x1d)? as i32;
        self.tw(0x1f, 4)?;
        self.sleep(10)?;
        Ok((first - self.tr(0x1d)? as i32) as i16 as i32)
    }
    fn calibrate(&self, reg: u32, shift: u32, invert: bool) -> Result<u32> {
        self.sleep(5)?;
        let mut difference = self.calibration_difference()?;
        let mut index = if invert {
            if difference < 0 {
                16
            } else {
                0
            }
        } else {
            (((if difference < 0 { -1 } else { 0 }) - difference) as u32 >> 27) & 16
        };
        let base = self.tr(reg)? & !(31 << shift) & 65535;
        let mut best = difference.abs();
        while best < 1023 && index != 15 && index != 31 {
            index += 1;
            self.tw(reg, base | (index << shift))?;
            difference = self.calibration_difference()?;
            let improved = difference.abs() < best;
            best = difference.abs();
            if !improved {
                break;
            }
        }
        let value = (base | ((index.wrapping_sub(1) & 255) << shift)) & 65535;
        self.tw(reg, value)?;
        Ok(value)
    }
    pub fn tuner_init(&mut self) -> Result<()> {
        let mut config = vec![0; 32];
        config[..12].copy_from_slice(
            &self
                .assets
                .module
                .read(self.assets.module.pointer(-0x7cf0)?, 12)?,
        );
        let word4 = be32(&config[4..]) | 0x80000000;
        config[4..8].copy_from_slice(&word4.to_be_bytes());
        config[16..20].copy_from_slice(&(-143i32).to_be_bytes());
        config[20] = 0x80;
        config[21] = 1;
        config[22] = 0xc0;
        config[24..26].copy_from_slice(&100u16.to_be_bytes());
        config[26..28].copy_from_slice(&500u16.to_be_bytes());
        config[28] = 1;
        config[29] = 1;
        self.tw(0x24, 0x20)?;
        self.tw(0x1b, 0xa2)?;
        self.tw(
            0x20,
            (((config[6] as u32).wrapping_sub(1) << 11) | 0x300) & 0xfb00,
        )?;
        let word14 = be32(&config[20..]);
        self.tw(
            0x23,
            (((config[29] as u32) << 5)
                | ((word14 >> 27) & 16)
                | if config[21] != 0 { 0x600 } else { 0x4600 })
                & 0xfff0,
        )?;
        self.tw(
            0x21,
            ((word4 >> 16 & 0x8000)
                | ((word4 >> 30 & 1) << 12)
                | (word4 >> 24 & 63)
                | ((word4 >> 18 & 63) << 6)
                | 0x2000)
                & 0xbfff,
        )?;
        let identity = self.tr(0x1a)?;
        let kind = (identity & 255) >> 5;
        ensure!(
            identity >> 8 & 3 != 0 && [1, 4, 6, 7].contains(&kind) && identity & 31 != 0,
            "Unexpected tuner identity"
        );
        self.table(self.assets.module.pointer(-0x7cf8)? + 0x2a0, true)?;
        self.tw(0x14, if be32(&config) < 24000 { 2 } else { 1 })?;
        for (r, v) in [(7, 0x48d), (0x24, 0x2ed), (1, 0x20d0)] {
            self.tw(r, v)?;
        }
        self.calibrate(6, 5, true)?;
        self.tw(7, 0x40d)?;
        let field7 = self.calibrate(7, 11, true)?;
        self.tw(1, 0x3dd0)?;
        self.tw(7, field7 | 0x80)?;
        self.calibrate(6, 0, false)?;
        self.tw(7, field7 & 0xff7f)?;
        self.calibrate(6, 10, false)?;
        self.tw(7, field7 & 0xff77)?;
        self.tw(0x1f, 7)?;
        let saved = self.tr(0x24)?;
        let saved10 = self.tr(0x10)?;
        self.tw(0x10, (saved10 & 0xfb0f) | 0x400)?;
        self.tw(0x24, 0x2e9)?;
        self.sleep(9)?;
        self.rf_baseline = self.tr(0x1d)?;
        self.tw(0x24, saved)?;
        self.rf_config = config;
        Ok(())
    }
    fn rf_tune(&self, frequency: u32) -> Result<()> {
        let base = self.assets.module.pointer(-0x7cf8)?;
        let select = |off: u32, stride: u32, count: u32| -> Result<Vec<u8>> {
            for n in 0..count {
                let b = self
                    .assets
                    .module
                    .read(base + off + n * stride, stride as usize)?;
                if be32(&b) >= frequency {
                    return Ok(b);
                }
            }
            bail!("Frequency exceeds native RF table")
        };
        let analog = select(0x0c, 16, 15)?;
        let divider = select(0xfc, 8, 18)?;
        let a: Vec<u32> = analog.iter().map(|&n| n as u32).collect();
        let d: Vec<u32> = divider.iter().map(|&n| n as u32).collect();
        ensure!(d[7] != 0, "Invalid RF divider");
        let reference = be32(&self.rf_config);
        ensure!(reference >= 10, "Invalid RF clock");
        let vco = frequency * (d[6] << 1) / d[7];
        let mut integer = vco / reference;
        let remainder = vco % reference;
        let mut numerator = 0;
        if remainder > 99 {
            numerator = 0x13ec00;
            if remainder > 199 {
                if remainder > reference - 100 {
                    integer += 1;
                    numerator = 0;
                } else {
                    numerator = remainder.min(reference - 200) * 0x1980;
                }
            }
        }
        let fraction = numerator / (reference / 10);
        let reg19 = if fraction != 0 {
            if d[4] != 0 {
                0x49e
            } else {
                0x41d
            }
        } else if d[4] != 0 {
            0x49f
        } else {
            0x41f
        };
        for (r, v) in [
            (0x0b, a[4] | 0xb800),
            (0x15, integer),
            (0x16, if fraction != 0 { 0xff01 } else { 0x101 }),
            (0x17, fraction),
            (0x19, (d[5] << 11) | (d[4] << 7) | reg19),
            (0x1c, if fraction != 0 { 0xff2e } else { 0xfff8 }),
            (0x24, (be16(&analog[14..]) & 0xff9f) | 6),
        ] {
            self.tw(r, v)?;
        }
        self.sleep(10)?;
        self.tw(0x10, 0x2b1)?;
        self.tw(0x1e, 0x32)?;
        self.sleep(20)?;
        let mut step = 64;
        let mut current = 64;
        let mut best_value = 64;
        let mut best_error = 3000;
        let target = if fraction != 0 { 400 } else { 200 };
        for _ in 0..7 {
            step /= 2;
            self.tw(0x18, 0xe900 | (current as u32 & 65535))?;
            self.sleep(10)?;
            let sample = self.tr(0x1d)? as i32;
            let error = (sample - target).abs();
            if error < best_error {
                best_error = error;
                best_value = current;
            }
            current = (current + if sample < target { step } else { -step }) as i8 as i32;
            self.sleep(15)?;
        }
        self.tw(0x18, 0xe900 | (best_value as u32 & 65535))?;
        self.sleep(10)?;
        self.tw(0x1e, 0x7ff)?;
        self.tw(0x10, 0x9f83)?;
        self.tw(9, ((a[5] << 5) | a[6]) & 0x1fff)?;
        for n in 0..3 {
            self.tw(0x0c + n, be16(&analog[8 + n as usize * 2..]))?;
        }
        self.sleep(10)?;
        self.tw(0x0f, 0x7c00)?;
        self.tw(1, 0x8000)?;
        self.sleep(10)?;
        let gain = self.assets.module.read(base + 0x28a, 22)?;
        let limiter = self.assets.module.read(base + 0x266, 14)?;
        self.tw(0x2a, 0xffff)?;
        for n in 0..6 {
            self.tw(0x2c + n, be16(&gain[6 + n as usize * 2..]))?;
        }
        for n in 0..2 {
            self.tw(0x3e + n, be16(&gain[18 + n as usize * 2..]))?;
        }
        self.tw(0x33, 0xffff)?;
        for n in 0..4 {
            self.tw(0x35 + n, be16(&limiter[6 + n as usize * 2..]))?;
        }
        self.tw(0x32, if be16(&gain) != 0 { 0x1800 } else { 0 })?;
        self.tw(0x39, 0x400)
    }
    fn agc(&self, frequency: u32) -> Result<()> {
        self.mode(2)?;
        let base = self.assets.module.pointer(-0x7cf0)?;
        let thresholds = self.assets.module.read(base + 0x0c, 24)?;
        let mut offset = None;
        for row in thresholds.chunks_exact(4) {
            if be16(row) * 1000 >= frequency {
                offset = Some(be16(&row[2..]));
                break;
            }
        }
        let offset = offset.ok_or_else(|| anyhow::anyhow!("No AGC threshold"))?;
        let override_value = 4095.min(((self.rf_baseline * 144 / 33 + 1) / 2) + offset);
        self.dw(0x6a, override_value)?;
        let data = self.assets.module.read(base + 0x24, 48)?;
        let g: Vec<u32> = data.iter().map(|&n| n as u32).collect();
        ensure!(g[0] & 2 != 0, "Unsupported AGC configuration");
        let half = |n| be16(&data[n..]);
        for (r, v) in [
            (0x4c, half(2)),
            (0x4d, half(4)),
            (0x4e, half(6)),
            (0x65, (g[8] << 12) | half(10)),
            (0x66, (g[35] << 5) | g[36]),
            (0x67, (g[37] << 6) | g[38]),
            (
                0x6a,
                if override_value != 0 {
                    override_value
                } else {
                    half(14)
                },
            ),
            (0x6b, (((g[17] & 127) << 9) | (g[39] << 8)) & 0xff00),
        ] {
            self.dw(r, v)?;
        }
        for (n, off) in [18, 20, 22, 24, 26, 29, 31, 33].iter().enumerate() {
            self.dw(0x6c + n as u32, half(*off))?;
        }
        self.dw(0x4b, g[28])?;
        self.dw(
            0x39b,
            (self.dr(0x39b)? & 0xffe3) | (g[12] << 4) | (g[16] << 2),
        )?;
        self.sleep(70)?;
        self.dw(0x302, 0xa00)?;
        self.dw(0x302, 0)?;
        self.sleep(50)?;
        self.dg(6, true)?;
        self.sleep(70)?;
        ensure!(
            g[39] == 0 || half(42) == 0,
            "Unsupported AGC split configuration"
        );
        Ok(())
    }
    fn tmcc(&self) -> Result<Tmcc> {
        let raw = self.dr(0x23a)?;
        let code = raw >> 4 & 3;
        let mut t = Tmcc {
            mode: if code == 1 {
                0
            } else if code == 2 {
                2
            } else {
                1
            },
            guard: raw & 3,
            inversion: raw >> 6 & 1,
            partial: self.dr(0x1f9)? & 1,
            parameter: self.dr(0x1fc)? & 1,
            layers: [Layer {
                segments: 0,
                modulation: 0,
                rate: 0,
                interleave: 0,
            }; 3],
        };
        for n in 0..3 {
            let rate = self.dr(0x1e1 + n)?;
            let modulation = self.dr(0x1e7 + n)?;
            let segments = self.dr(0x1ed + n)?;
            let interleave = self.dr(0x1f3 + n)?;
            t.layers[n as usize] = Layer {
                segments: segments & 15,
                modulation: match modulation {
                    0 => 3,
                    1 => 0,
                    2 => 1,
                    _ => 2,
                },
                rate: rate & 7,
                interleave: interleave & 3,
            };
        }
        Ok(t)
    }
    pub fn locked(&self) -> Result<bool> {
        let s = self.dr(0x238)?;
        let mut viterbi = false;
        for n in 0..3 {
            viterbi |= self.dr(0x22a + n)? & 1 != 0;
        }
        Ok(s & 0x2000 != 0
            && s & 0x100 != 0
            && s >> 1 & 15 == 15
            && s & 0x1000 != 0
            && s >> 5 & 7 != 0
            && viterbi)
    }
    fn acquire(&self, captured: Option<Tmcc>) -> Result<()> {
        let def = self
            .assets
            .module
            .read(self.assets.module.pointer(-0x7cf0)? + 0x84, 32)?;
        ensure!(be32(&def[20..]) == 0, "Unsupported demodulator clock flags");
        let p = captured.unwrap_or(Tmcc {
            mode: 1,
            guard: 2,
            inversion: 0,
            partial: 1,
            parameter: 0,
            layers: [
                Layer {
                    segments: 13,
                    modulation: 2,
                    rate: 2,
                    interleave: 0,
                },
                Layer {
                    segments: 0,
                    modulation: 2,
                    rate: 255,
                    interleave: 255,
                },
                Layer {
                    segments: 0,
                    modulation: 2,
                    rate: 255,
                    interleave: 255,
                },
            ],
        });
        ensure!(
            p.parameter == 0 && p.guard < 4,
            "Unsupported TMCC parameters"
        );
        ensure!(
            p.layers
                .iter()
                .all(|l| l.segments == 0 || l.modulation != 3),
            "Unsupported DQPSK layer"
        );
        let fft = if p.mode == 0 {
            1
        } else if p.mode == 2 {
            2
        } else {
            3
        };
        let clock = be32(&def[24..]);
        self.dw(0x1d, clock >> 16)?;
        self.dw(0x1e, clock)?;
        self.dw(0x11d, self.dr(0x11d)? & 0x60)?;
        self.dw(0x1a, (self.dr(0x1a)? & 1) ^ p.inversion)?;
        for (r, v) in [
            (0x1b, 0),
            (0x1c, 0),
            (0x1a, 0),
            (10, if captured.is_some() { 0 } else { 0x70 }),
            (1, 0x3ffc | p.guard),
        ] {
            self.dw(r, v)?;
        }
        let mut active = 3;
        for (n, l) in p.layers.iter().enumerate() {
            let coding = match l.rate {
                1 => 8,
                2 => 16,
                3 => 24,
                5 => 40,
                _ => 56,
            };
            let interleave = if l.interleave < 4 { l.interleave } else { 0 };
            ensure!(l.modulation < 4, "Invalid modulation");
            self.dw(
                2 + n as u32,
                (l.segments << 6)
                    | [0x400, 0x800, 0xc00, 0][l.modulation as usize]
                    | coding
                    | interleave,
            )?;
            if l.segments != 0 {
                if active == 1 && l.modulation == 2 {
                    active = 2;
                } else if (active == 0 || active == 3) && (l.modulation == 1 || l.modulation == 2) {
                    active = l.modulation;
                }
            }
        }
        let base = if p.mode == 0 {
            64
        } else if p.mode == 2 {
            128
        } else {
            256
        };
        self.dw(0x112, (self.dr(0x112)? & 0xffcf) | (p.partial << 5))?;
        self.dw(0x110, 0x200)?;
        self.dw(0x111, (self.dr(0x111)? & 0xfff0) | 6)?;
        for (r, v) in [
            (0x10e, 1),
            (0x10f, 0),
            (0, fft << 13),
            (0x10c, 0x427),
            (0x160, 0),
            (0x161, 0x1fff),
            (0x15f, 0xd5),
            (0xb4, 0x409),
            (0xbb, 0x4102),
        ] {
            self.dw(r, v)?;
        }
        for reg in 0xb5..0xbb {
            self.dw(reg, 0x2800)?;
        }
        for (r, v) in [
            (0x152, 0x1461),
            (0x154, 0x206),
            (0x155, 0x27),
            (0xb2, 0x20),
            (0xbd, 0xe000),
            (0xc0, 0xe000),
            (0xe1, 0xe000),
            (0x10a, 0xe040),
            (0x11f, 0xf000),
            (0x120, if captured.is_some() { 0 } else { 0x1fff }),
            (0xd3, 0x1fff),
            (0x20, ((9 - fft) << 12) | 0x680),
            (0x25, (8 - fft) | 0x60),
        ] {
            self.dw(r, v)?;
        }
        self.dw(
            0x111,
            (self.dr(0x111)? & 15) | (((base << p.guard) * 3 / 2 + 0x90) << 4),
        )?;
        let tab = be32(&self.assets.module.read(0x3db34, 4)?);
        self.table(
            tab + if active == 2 {
                0x1be
            } else if active == 1 {
                0x1cc
            } else {
                0x290
            },
            false,
        )?;
        self.dw(0x74, if active == 1 || active == 2 { 7 } else { 0 })?;
        let b = self.assets.module.read(tab + 0x1da, 20)?;
        for n in 0..10 {
            self.dw(
                0x50 + n,
                be16(&b[n as usize * 2..]).wrapping_sub(if active == 1 || active == 2 {
                    0
                } else {
                    0x163
                }),
            )?;
        }
        let b = self.assets.module.read(tab + 0x1f0, 48)?;
        for n in 0..24 {
            self.dw(0x75 + n, be16(&b[n as usize * 2..]))?;
        }
        let segments: u32 = p.layers.iter().map(|l| l.segments).sum();
        for (r, v) in [
            (0xde, 1),
            (0xdf, 0x1000),
            (0xe4, 0),
            (0x109, 0x1f),
            (0xcd, 0x200f),
            (0x122, segments << 7),
            (0x123, segments << 7),
            (0x124, segments << 7),
            (0xfa, 0xcd5),
        ] {
            self.dw(r, v)?;
        }
        if captured.is_some() {
            self.dw(0x302, 0x4000)?;
            self.dw(0x302, 0)?;
            self.sleep(245)?;
            self.dw(0x20, (((11 - fft) << 12) | 0x680) & 0xf6e0)?;
            self.dw(0x25, (10 - fft) | ((fft + 5) << 5))?;
            if self.dr(0x238)? & 0x800 != 0 {
                self.dw(0x1d, self.dr(0x1b3)?)?;
                self.dw(0x1e, self.dr(0x1b4)?)?;
            }
            self.dw(6, 0x200)?;
            let v = self.dr(0x387)?;
            self.dw(0x387, v & !8)?;
            self.sleep(1)?;
            self.dw(0x387, v | 8)?;
        } else {
            let clock = be32(&def);
            for (r, v) in [
                (6, 4),
                (7, 8),
                (8, 0x1000),
                (0x0b, clock * 50 >> 16),
                (0x0c, clock * 50),
                (0x0d, clock * 100 >> 16),
                (0x0e, clock * 100),
                (0x0f, clock * 1000 >> 16),
                (0x10, clock * 1000),
            ] {
                self.dw(r, v)?;
            }
            let original = self.dr(0)?;
            self.dw(0, original | 0x8000)?;
            self.dr(0x504)?;
            self.dw(0, original)?;
        }
        Ok(())
    }
    pub fn tune_rf(&self, physical: u32) -> Result<()> {
        ensure!((13..=52).contains(&physical), "Invalid physical channel");
        let frequency = 473143 + (physical - 13) * 6000;
        self.demod_output(false)?;
        self.mode(2)?;
        self.mode(0)?;
        self.dg(0, false)?;
        self.dg(9, true)?;
        self.dg(8, false)?;
        self.dg(7, true)?;
        self.rf_tune(frequency)?;
        self.agc(frequency)?;
        self.acquire(None)?;
        let deadline = Instant::now() + Duration::from_secs(5);
        let mut search = 0;
        while Instant::now() < deadline {
            search = self.dr(0x504)?;
            if search & 3 != 0 {
                break;
            }
            self.sleep(50)?;
        }
        let t = self.tmcc()?;
        if search & 1 == 0 && search & 2 != 0 {
            self.acquire(Some(t))?;
        }
        let deadline = Instant::now() + Duration::from_secs(5);
        let mut consecutive = 0;
        while Instant::now() < deadline {
            consecutive = if self.locked()? { consecutive + 1 } else { 0 };
            if consecutive >= 3 {
                break;
            }
            self.sleep(100)?;
        }
        Ok(())
    }
}
