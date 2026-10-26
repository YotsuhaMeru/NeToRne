use torne_firmware_core::{controller::*, stream::*};

fn packet(pid: u16, seed: u8) -> Packet {
    let mut p = [0; TS_BYTES];
    for (n, byte) in p.iter_mut().enumerate() {
        *byte = seed.wrapping_add(n as u8);
    }
    p[0] = 0x47;
    p[1] = 0xe0 | (pid >> 8) as u8;
    p[2] = pid as u8;
    // Retain TEI, PUSI, priority, scrambled flags, adaptation and CC as received.
    p[3] = 0xf7;
    p
}

#[test]
fn all_8192_pids_and_every_byte_pass_unchanged() {
    let mut stream = Stream::<2>::new();
    for pid in 0..8192 {
        let p = packet(pid, pid as u8);
        stream.feed(stream.generation(), &p).unwrap();
        assert_eq!(stream.front(), Some(&p));
        assert!(stream.commit());
    }
    assert_eq!(stream.queued(), 0);
}
#[test]
fn entire_batch_rejected_on_pressure_or_malformed_input() {
    let mut stream = Stream::<2>::new();
    let g = stream.generation();
    let a = packet(0, 1);
    let b = packet(0x1fff, 2);
    stream.feed(g, &a).unwrap();
    let both: Vec<_> = [b, a].into_iter().flatten().collect();
    assert_eq!(stream.feed(g, &both), Err(StreamError::Backpressure));
    assert_eq!(stream.queued(), 1);
    assert_eq!(stream.front(), Some(&a));
    let mut malformed = both.clone();
    malformed[188] = 0;
    assert_eq!(stream.feed(g, &malformed), Err(StreamError::BadSync));
    assert_eq!(
        stream.feed(g, &both[..189]),
        Err(StreamError::PartialPacket)
    );
    assert_eq!(stream.queued(), 1);
    // USB retry must keep the same packet until explicitly committed.
    assert_eq!(stream.front(), stream.front());
    stream.commit();
    stream.feed(g, &both).unwrap();
    assert_eq!(stream.front(), Some(&b));
    stream.commit();
    assert_eq!(stream.front(), Some(&a));
}
#[test]
fn zero_capacity_returns_pressure_without_panic() {
    let mut stream = Stream::<0>::new();
    let g = stream.generation();
    stream.feed(g, &[]).unwrap();
    assert_eq!(
        stream.feed(g, &packet(1, 0)),
        Err(StreamError::Backpressure)
    );
    assert_eq!(stream.front(), None);
    assert!(!stream.commit());
}
#[derive(Default)]
struct FakeBoard {
    log: Vec<&'static str>,
    fail: Option<&'static str>,
    locked: bool,
}
impl FakeBoard {
    fn call(&mut self, name: &'static str) -> Result<(), &'static str> {
        self.log.push(name);
        if self.fail == Some(name) {
            Err(name)
        } else {
            Ok(())
        }
    }
}
impl Board for FakeBoard {
    type Error = &'static str;
    fn initialize_legacy(&mut self) -> Result<(), Self::Error> {
        self.call("legacy-init")
    }
    fn stop_transport(&mut self) -> Result<(), Self::Error> {
        self.call("stop")
    }
    fn drain_transport(&mut self) -> Result<(), Self::Error> {
        self.call("drain")
    }
    fn tune(&mut self, _: Channel) -> Result<(), Self::Error> {
        self.call("tune")
    }
    fn reception(&mut self) -> Result<Reception, Self::Error> {
        self.call("lock")?;
        Ok(Reception {
            locked: self.locked,
            cnr_centidb: None,
        })
    }
    fn start_transport(&mut self, _: Generation) -> Result<(), Self::Error> {
        self.call("start")
    }
    fn service_legacy(&mut self) -> Result<(), Self::Error> {
        self.call("legacy-service")
    }
    fn shutdown_legacy(&mut self) -> Result<(), Self::Error> {
        self.call("legacy-close")
    }
}
fn controller() -> Controller<FakeBoard, 4> {
    let mut c = Controller::new(FakeBoard {
        locked: true,
        ..Default::default()
    });
    c.initialize().unwrap();
    c.tune(Channel::physical(20).unwrap()).unwrap();
    c
}
#[test]
fn channel_change_and_purge_reject_old_dma_completions() {
    let mut c = controller();
    let old = c.generation();
    let p = packet(100, 1);
    c.feed(old, &p).unwrap();
    c.tune(Channel::physical(22).unwrap()).unwrap();
    assert_eq!(c.queued(), 0);
    assert_eq!(c.channel().unwrap().number(), 22);
    assert_eq!(
        c.feed(old, &p),
        Err(Error::Stream(StreamError::StaleGeneration))
    );
    let current = c.generation();
    c.feed(current, &p).unwrap();
    c.purge().unwrap();
    assert_eq!(c.queued(), 0);
    assert_eq!(c.channel().unwrap().number(), 22);
    assert_eq!(
        c.feed(current, &p),
        Err(Error::Stream(StreamError::StaleGeneration))
    );
    c.close().unwrap();
    assert_eq!(c.state(), State::Cold);
    assert_eq!(c.front(), None);
    assert_eq!(c.feed(c.generation(), &p), Err(Error::InvalidState));
}
#[test]
fn boot_or_any_tune_stage_failure_never_exposes_ts() {
    for stage in ["legacy-init", "stop", "drain", "tune", "lock", "start"] {
        let mut c = Controller::<_, 2>::new(FakeBoard {
            fail: Some(stage),
            locked: true,
            ..Default::default()
        });
        let result = c
            .initialize()
            .and_then(|()| c.tune(Channel::physical(20).unwrap()));
        assert_eq!(result, Err(Error::Board(stage)), "{stage}");
        assert_eq!(c.state(), State::Fault);
        assert_eq!(c.front(), None);
        assert_eq!(c.channel(), None);
    }
}
#[test]
fn unlocked_channel_never_starts_transport() {
    let mut c = Controller::<_, 2>::new(FakeBoard::default());
    c.initialize().unwrap();
    assert_eq!(c.tune(Channel::physical(20).unwrap()), Err(Error::NoLock));
    assert_eq!(c.state(), State::Standby);
    assert_eq!(c.channel(), None);
    assert_eq!(c.front(), None);
    assert_eq!(c.reception().cnr_centidb, None);
}
#[test]
fn stop_close_reopen_and_housekeeping() {
    let mut c = controller();
    c.tick().unwrap();
    c.stop().unwrap();
    assert_eq!(c.state(), State::Standby);
    c.tick().unwrap();
    c.close().unwrap();
    c.close().unwrap();
    c.initialize().unwrap();
    c.tune(Channel::physical(13).unwrap()).unwrap();
    assert_eq!(c.state(), State::Streaming);
}
#[test]
fn bon_channel_boundaries() {
    assert_eq!(Channel::bon_index(0, 0).unwrap().frequency_khz(), 473143);
    assert_eq!(Channel::bon_index(0, 39).unwrap().number(), 52);
    assert_eq!(Channel::bon_index(0, 40), None);
    assert_eq!(Channel::bon_index(1, 0), None);
    assert_eq!(Channel::bon_index(0, u32::MAX), None);
    assert_eq!(Channel::physical(12), None);
    assert_eq!(Channel::physical(53), None);
}
