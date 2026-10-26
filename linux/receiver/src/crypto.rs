use aes::cipher::{generic_array::GenericArray, BlockDecrypt, BlockEncrypt, KeyInit};
use aes::Aes128;
use anyhow::{ensure, Result};
use sha1::{Digest, Sha1};
use sha2::Sha256;

pub fn sha1(data: &[u8]) -> Vec<u8> {
    Sha1::digest(data).to_vec()
}
pub fn hex(data: &[u8]) -> String {
    data.iter().map(|b| format!("{b:02x}")).collect()
}
pub fn sha256(data: &[u8]) -> String {
    hex(&Sha256::digest(data))
}

pub fn cbc(data: &mut [u8], key: &[u8], iv: &[u8], encrypt: bool) -> Result<()> {
    ensure!(
        key.len() == 16 && iv.len() == 16 && data.len() % 16 == 0,
        "Invalid AES/CBC dimensions"
    );
    let cipher = Aes128::new_from_slice(key)?;
    let mut previous = iv.to_vec();
    for block in data.chunks_exact_mut(16) {
        if encrypt {
            for (b, p) in block.iter_mut().zip(&previous) {
                *b ^= p;
            }
            cipher.encrypt_block(GenericArray::from_mut_slice(block));
            previous.copy_from_slice(block);
        } else {
            let ciphertext = block.to_vec();
            cipher.decrypt_block(GenericArray::from_mut_slice(block));
            for (b, p) in block.iter_mut().zip(&previous) {
                *b ^= p;
            }
            previous = ciphertext;
        }
    }
    Ok(())
}

pub fn decrypt_media(data: &mut [u8], key: &[u8], iv: &[u8]) -> Result<()> {
    let full = data.len() & !15;
    let mut previous = if full == 0 {
        iv.to_vec()
    } else {
        data[full - 16..full].to_vec()
    };
    cbc(&mut data[..full], key, iv, false)?;
    if full != data.len() {
        let cipher = Aes128::new_from_slice(key)?;
        cipher.encrypt_block(GenericArray::from_mut_slice(&mut previous));
        for (b, mask) in data[full..].iter_mut().zip(previous) {
            *b ^= mask;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    fn unhex(s: &str) -> Vec<u8> {
        (0..s.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&s[i..i + 2], 16).unwrap())
            .collect()
    }
    #[test]
    fn nist_cbc_and_media_tail() {
        let key = unhex("2b7e151628aed2a6abf7158809cf4f3c");
        let iv = unhex("000102030405060708090a0b0c0d0e0f");
        let plain = unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51");
        let expected = unhex("7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2");
        let mut encrypted = plain.clone();
        cbc(&mut encrypted, &key, &iv, true).unwrap();
        assert_eq!(encrypted, expected);
        cbc(&mut encrypted, &key, &iv, false).unwrap();
        assert_eq!(encrypted, plain);
        // The media tail uses AES(previous ciphertext), with no padding.
        let mut tail = expected.clone();
        let mut mask = expected[16..].to_vec();
        Aes128::new_from_slice(&key)
            .unwrap()
            .encrypt_block(GenericArray::from_mut_slice(&mut mask));
        tail.extend((0..7).map(|n| mask[n] ^ n as u8));
        decrypt_media(&mut tail, &key, &iv).unwrap();
        assert_eq!(&tail[..32], plain);
        assert_eq!(&tail[32..], &[0, 1, 2, 3, 4, 5, 6]);
    }
}
