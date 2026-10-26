// Native port of scripts/torne-protocol.py and the verified BonDriver protocol.
// Firmware, keys, tables and the media IV come only from the user's local files.
use crate::{
    assets::{be16, be32, Assets},
    crypto::{cbc, sha1, sha256},
};
use anyhow::{bail, ensure, Context, Result};
use rand::{rngs::OsRng, RngCore};
use serde_json::json;
use std::{
    fs::File,
    io::Write,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::{Duration, Instant, SystemTime},
};

pub trait Transport {
    fn control(&self, kind: u8, request: u8, value: u16, index: u16, data: &mut [u8])
        -> Result<()>;
    fn pipe(&self, id: u8, length: usize) -> Result<Vec<u8>>;
    fn write_pipe(&self, data: &[u8]) -> Result<()>;
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{cell::RefCell, collections::BTreeMap, fs::OpenOptions};

    #[derive(Default)]
    struct FakeState {
        registers: BTreeMap<u32, u8>,
        completion: Vec<u8>,
        commands: Vec<Vec<u8>>,
        state: u32,
        bad_sequence: bool,
    }
    #[derive(Default)]
    struct FakeUsb(RefCell<FakeState>);
    impl Transport for FakeUsb {
        fn control(
            &self,
            kind: u8,
            request: u8,
            value: u16,
            index: u16,
            data: &mut [u8],
        ) -> Result<()> {
            let mut s = self.0.borrow_mut();
            if request == 0xb8 {
                s.commands.push(data.to_vec());
                s.completion = vec![
                    0x10,
                    0,
                    0,
                    if s.bad_sequence { data[1] ^ 1 } else { data[1] },
                    data[2],
                    0,
                ];
                if data[2] == 5 {
                    s.state = data[5] as u32;
                }
                if data[2] == 8 {
                    let mut reply: Vec<u8> = (0..64)
                        .map(|n| *s.registers.get(&(0x83000 + n)).unwrap_or(&0))
                        .collect();
                    // Reply data is independent of the request. Secure replies
                    // encrypt nonce + payload and advertise reply flag 2.
                    reply[4..8].fill(0);
                    reply[8..12].copy_from_slice(&4u32.to_be_bytes());
                    reply[12..32].copy_from_slice(&[0; 20]);
                    reply[16..32].fill(0xa5);
                    if reply[2] != 0 {
                        reply[2] = 2;
                        cbc(&mut reply[12..44], &[7; 16], &[0; 16], true)?;
                    }
                    for (n, &b) in reply.iter().enumerate() {
                        s.registers.insert(0x83000 + n as u32, b);
                    }
                }
                return Ok(());
            }
            if request == 0xbd {
                if kind == 0xc0 {
                    data.copy_from_slice(&[8, 8]);
                }
                return Ok(());
            }
            ensure!(request == 0xbc, "Unexpected test request");
            let address = (((value as u32 >> 8) & 15) << 16) | index as u32;
            if kind == 0xc0 {
                if address == 0x82008 {
                    data.copy_from_slice(&(s.state as u16).to_be_bytes());
                } else {
                    for (n, b) in data.iter_mut().enumerate() {
                        *b = *s.registers.get(&(address + n as u32)).unwrap_or(&0);
                    }
                }
            } else {
                for (n, &b) in data.iter().enumerate() {
                    s.registers.insert(address + n as u32, b);
                }
            }
            Ok(())
        }
        fn pipe(&self, id: u8, _length: usize) -> Result<Vec<u8>> {
            if id == 0x83 {
                Ok(std::mem::take(&mut self.0.borrow_mut().completion))
            } else {
                Ok(Vec::new())
            }
        }
        fn write_pipe(&self, _data: &[u8]) -> Result<()> {
            Ok(())
        }
    }
    struct Fixture {
        session: Session<FakeUsb>,
        path: std::path::PathBuf,
    }
    impl Fixture {
        fn new(name: &str) -> Self {
            let path =
                std::env::temp_dir().join(format!("torne-{}-{name}.jsonl", std::process::id()));
            let log = OpenOptions::new()
                .create(true)
                .truncate(true)
                .write(true)
                .open(&path)
                .unwrap();
            Self {
                session: Session::new(
                    FakeUsb::default(),
                    Assets::fixture(),
                    63,
                    Arc::new(AtomicBool::new(false)),
                    log,
                ),
                path,
            }
        }
    }
    impl Drop for Fixture {
        fn drop(&mut self) {
            let _ = std::fs::remove_file(&self.path);
        }
    }
    #[test]
    fn api_sequence_wrap_and_reject_mismatched_completion() {
        let mut f = Fixture::new("sequence");
        let s = &mut f.session;
        s.api(4, 0x20, 0x1234).unwrap();
        assert_eq!(s.sequence, 0);
        assert_eq!(s.usb.0.borrow().commands[0], [0, 63, 4, 0x20, 0x12, 0x34]);
        s.usb.0.borrow_mut().bad_sequence = true;
        assert!(s.api(5, 0, 2).is_err());
        assert_eq!(s.sequence, 0);
    }
    #[test]
    fn encrypted_reply_and_cleanup_after_cancellation() {
        let mut f = Fixture::new("cleanup");
        let s = &mut f.session;
        s.streamkey = vec![7; 16];
        s.media_open = true;
        let reply = s.seqcom(0x701, &[0; 12], 6).unwrap();
        assert_eq!(&reply[16..32], &[0xa5; 16]);
        s.usb.0.borrow_mut().state = 2;
        s.stopped.store(true, Ordering::Relaxed);
        assert!(s.api(5, 0, 4).unwrap_err().is::<Cancelled>());
        assert_eq!(s.usb.0.borrow().commands.len(), 1);
        s.cleanup().unwrap();
        assert!(!s.media_open);
        assert_eq!(s.word(0x82008).unwrap(), 1);
        let commands = &s.usb.0.borrow().commands;
        assert_eq!(commands[1], [0, 0, 5, 0, 0, 4]);
        assert_eq!(commands[2], [0, 1, 5, 0, 0, 1]);
        assert_eq!(commands[3][2], 8);
        let log = std::fs::read_to_string(&f.path).unwrap();
        assert!(!log.contains("dataHex"));
    }

