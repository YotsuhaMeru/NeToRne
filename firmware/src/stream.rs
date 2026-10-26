//! Whole-packet FIFO: no PID selection, PSI regeneration, PCR rewriting,
//! PES processing, null-packet insertion or continuity-counter repair.
pub const TS_BYTES: usize = 188;
pub type Packet = [u8; TS_BYTES];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StreamError {
    PartialPacket,
    BadSync,
    Backpressure,
    StaleGeneration,
    GenerationExhausted,
}

/// A DMA completion must retain the generation captured when DMA was started.
/// Generations never wrap, so an old completion cannot become valid again.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Generation(u32);
impl Generation {
    pub const fn initial() -> Self {
        Self(0)
    }
    pub fn next(self) -> Result<Self, StreamError> {
        self.0
            .checked_add(1)
            .map(Self)
            .ok_or(StreamError::GenerationExhausted)
    }
    pub const fn value(self) -> u32 {
        self.0
    }
}

/// Fixed memory; N is a packet count, not bytes. Caller serializes DMA and USB
/// access. Backpressure rejects an entire batch without overwriting any packet.
pub struct Stream<const N: usize> {
    packets: [Packet; N],
    head: usize,
    len: usize,
    generation: Generation,
}
impl<const N: usize> Default for Stream<N> {
    fn default() -> Self {
        Self::new()
    }
}
impl<const N: usize> Stream<N> {
    pub const fn new() -> Self {
        Self {
            packets: [[0; TS_BYTES]; N],
            head: 0,
            len: 0,
            generation: Generation::initial(),
        }
    }
    pub const fn generation(&self) -> Generation {
        self.generation
    }
    pub const fn queued(&self) -> usize {
        self.len
    }
    pub fn reset(&mut self, generation: Generation) {
        self.head = 0;
        self.len = 0;
        self.generation = generation;
    }
    pub fn feed(&mut self, generation: Generation, bytes: &[u8]) -> Result<(), StreamError> {
        if generation != self.generation {
            return Err(StreamError::StaleGeneration);
        }
        if bytes.len() % TS_BYTES != 0 {
            return Err(StreamError::PartialPacket);
        }
        if bytes.chunks_exact(TS_BYTES).any(|p| p[0] != 0x47) {
            return Err(StreamError::BadSync);
        }
        let count = bytes.len() / TS_BYTES;
        if count > N - self.len {
            return Err(StreamError::Backpressure);
        }
        for packet in bytes.chunks_exact(TS_BYTES) {
            let tail = (self.head + self.len) % N;
            self.packets[tail].copy_from_slice(packet);
            self.len += 1;
        }
        Ok(())
    }
    /// Borrow bytes for USB framing. Only commit after the USB layer owns a
    /// copy, or after a zero-copy DMA transfer has completed successfully.
    pub fn front(&self) -> Option<&Packet> {
        if self.len == 0 {
            None
        } else {
            Some(&self.packets[self.head])
        }
    }
    pub fn commit(&mut self) -> bool {
        if self.len == 0 {
            return false;
        }
        self.head = (self.head + 1) % N;
        self.len -= 1;
        true
    }
}
