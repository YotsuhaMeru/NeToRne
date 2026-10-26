//! Board-independent lifecycle. This is an internal Rust API, NOT a new USB
//! protocol. The board adapter and existing vendor command dispatcher are TBD.
use crate::stream::{Generation, Packet, Stream, StreamError};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Channel(u8);
impl Channel {
    pub fn physical(value: u8) -> Option<Self> {
        if (13..=52).contains(&value) {
            Some(Self(value))
        } else {
            None
        }
    }
    pub fn bon_index(space: u32, index: u32) -> Option<Self> {
        if space == 0 && index < 40 {
            Some(Self(index as u8 + 13))
        } else {
            None
        }
    }
    pub const fn number(self) -> u8 {
        self.0
    }
    pub const fn frequency_khz(self) -> u32 {
        473143 + (self.0 as u32 - 13) * 6000
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Reception {
    pub locked: bool,
    /// Only Some when a calibrated measurement is available; never invent dB.
    pub cnr_centidb: Option<u16>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State {
    Cold,
    Standby,
    Streaming,
    Fault,
}
#[derive(Debug, PartialEq, Eq)]
pub enum Error<E> {
    Board(E),
    InvalidState,
    NoLock,
    Stream(StreamError),
}

/// The future FR adapter must retain the ORIGINAL B-CAS implementation and
/// session lifetime. No keys/APDUs enter this core. Preserve ECM/EMM observation
/// upstream even though the forwarding path never selects output PIDs.
pub trait Board {
    type Error;
    fn initialize_legacy(&mut self) -> Result<(), Self::Error>;
    /// Quiesce TS input and cancel/finish USB and DMA transfers. On success no
    /// buffer borrowed from Stream remains in use by hardware.
    fn stop_transport(&mut self) -> Result<(), Self::Error>;
    /// Drain DMA, hardware and USB staging queues before changing generation.
    fn drain_transport(&mut self) -> Result<(), Self::Error>;
    /// Host-driven tuner writes are allowed; do not reset the card session.
    fn tune(&mut self, channel: Channel) -> Result<(), Self::Error>;
    fn reception(&mut self) -> Result<Reception, Self::Error>;
    /// Start transfer AFTER the unchanged descrambler, BEFORE the old remuxer.
    fn start_transport(&mut self, generation: Generation) -> Result<(), Self::Error>;
    /// Continue original card/ECM/EMM/authentication housekeeping in all states.
    fn service_legacy(&mut self) -> Result<(), Self::Error>;
    fn shutdown_legacy(&mut self) -> Result<(), Self::Error>;
}

pub struct Controller<B: Board, const N: usize> {
    board: B,
    stream: Stream<N>,
    state: State,
    channel: Option<Channel>,
    reception: Reception,
}
impl<B: Board, const N: usize> Controller<B, N> {
    pub const fn new(board: B) -> Self {
        Self {
            board,
            stream: Stream::new(),
            state: State::Cold,
            channel: None,
            reception: Reception {
                locked: false,
                cnr_centidb: None,
            },
        }
    }
    pub const fn state(&self) -> State {
        self.state
    }
    pub const fn channel(&self) -> Option<Channel> {
        self.channel
    }
    pub const fn reception(&self) -> Reception {
        self.reception
    }
    pub const fn generation(&self) -> Generation {
        self.stream.generation()
    }
    pub const fn queued(&self) -> usize {
        self.stream.queued()
    }

    fn board_result<T>(&mut self, result: Result<T, B::Error>) -> Result<T, Error<B::Error>> {
        result.map_err(|e| {
            self.state = State::Fault;
            self.channel = None;
            self.reception = Reception::default();
            Error::Board(e)
        })
    }
    pub fn initialize(&mut self) -> Result<(), Error<B::Error>> {
        if self.state != State::Cold {
            return Err(Error::InvalidState);
        }
        let result = self.board.initialize_legacy();
        self.board_result(result)?;
        self.state = State::Standby;
        Ok(())
    }
    fn quiesce(&mut self, advance_generation: bool) -> Result<(), Error<B::Error>> {
        // Always stop even from standby: RF/DMA staging may still contain data.
        self.state = State::Standby;
        self.reception = Reception::default();
        let result = self.board.stop_transport();
        self.board_result(result)?;
        let result = self.board.drain_transport();
        self.board_result(result)?;
        let next = if advance_generation {
            self.generation().next().map_err(|e| {
                self.state = State::Fault;
                self.channel = None;
                Error::Stream(e)
            })?
        } else {
            self.generation()
        };
        self.stream.reset(next);
        Ok(())
    }
    fn require_active(&self) -> Result<(), Error<B::Error>> {
        match self.state {
            State::Standby | State::Streaming => Ok(()),
            _ => Err(Error::InvalidState),
        }
    }
    fn start(&mut self) -> Result<(), Error<B::Error>> {
        let result = self.board.reception();
        self.reception = self.board_result(result)?;
        if !self.reception.locked {
            return Err(Error::NoLock);
        }
        let result = self.board.start_transport(self.generation());
        self.board_result(result)?;
        self.state = State::Streaming;
        Ok(())
    }
    pub fn tune(&mut self, channel: Channel) -> Result<(), Error<B::Error>> {
        self.require_active()?;
        // Even reselecting the same channel goes through lock verification.
        self.quiesce(true)?;
        self.channel = None;
        let result = self.board.tune(channel);
        self.board_result(result)?;
        self.start()?;
        self.channel = Some(channel);
        Ok(())
    }
    pub fn stop(&mut self) -> Result<(), Error<B::Error>> {
        self.require_active()?;
        self.quiesce(true)
    }
    pub fn purge(&mut self) -> Result<(), Error<B::Error>> {
        self.require_active()?;
        let restart = self.state == State::Streaming;
        self.quiesce(true)?;
        if restart {
            if let Err(error) = self.start() {
                self.channel = None;
                return Err(error);
            }
        }
        Ok(())
    }
    pub fn tick(&mut self) -> Result<(), Error<B::Error>> {
        self.require_active()?;
        let result = self.board.service_legacy();
        self.board_result(result)?;
        let result = self.board.reception();
        let reception = self.board_result(result)?;
        if self.state == State::Streaming && !reception.locked {
            self.quiesce(true)?;
            self.channel = None;
            return Err(Error::NoLock);
        }
        self.reception = reception;
        Ok(())
    }
    pub fn feed(&mut self, generation: Generation, bytes: &[u8]) -> Result<(), Error<B::Error>> {
        if self.state != State::Streaming {
            return Err(Error::InvalidState);
        }
        self.stream.feed(generation, bytes).map_err(Error::Stream)
    }
    pub fn front(&self) -> Option<&Packet> {
        if self.state == State::Streaming {
            self.stream.front()
        } else {
            None
        }
    }
    pub fn commit(&mut self) -> bool {
        self.state == State::Streaming && self.stream.commit()
    }
    /// Also allowed after a partial boot or a transport error. A failed stop
    /// keeps buffers alive and prevents shutdown/reinitialization until retried.
    pub fn close(&mut self) -> Result<(), Error<B::Error>> {
        if self.state == State::Cold {
            return Ok(());
        }
        // Closing must still release the legacy session if generations ran out.
        // Cold state blocks feed; the next tune must advance before restarting.
        self.quiesce(false)?;
        let result = self.board.shutdown_legacy();
        self.board_result(result)?;
        self.state = State::Cold;
        self.channel = None;
        Ok(())
    }
}
