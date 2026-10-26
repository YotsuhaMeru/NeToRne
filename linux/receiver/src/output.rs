use anyhow::{ensure, Result};
use std::io::{self, Write};

pub struct Output<W> {
    pub sink: W,
    pub pending: Vec<u8>,
    pub bytes_written: u64,
}
impl<W: Write> Output<W> {
    pub fn new(sink: W) -> Self {
        Self {
            sink,
            pending: Vec::new(),
            bytes_written: 0,
        }
    }
    pub fn feed(&mut self, ts: &[u8]) -> Result<()> {
        ensure!(
            ts.len() % 188 == 0,
            "Output must consist of complete TS packets"
        );
        ensure!(
            self.pending.len() + ts.len() <= 8 * 1024 * 1024,
            "TS consumer stalled; output queue reached its limit"
        );
        self.pending.extend_from_slice(ts);
        self.pump()?;
        Ok(())
    }
    pub fn pump(&mut self) -> io::Result<()> {
        for _ in 0..16 {
            if self.pending.is_empty() {
                break;
            }
            match self
                .sink
                .write(&self.pending[..self.pending.len().min(65536)])
            {
                Ok(0) => return Err(io::ErrorKind::BrokenPipe.into()),
                Ok(n) => {
                    self.pending.drain(..n);
                    self.bytes_written += n as u64;
                }
                Err(e) if e.kind() == io::ErrorKind::WouldBlock => break,
                Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
                Err(e) => return Err(e),
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Slow {
        data: Vec<u8>,
        blocked: bool,
    }
    impl Write for Slow {
        fn write(&mut self, b: &[u8]) -> io::Result<usize> {
            if self.blocked {
                return Err(io::ErrorKind::WouldBlock.into());
            }
            let n = b.len().min(17);
            self.data.extend_from_slice(&b[..n]);
            Ok(n)
        }
        fn flush(&mut self) -> io::Result<()> {
            Ok(())
        }
    }
    #[test]
    fn short_writes_backpressure_and_bound() {
        let mut output = Output::new(Slow {
            data: Vec::new(),
            blocked: true,
        });
        let data = vec![0x47; 188 * 3];
        output.feed(&data).unwrap();
        assert_eq!(output.pending, data);
        output.sink.blocked = false;
        for _ in 0..3 {
            output.pump().unwrap();
        }
        assert_eq!(output.sink.data, data);
        assert_eq!(output.bytes_written, 564);
        assert!(output.pending.is_empty());
        output.sink.blocked = true;
        assert!(output.feed(&vec![0; 188 * 45000]).is_err());
        assert!(output.feed(&[0; 1]).is_err());
    }
}
