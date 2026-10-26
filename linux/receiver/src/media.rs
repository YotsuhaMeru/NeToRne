use crate::crypto::decrypt_media;
use anyhow::{bail, ensure, Result};
use serde::Serialize;

#[derive(Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Stats {
    pub packets: u64,
    pub media_encrypted_packets: u64,
    pub trailing_bytes: usize,
}
pub struct Converter {
    key: Vec<u8>,
    iv: Vec<u8>,
    pub pending: Vec<u8>,
    pub stats: Stats,
}
impl Converter {
    pub fn new(key: Vec<u8>, iv: Vec<u8>) -> Result<Self> {
        ensure!(
            key.len() == 16 && iv.len() == 16,
            "Media AES requires a 16-byte key and IV"
        );
        Ok(Self {
            key,
            iv,
            pending: Vec::new(),
            stats: Stats::default(),
        })
    }
    pub fn feed(&mut self, chunk: &[u8]) -> Result<Vec<u8>> {
        self.pending.extend_from_slice(chunk);
        let complete = self.pending.len() / 192 * 192;
        let mut out = Vec::with_capacity(complete / 192 * 188);
        for frame in self.pending[..complete].chunks_exact_mut(192) {
            let packet = &mut frame[4..];
            ensure!(packet[0] == 0x47, "Media packet sync lost");
            let control = (packet[3] >> 4) & 3;
            ensure!(control != 0, "Invalid adaptation-field control");
            let start = 4 + if control & 2 != 0 {
                1 + packet[4] as usize
            } else {
                0
            };
            ensure!(start <= 188, "Adaptation field exceeds packet");
            match packet[3] >> 6 {
                1 if control & 1 != 0 => {
                    decrypt_media(&mut packet[start..], &self.key, &self.iv)?;
                    packet[3] &= 0x3f;
                    self.stats.media_encrypted_packets += 1;
                }
                2 | 3 => bail!("Unimplemented broadcast scrambling in media converter"),
                _ => (),
            }
            out.extend_from_slice(packet);
            self.stats.packets += 1;
        }
        self.pending.drain(..complete);
        self.stats.trailing_bytes = self.pending.len();
        Ok(out)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use crate::crypto::cbc;
    fn frame() -> Vec<u8> {
        let mut f = vec![0; 192];
        f[4] = 0x47;
        f[7] = 0x10;
        f
    }
    #[test]
    fn arbitrary_usb_boundaries_and_encryption() {
        let mut encrypted = frame();
        let key = vec![3; 16];
        let iv = vec![7; 16];
        encrypted[7] = 0x70; // media scramble=1, adaptation + payload
        encrypted[8] = 7; // payload length 176, exactly eleven AES blocks
        for (n, b) in encrypted[16..].iter_mut().enumerate() {
            *b = n as u8;
        }
        let mut expected = encrypted[4..].to_vec();
        expected[3] &= 0x3f;
        cbc(&mut encrypted[16..], &key, &iv, true).unwrap();
        let mut converter = Converter::new(key, iv).unwrap();
        let mut actual = Vec::new();
        for chunk in encrypted.chunks(7) {
            actual.extend(converter.feed(chunk).unwrap());
        }
        assert_eq!(actual, expected);
        assert_eq!(converter.stats.packets, 1);
        assert_eq!(converter.stats.media_encrypted_packets, 1);
        assert_eq!(converter.stats.trailing_bytes, 0);
    }
    #[test]
    fn reject_bad_packets() {
        for field in [0x00, 0x90, 0xd0] {
            let mut f = frame();
            f[7] = field;
            assert!(Converter::new(vec![0; 16], vec![0; 16])
                .unwrap()
                .feed(&f)
                .is_err());
        }
        let mut f = frame();
        f[7] = 0x30;
        f[8] = 184;
        assert!(Converter::new(vec![0; 16], vec![0; 16])
            .unwrap()
            .feed(&f)
            .is_err());
    }
}
