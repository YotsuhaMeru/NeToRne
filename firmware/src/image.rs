//! Read-only MB8AC018 plaintext validator. Accepts decrypted local images;
//! deliberately contains no firmware decryption keys or Flash/USB operations.
const MAGIC: &[u8; 8] = b"MB8AC018";
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ImageError {
    Truncated,
    Header,
    Checksum,
    Layout,
    Overflow,
}
fn word(bytes: &[u8], offset: usize) -> Result<u32, ImageError> {
    let end = offset.checked_add(4).ok_or(ImageError::Overflow)?;
    let b = bytes.get(offset..end).ok_or(ImageError::Truncated)?;
    Ok(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
}
fn sum(bytes: &[u8]) -> Result<u32, ImageError> {
    if bytes.len() % 4 != 0 {
        return Err(ImageError::Layout);
    }
    Ok(bytes.chunks_exact(4).fold(0u32, |s, b| {
        s.wrapping_add(u32::from_be_bytes([b[0], b[1], b[2], b[3]]))
    }))
}
fn header(bytes: &[u8], offset: usize) -> Result<&[u8], ImageError> {
    let end = offset.checked_add(32).ok_or(ImageError::Overflow)?;
    let h = bytes.get(offset..end).ok_or(ImageError::Truncated)?;
    if &h[..8] != MAGIC {
        return Err(ImageError::Header);
    }
    if sum(h)? != 0 {
        return Err(ImageError::Checksum);
    }
    Ok(h)
}

#[derive(Clone, Copy, Debug)]
pub struct Record<'a> {
    pub offset: usize,
    pub address: u32,
    pub payload: &'a [u8],
    pub end: usize,
}
fn record(bytes: &[u8], offset: usize) -> Result<Record<'_>, ImageError> {
    let h = header(bytes, offset)?;
    let size = word(h, 20)? as usize;
    if size == 0 || size % 4 != 0 || word(h, 24)? != 0 {
        return Err(ImageError::Layout);
    }
    let start = offset.checked_add(32).ok_or(ImageError::Overflow)?;
    let end = start.checked_add(size).ok_or(ImageError::Overflow)?;
    let checksum_end = end.checked_add(4).ok_or(ImageError::Overflow)?;
    let body = bytes
        .get(start..checksum_end)
        .ok_or(ImageError::Truncated)?;
    if sum(body)? != 0 {
        return Err(ImageError::Checksum);
    }
    Ok(Record {
        offset,
        address: word(h, 16)?,
        payload: &body[..size],
        end: checksum_end,
    })
}

/// An iterator only constructed after validating EVERY record and its bounds.
pub struct Records<'a> {
    bytes: &'a [u8],
    offset: usize,
    remaining: u32,
    end: usize,
}
impl<'a> Records<'a> {
    pub fn validate(bytes: &'a [u8], offset: usize, count: u32) -> Result<Self, ImageError> {
        if count == 0 || count > 128 {
            return Err(ImageError::Layout);
        }
        let mut end = offset;
        for _ in 0..count {
            end = record(bytes, end)?.end;
        }
        Ok(Self {
            bytes,
            offset,
            remaining: count,
            end,
        })
    }
    pub const fn end(&self) -> usize {
        self.end
    }
}
impl<'a> Iterator for Records<'a> {
    type Item = Record<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        if self.remaining == 0 {
            return None;
        }
        // Immutable backing bytes were fully checked at construction.
        let r = record(self.bytes, self.offset).ok()?;
        self.offset = r.end;
        self.remaining -= 1;
        Some(r)
    }
}

pub fn idle(bytes: &[u8]) -> Result<Records<'_>, ImageError> {
    let records = Records::validate(bytes, 0, 10)?;
    if bytes.len() - records.end() != 12 || bytes[records.end()..].iter().any(|&b| b != 0) {
        return Err(ImageError::Layout);
    }
    Ok(records)
}

pub struct NestedImage<'a> {
    pub index: u32,
    pub mask: u32,
    pub offset: usize,
    pub records: Records<'a>,
}
pub struct Trc<'a> {
    top: Record<'a>,
    next: u32,
}
impl<'a> Trc<'a> {
    pub fn validate(bytes: &'a [u8]) -> Result<Self, ImageError> {
        let top = record(bytes, 0)?;
        if top.address != 0x42000000
            || bytes.len() - top.end != 12
            || bytes[top.end..].iter().any(|&b| b != 0)
            || word(top.payload, 0)? != 10
        {
            return Err(ImageError::Layout);
        }
        let trc = Self { top, next: 0 };
        let mut last_end = 16 + 10 * 8;
        for index in 0..10 {
            let image = trc.at(index)?;
            // Images must not overlap the index or each other.
            if image.offset < last_end {
                return Err(ImageError::Layout);
            }
            last_end = image.records.end();
        }
        Ok(trc)
    }
    fn at(&self, index: u32) -> Result<NestedImage<'a>, ImageError> {
        let entry = 16 + index as usize * 8;
        let offset = word(self.top.payload, entry + 4)? as usize;
        let h = header(self.top.payload, offset)?;
        if word(h, 24)? != 0 {
            return Err(ImageError::Layout);
        }
        let count = word(h, 20)?;
        let records = Records::validate(self.top.payload, offset + 32, count)?;
        Ok(NestedImage {
            index,
            mask: word(self.top.payload, entry)?,
            offset,
            records,
        })
    }
}
impl<'a> Iterator for Trc<'a> {
    type Item = NestedImage<'a>;
    fn next(&mut self) -> Option<Self::Item> {
        if self.next >= 10 {
            return None;
        }
        let image = self.at(self.next).ok()?;
        self.next += 1;
        Some(image)
    }
}