    #[test]
    fn cooperative_stop_ignores_stale_file_and_allows_cleanup() {
        let mut f = Fixture::new("stop-file");
        let path = f.path.with_extension("stop");
        std::fs::write(&path, b"stop").unwrap();
        f.session.stop_file = Some((path.clone(), SystemTime::now() + Duration::from_secs(60)));
        f.session.check_cancel().unwrap();
        f.session.stop_file = Some((path.clone(), SystemTime::now() - Duration::from_secs(60)));
        assert!(f.session.check_cancel().unwrap_err().is::<Cancelled>());
        f.session.cleaning = true;
        f.session.check_cancel().unwrap();
        std::fs::remove_file(path).unwrap();
    }
}
#[derive(Debug)]
pub struct Cancelled;
impl std::fmt::Display for Cancelled {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Cancelled")
    }
}
impl std::error::Error for Cancelled {}

pub struct Session<T> {
    pub usb: T,
    pub assets: Assets,
    pub sequence: u8,
    pub media_key: Vec<u8>,
    pub media_open: bool,
    pub cleaning: bool,
    pub authentications: u64,
    streamkey: Vec<u8>,
    auth_counter: u32,
    api_pending: u32,
    pub stopped: Arc<AtomicBool>,
    pub stop_file: Option<(std::path::PathBuf, SystemTime)>,
    log: File,
    pub rf_config: Vec<u8>,
    pub rf_baseline: u32,
}
impl<T: Transport> Session<T> {
    pub fn new(usb: T, assets: Assets, sequence: u8, stopped: Arc<AtomicBool>, log: File) -> Self {
        Self {
            usb,
            assets,
            sequence: sequence & 63,
            media_key: Vec::new(),
            media_open: false,
            cleaning: false,
            authentications: 0,
            streamkey: Vec::new(),
            auth_counter: 1,
            api_pending: 0,
            stopped,
            stop_file: None,
            log,
            rf_config: Vec::new(),
            rf_baseline: 0,
        }
    }
    pub fn check_cancel(&self) -> Result<()> {
        if !self.cleaning
            && self.stop_file.as_ref().is_some_and(|(path, launched)| {
                std::fs::metadata(path)
                    .ok()
                    .and_then(|m| m.modified().ok())
                    .is_some_and(|t| t >= *launched)
            })
        {
            self.stopped.store(true, Ordering::Relaxed);
        }
        if !self.cleaning && self.stopped.load(Ordering::Relaxed) {
            return Err(Cancelled.into());
        }
        Ok(())
    }
    pub fn sleep(&self, ms: u64) -> Result<()> {
        self.check_cancel()?;
        std::thread::sleep(Duration::from_millis(ms));
        self.check_cancel()
    }
    pub fn control(
        &self,
        kind: u8,
        request: u8,
        value: u16,
        index: u16,
        mut data: Vec<u8>,
    ) -> Result<Vec<u8>> {
        ensure!(
            data.len() <= 64 && (kind == 0x40 || kind == 0xc0),
            "Invalid vendor control request"
        );
        self.usb.control(kind, request, value, index, &mut data)?;
        // Hash all register data; never persist authentication or media keys.
        writeln!(
            &self.log,
            "{}",
            json!({"operation":"control", "kind":kind, "request":request,"value":value,"index":index,"bytesTransferred":data.len(),"dataSha256":sha256(&data)})
        )?;
        Ok(data)
    }
    pub fn read(&self, address: u32, length: usize) -> Result<Vec<u8>> {
        self.control(
            0xc0,
            0xbc,
            (((address >> 16) & 15) << 8) as u16,
            address as u16,
            vec![0; length],
        )
    }
    pub fn word(&self, address: u32) -> Result<u32> {
        Ok(be16(&self.read(address, 2)?))
    }
    pub fn write(&self, address: u32, data: &[u8]) -> Result<()> {
        self.control(
            0x40,
            0xbc,
            (((address >> 16) & 15) << 8) as u16,
            address as u16,
            data.to_vec(),
        )?;
        Ok(())
    }
    pub fn write_word(&self, address: u32, word: u32) -> Result<()> {
        self.write(address, &(word as u16).to_be_bytes())
    }
    pub fn pending(&self) -> Result<u32> {
        let bits = self.word(0x90074)? & 0x0ee4;
        self.read(0x901b0, 2)?;
        for bit in 0..16 {
            if bits & (1 << bit) != 0 {
                self.write_word(0x90074, 1 << bit)?;
            }
        }
        Ok(bits)
    }
    pub fn wait_state(&self, expected: u32) -> Result<()> {
        let deadline = Instant::now() + Duration::from_secs(3);
        loop {
            self.check_cancel()?;
            let state = self.word(0x82008)?;
            self.pending()?;
            if state == expected {
                return Ok(());
            }
            ensure!(
                Instant::now() < deadline,
                "Expected state {expected:04x}, last state {state:04x}"
            );
            self.usb.pipe(0x83, 16)?;
        }
    }
    pub fn api(&mut self, command: u8, arg: u8, word: u16) -> Result<Vec<u8>> {
        // Cancellation happens before complete protocol transactions, never
        // between submitting an API request and consuming its completion.
        self.check_cancel()?;
        self.api_pending = 0;
        self.control(
            0x40,
            0xb8,
            0,
            0,
            vec![
                0,
                self.sequence,
                command,
                arg,
                (word >> 8) as u8,
                word as u8,
            ],
        )?;
        let deadline = Instant::now() + Duration::from_secs(3);
        loop {
            let b = self.usb.pipe(0x83, 16)?;
            if b.first() == Some(&0x10) {
                ensure!(
                    b.len() >= 6 && b[3] == self.sequence && b[4] == command,
                    "Unexpected API completion"
                );
                self.sequence = (self.sequence + 1) & 63;
                ensure!(b[5] == 0, "API {command:x} completion status {}", b[5]);
                return Ok(b);
            }
            if b.first() == Some(&0x20) {
                self.api_pending |= self.pending()?;
            }
            ensure!(
                Instant::now() < deadline,
                "No completion for API {command:x}"
            );
        }
    }
    pub fn seqcom(&mut self, command: u16, payload: &[u8], flags: u8) -> Result<Vec<u8>> {
        ensure!(
            flags & !6 == 0 && payload.len() % 4 == 0 && payload.len() <= 480,
            "Invalid SeqCom request"
        );
        ensure!(
            flags == 0 || self.streamkey.len() == 16,
            "Secure SeqCom requires authentication"
        );
        let wire = if flags & 4 != 0 {
            12 + ((payload.len() + 19) & !15)
        } else {
            16 + ((payload.len() + 15) & !15)
        };
        let mut packet = vec![0; (wire + 63) & !63];
        packet[..2].copy_from_slice(&command.to_be_bytes());
        packet[2] = flags;
        packet[8..12].copy_from_slice(&((payload.len() / 4) as u32).to_be_bytes());
        packet[16..16 + payload.len()].copy_from_slice(payload);
        if flags & 4 != 0 {
            let length = (payload.len() + 19) & !15;
            OsRng.try_fill_bytes(&mut packet[12..16])?;
            OsRng.try_fill_bytes(&mut packet[16 + payload.len()..12 + length])?;
            cbc(
                &mut packet[12..12 + length],
                &self.streamkey,
                &[0; 16],
                true,
            )?;
        }
        for (n, chunk) in packet.chunks(64).enumerate() {
            self.write(0x83000 + n as u32 * 64, chunk)?;
        }
        self.api(8, 0, 0)?;
        let mut response = self.read(0x83000, 64)?;
        ensure!(
            be16(&response) == command as u32 && be32(&response[4..]) == 0,
            "SeqCom {command:x} failed"
        );
        let words = be32(&response[8..]) as usize;
        ensure!(
            words <= 120 && response[2] & !6 == 0,
            "Invalid SeqCom response"
        );
        let size = if response[2] & 2 != 0 {
            12 + ((words * 4 + 19) & !15)
        } else {
            16 + ((words * 4 + 15) & !15)
        };
        ensure!(size <= 512, "SeqCom response exceeds buffer");
        for off in (64..size).step_by(64) {
            response.extend(self.read(0x83000 + off as u32, 64)?);
        }
        if response[2] & 2 != 0 {
            let length = (words * 4 + 19) & !15;
            cbc(
                &mut response[12..12 + length],
                &self.streamkey,
                &[0; 16],
                false,
            )?;
        }
        response.truncate(16 + words * 4);
        Ok(response)
    }
    fn transfer(&self, data: &[u8]) -> Result<()> {
        for chunk in data.chunks(65536) {
            self.check_cancel()?;
            self.usb.write_pipe(chunk)?;
        }
        Ok(())
    }
    fn authenticate(&mut self) -> Result<()> {
        let base = self.assets.module.pointer(-0x7dcc)?;
        let response = self.seqcom(0x101, &self.assets.module.read(base + 0x20, 32)?, 0)?;
        ensure!(response.len() >= 48, "Short authentication response");
        let mut data = self.assets.module.read(base + 0x40, 28)?;
        data.push(response[0]);
        data.extend(self.assets.module.read(base + 0x25, 1)?);
        let index = (sha1(&data)[19] & 15) as usize;
        let mut table = self.assets.module.read(base + 0x5c, 512)?;
        cbc(
            &mut table,
            &self
                .assets
                .module
                .read(self.assets.module.pointer(-0x7db0)?, 16)?,
            &[0; 16],
            false,
        )?;
        let key = &table[index * 32..(index + 1) * 32];
        data = response[16..48].to_vec();
        data.extend(key);
        let digest = sha1(&data);
        self.seqcom(0x102, &digest, 0)?;
        data = digest;
        data.extend(key);
        let hash = sha1(&data);
        self.streamkey = [16, 6, 11, 0]
            .iter()
            .flat_map(|&off| hash[off..off + 4].iter().copied())
            .collect();
        self.auth_counter = 1;
        self.api(1, 0, 0)?;
        self.wait_state(0x11)
    }
    pub fn runtime_auth(&mut self) -> Result<()> {
        let response = self.seqcom(0x201, &[], 2)?;
        ensure!(
            response.len() >= 48,
            "Short runtime authentication challenge"
        );
        let mut data = response[16..48].to_vec();
        data.extend(
            self.assets
                .module
                .read(self.assets.module.pointer(-0x7dcc)?, 28)?,
        );
        data.extend(self.auth_counter.to_be_bytes());
        self.seqcom(0x202, &sha1(&data), 4)?;
        self.auth_counter = self.auth_counter.wrapping_add(1).max(1);
        self.authentications += 1;
        Ok(())
    }
    fn card_startup(&mut self) -> Result<()> {
        self.pending()?;
        self.api(10, 0, 0)?;
        let mut last = None;
        let mut retries = 0;
        for _ in 0..16 {
            self.check_cancel()?;
            self.usb.pipe(0x83, 16)?;
            let bits = self.pending()?;
            let header = self.word(0x822ac)?;
            if bits & 0x200 == 0 && last == Some(header) {
                continue;
            }
            last = Some(header);
            match header >> 12 {
                1 => {
                    self.api(10, 1, 1)?;
                }
                3 => return Ok(()),
                4 => {
                    ensure!(retries < 3, "B-CAS startup retries exhausted");
                    retries += 1;
                    let b = self.api(10, 1, 0)?;
                    ensure!(b.len() == 6, "Unexpected card startup response");
                    self.api(10, 1, 1)?;
                }
                0 | 5 => bail!("B-CAS card is not ready"),
                _ => (),
            }
        }
        bail!("B-CAS startup timed out")
    }
    fn card_initial_setting(&mut self) -> Result<()> {
        let mut data = vec![0; 16];
        OsRng.try_fill_bytes(&mut data[..4])?;
        data[4..9].copy_from_slice(&[0x90, 0x30, 0, 0, 0]);
        cbc(&mut data, &self.streamkey, &[0; 16], true)?;
        self.pending()?;
        self.write(0x8219c, &data)?;
        self.api(10, 0x10, 9)?;
        let mut notified = self.api_pending & 0x400 != 0;
        let deadline = Instant::now() + Duration::from_secs(3);
        loop {
            notified |= self.pending()? & 0x400 != 0;
            if notified {
                let mut reply = self.read(0x822ac, 64)?;
                if reply[0] == 0x30 && reply[1] >= 4 {
                    let length = reply[1] as usize;
                    let total = 2 + ((length + 15) & !15);
                    for off in (64..total).step_by(64) {
                        reply.extend(self.read(0x822ac + off as u32, 64.min(total - off))?);
                    }
                    cbc(&mut reply[2..total], &self.streamkey, &[0; 16], false)?;
                    self.pending()?;
                    ensure!(
                        length >= 61
                            && reply[10..12] == [0x21, 0]
                            && reply[length..length + 2] == [0x90, 0],
                        "B-CAS initial setting failed"
                    );
                    return Ok(());
                }
            }
            self.check_cancel()?;
            ensure!(Instant::now() < deadline, "B-CAS initial setting timed out");
            self.usb.pipe(0x83, 16)?;
        }
    }
    pub fn prepare(&mut self, channel: u32) -> Result<()> {
        let mut state = self.word(0x82008)?;
        if (2..=4).contains(&state) {
            self.stop_stream()?;
            state = self.word(0x82008)?;
        }
        match state {
            0 => {
                self.sequence = 0;
                self.usb.pipe(0x83, 16)?;
                self.write_word(0x90078, 0x0ee4)?;
                self.read(0x90078, 2)?;
                self.write_word(0x901b4, 0)?;
                let deadline = Instant::now() + Duration::from_secs(3);
                let mut irq = false;
                while Instant::now() < deadline {
                    self.check_cancel()?;
                    if self.usb.pipe(0x83, 16)?.first() == Some(&0x20) {
                        irq = true;
                        break;
                    }
                }
                if !irq {
                    ensure!(self.pending()? & 4 != 0, "No boot/state interrupt");
                }
                self.transfer(&self.assets.idle)?;
                self.write_word(0x90070, 4)?;
                self.wait_state(0x10)?;
            }
            0xffff => {
                self.pending()?;
                self.api(3, 0, 0)?;
                self.sequence = 0;
                self.wait_state(0x10)?;
            }
            1 => {
                self.api(1, 0, 0)?;
                self.wait_state(0x11)?;
                self.api(2, 1, 0)?;
                self.wait_state(0x12)?;
                self.api(2, 0, 0)?;
                self.wait_state(0x10)?;
            }
            0x10 => (),
            _ => bail!("Unsupported boot state {state:x}"),
        }
        self.authenticate()?;
        self.write_word(0x90000, 0)?;
        for (ptr, size, address) in [(-0x7d5c, 0x500, 0x1000), (-0x7d58, 0x210, 0x1500)] {
            let data = self
                .assets
                .module
                .read(self.assets.module.pointer(ptr)?, size)?;
            for (n, chunk) in data.chunks(64).enumerate() {
                self.check_cancel()?;
                self.write(address + n as u32 * 64, chunk)?;
            }
        }
        self.api(4, 0, 0)?;
        self.transfer(&self.assets.operational)?;
        self.api(4, 0x20, 0)?;
        self.wait_state(1)?;
        self.runtime_auth()?;
        self.card_startup()?;
        self.card_initial_setting()?;
        self.runtime_auth()?;
        let reply = self.seqcom(0x701, &[0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0], 6)?;
        self.media_open = true;
        ensure!(reply.len() >= 32, "Short media key response");
        self.media_key = reply[16..32].to_vec();
        self.demod_init().context("Initialize demodulator")?;
        self.tuner_init().context("Initialize tuner")?;
        self.runtime_auth()?;
        self.tune_rf(channel).context("Tune UHF")?;
        self.runtime_auth()?;
        ensure!(self.locked()?, "No broadcast lock on channel {channel}");
        self.start_stream()
    }
    pub fn set_output(&self, on: bool) -> Result<()> {
        self.dw(0x12b, (self.dr(0x12b)? & 0x50) | 0x22)?;
        self.dw(0x12c, 0x700)?;
        self.dw(0x506, if on { 0x400 } else { 0 })?;
        self.dw(0x50b, 0x205)
    }
    pub fn start_stream(&mut self) -> Result<()> {
        ensure!(
            self.word(0x82008)? == 1,
            "Expected standby before stream startup"
        );
        let mut selection = self.read(0x101a, 2)?;
        selection[0] = (selection[0] & 0xf0) | 4;
        self.write(0x101a, &selection)?;
        ensure!(
            self.read(0x101a, 2)? == selection,
            "ES selection readback failed"
        );
        let mut pmt = self.read(0x1136, 2)?;
        pmt[1] = (pmt[1] & !0x70) | 0x40;
        self.write(0x1136, &pmt)?;
        ensure!(self.read(0x1136, 2)? == pmt, "PMT output readback failed");
        self.write_word(0x82000, self.word(0x82000)? & !0x20)?;
        self.set_output(true)?;
        self.pending()?;
        self.write_word(0x1018, 0)?;
        self.api(4, 0x20, 0)?;
        self.wait_state(1)?;
        self.api(5, 0, 2)?;
        self.wait_state(2)
    }
    pub fn stop_stream(&mut self) -> Result<()> {
        let mut state = self.word(0x82008)?;
        if state == 2 {
            self.api(5, 0, 4)?;
            let deadline = Instant::now() + Duration::from_secs(3);
            while self.word(0x82008)? == 2 && Instant::now() < deadline {
                self.usb.pipe(0x81, 32768)?;
                self.pending()?;
            }
            self.wait_state(4)?;
            state = 4;
        }
        if state != 0xffff {
            self.set_output(false)?;
            if state == 3 || state == 4 {
                self.api(5, 0, 1)?;
                self.wait_state(1)?;
            }
        }
        ensure!(self.word(0x82008)? == 1, "Stream did not reach standby");
        let deadline = Instant::now() + Duration::from_secs(3);
        while !self.usb.pipe(0x81, 32768)?.is_empty() {
            ensure!(Instant::now() < deadline, "Stream drain timed out");
        }
        Ok(())
    }
    pub fn cleanup(&mut self) -> Result<()> {
        self.cleaning = true;
        let mut error = None;
        match self.word(0x82008) {
            Ok(1..=4) => {
                if let Err(e) = self.stop_stream() {
                    error = Some(e);
                }
            }
            Ok(_) => (),
            Err(e) => error = Some(e),
        }
        if self.media_open {
            let close = (|| -> Result<()> {
                ensure!(self.word(0x82008)? != 0xffff, "Firmware in error state");
                self.seqcom(0x703, &[0, 0, 0, 3, 0, 0, 0, 0], 4)?;
                self.media_open = false;
                Ok(())
            })();
            if let Err(e) = close {
                if error.is_none() {
                    error = Some(e);
                }
            }
        }
        if let Some(e) = error {
            return Err(e);
        }
        ensure!(
            self.read(0x80080, 16)? == [0; 16],
            "Firmware reported an error"
        );
        Ok(())
    }
}
