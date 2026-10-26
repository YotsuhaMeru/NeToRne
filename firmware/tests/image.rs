use torne_firmware_core::image::{self, ImageError, Records, Trc};
fn sum(b: &[u8]) -> u32 {
    b.chunks_exact(4).fold(0u32, |s, w| {
        s.wrapping_add(u32::from_be_bytes(w.try_into().unwrap()))
    })
}
fn header(address: u32, size_or_count: u32) -> Vec<u8> {
    let mut h = b"MB8AC018".to_vec();
    for word in [0, 0, address, size_or_count, 0] {
        h.extend_from_slice(&word.to_be_bytes());
    }
    h.extend_from_slice(&sum(&h).wrapping_neg().to_be_bytes());
    h
}
fn record(address: u32, payload: &[u8]) -> Vec<u8> {
    let mut r = header(address, payload.len() as u32);
    r.extend_from_slice(payload);
    r.extend_from_slice(&sum(payload).wrapping_neg().to_be_bytes());
    r
}
fn trc() -> Vec<u8> {
    let mut payload = vec![0; 96];
    payload[..4].copy_from_slice(&10u32.to_be_bytes());
    for index in 0..10 {
        let offset = payload.len() as u32;
        payload[16 + index * 8..20 + index * 8].copy_from_slice(&(1u32 << index).to_be_bytes());
        payload[20 + index * 8..24 + index * 8].copy_from_slice(&offset.to_be_bytes());
        payload.extend(header(0, 1));
        payload.extend(record(0xe0000, &[0x9f, 0xa0, 0x9f, 0xa0]));
    }
    let mut bytes = record(0x42000000, &payload);
    bytes.extend([0; 12]);
    bytes
}
#[test]
fn validate_all_records_and_nested_images() {
    let mut idle = Vec::new();
    for _ in 0..10 {
        idle.extend(record(0xe0000, &[0x9f, 0xa0, 0x9f, 0xa0]));
    }
    idle.extend([0; 12]);
    assert_eq!(image::idle(&idle).unwrap().count(), 10);
    assert_eq!(
        Trc::validate(&trc())
            .unwrap()
            .map(|i| i.records.count())
            .sum::<usize>(),
        10
    );
    let last = idle.len() - 13;
    idle[last] ^= 1;
    assert!(matches!(image::idle(&idle), Err(ImageError::Checksum)));
}
#[test]
fn reject_truncation_bad_headers_and_nested_aliases() {
    let bytes = trc();
    for n in 0..bytes.len() {
        assert!(Trc::validate(&bytes[..n]).is_err(), "accepted {n}");
    }
    let mut corrupted = bytes.clone();
    corrupted[0] ^= 1;
    assert!(matches!(Trc::validate(&corrupted), Err(ImageError::Header)));
    let mut payload = bytes[32..bytes.len() - 16].to_vec();
    // Repoint image 1 to image 0, then recompute outer checksum.
    let duplicate = payload[20..24].to_vec();
    payload[28..32].copy_from_slice(&duplicate);
    let mut overlapping = record(0x42000000, &payload);
    overlapping.extend([0; 12]);
    assert!(matches!(
        Trc::validate(&overlapping),
        Err(ImageError::Layout)
    ));
    assert!(Records::validate(&[0; 32], 0, u32::MAX).is_err());
    assert!(Records::validate(&[0; 32], usize::MAX, 1).is_err());
}
