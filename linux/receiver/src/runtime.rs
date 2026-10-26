use crate::{
    assets::Assets,
    crypto::hex,
    media::Converter,
    output::Output,
    protocol::{Cancelled, Session, Transport},
    usb::Usb,
    Args,
};
use anyhow::{Context, Result};
use fs2::FileExt;
use serde_json::{json, Value};
use std::{
    fs::{self, OpenOptions},
    io::{self, IsTerminal, Write},
    path::Path,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::{Duration, Instant, SystemTime},
};

fn save(path: &Path, status: &Value) -> Result<()> {
    let temporary = path.with_extension("json.new");
    fs::write(
        &temporary,
        format!("{}\n", serde_json::to_string_pretty(status)?),
    )?;
    fs::rename(temporary, path)?;
    Ok(())
}
fn sequence(path: &Path, override_value: Option<u64>) -> Result<u8> {
    if let Some(n) = override_value {
        return Ok((n & 63) as u8);
    }
    match fs::read(path) {
        Ok(b) => {
            let previous: Value =
                serde_json::from_slice(&b).context("Read saved receiver status")?;
            let n = previous["nextSequence"].as_u64().context(
                "Missing or invalid nextSequence in receiver.json; use --sequence to recover",
            )?;
            Ok((n & 63) as u8)
        }
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(0),
        Err(e) => Err(e.into()),
    }
}
struct Stdout;
impl Write for Stdout {
    fn write(&mut self, b: &[u8]) -> io::Result<usize> {
        // A raw nonblocking write avoids buffering locks and lets authentication
        // and SIGTERM continue when Mirakurun stops reading.
        let n = unsafe { libc::write(libc::STDOUT_FILENO, b.as_ptr().cast(), b.len()) };
        if n < 0 {
            Err(io::Error::last_os_error())
        } else {
            Ok(n as usize)
        }
    }
    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}
struct Nonblocking {
    original: i32,
}
impl Nonblocking {
    fn stdout() -> Result<Self> {
        let original = unsafe { libc::fcntl(libc::STDOUT_FILENO, libc::F_GETFL) };
        if original < 0 {
            return Err(io::Error::last_os_error().into());
        }
        if unsafe {
            libc::fcntl(
                libc::STDOUT_FILENO,
                libc::F_SETFL,
                original | libc::O_NONBLOCK,
            )
        } < 0
        {
            return Err(io::Error::last_os_error().into());
        }
        Ok(Self { original })
    }
}
impl Drop for Nonblocking {
    fn drop(&mut self) {
        unsafe {
            libc::fcntl(libc::STDOUT_FILENO, libc::F_SETFL, self.original);
        }
    }
}
fn stopped(args: &Args, flag: &AtomicBool, launched: SystemTime) -> bool {
    if flag.load(Ordering::Relaxed) {
        return true;
    }
    if args
        .stop_file
        .as_ref()
        .and_then(|p| fs::metadata(p).ok())
        .and_then(|m| m.modified().ok())
        .is_some_and(|t| t >= launched)
    {
        flag.store(true, Ordering::Relaxed);
        return true;
    }
    false
}
fn receive<T: Transport, W: Write>(
    session: &mut Session<T>,
    args: &Args,
    status: &mut Value,
    path: &Path,
    writer: &mut Output<W>,
    launched: SystemTime,
) -> Result<()> {
    session.prepare(args.channel)?;
    session.check_cancel()?;
    let mut converter = Converter::new(session.media_key.clone(), session.assets.iv.clone())?;
    let started = Instant::now();
    let mut last_auth = started;
    let mut last_status = started;
    let mut last_data = started;
    status["result"] = json!("receiving");
    status["streamStarted"] = json!(true);
    status["mediaSessionOpened"] = json!(true);
    save(path, status)?;
    eprintln!("torne: receiving UHF {}", args.channel);
    let outcome = (|| -> Result<()> {
        while !stopped(args, &session.stopped, launched) {
            if args.seconds > 0.0 && started.elapsed().as_secs_f64() >= args.seconds {
                break;
            }
            if last_auth.elapsed() >= Duration::from_secs(1) {
                session.runtime_auth()?;
                last_auth = Instant::now();
            }
            writer.pump()?;
            let chunk = session.usb.pipe(0x81, 32768)?;
            if !chunk.is_empty() {
                last_data = Instant::now();
                status["bytesReceived"] =
                    json!(status["bytesReceived"].as_u64().unwrap_or(0) + chunk.len() as u64);
                let ts = converter.feed(&chunk)?;
                status["tsBytes"] =
                    json!(status["tsBytes"].as_u64().unwrap_or(0) + ts.len() as u64);
                writer.feed(&ts)?;
            } else {
                anyhow::ensure!(
                    last_data.elapsed() < Duration::from_secs(5),
                    "No USB broadcast data for five seconds"
                );
            }
            if last_status.elapsed() >= Duration::from_secs(1) {
                status["nextSequence"] = json!(session.sequence);
                status["elapsedSeconds"] = json!(started.elapsed().as_secs_f64());
                status["stdoutBytes"] = json!(writer.bytes_written);
                status["runtimeAuthentications"] = json!(session.authentications);
                save(path, status)?;
                last_status = Instant::now();
            }
        }
        Ok(())
    })();
    status["elapsedSeconds"] = json!(started.elapsed().as_secs_f64());
    status["mediaConversion"] = serde_json::to_value(&converter.stats)?;
    status["stdoutBytes"] = json!(writer.bytes_written);
    status["pendingOutputBytes"] = json!(writer.pending.len());
    outcome
}
pub fn run(mut args: Args) -> Result<()> {
    args.root = fs::canonicalize(&args.root).context("Resolve --root")?;
    let assets = Assets::load(&args.root, &args.operational_firmware)?;
    let mut status = json!({"result":"validated","backend":"libusb","root":args.root,"physicalChannel":args.channel,
        "operationalFirmware":args.root.join(&args.operational_firmware),"firmwareSha256":assets.firmware_sha256,
        "firmwareVariant":assets.variant,"tsProcessing":if assets.variant=="original" {"firmware-remux-es-preserved"} else {"firmware-psi-passthrough-remux"}});
    if args.check {
        let _ = rusb::Context::new()?;
        eprintln!("{status}");
        return Ok(());
    }
    anyhow::ensure!(
        !io::stdout().is_terminal(),
        "Redirect stdout to a TS file or pipe it to Mirakurun"
    );
    fs::create_dir_all(&args.state_dir)?;
    let lock = OpenOptions::new()
        .create(true)
        .truncate(false)
        .read(true)
        .write(true)
        .open(args.state_dir.join("device.lock"))?;
    FileExt::try_lock_exclusive(&lock).context("Another receiver holds device.lock")?;
    let path = args.state_dir.join("receiver.json");
    let seq = sequence(&path, args.sequence)?;
    let flag = Arc::new(AtomicBool::new(false));
    signal_hook::flag::register(signal_hook::consts::SIGTERM, flag.clone())?;
    signal_hook::flag::register(signal_hook::consts::SIGINT, flag.clone())?;
    let _mode = Nonblocking::stdout()?;
    let launched = SystemTime::now();
    status["result"] = json!("initializing");
    status["pid"] = json!(std::process::id());
    status["nextSequence"] = json!(seq);
    for name in [
        "bytesReceived",
        "tsBytes",
        "stdoutBytes",
        "runtimeAuthentications",
    ] {
        status[name] = json!(0);
    }
    save(&path, &status)?;
    let outcome = (|| -> Result<()> {
        let log = OpenOptions::new()
            .create(true)
            .append(true)
            .open(args.state_dir.join("control.jsonl"))?;
        let usb = Usb::open()?;
        let mut session = Session::new(usb, assets, seq, flag, log);
        session.stop_file = args.stop_file.clone().map(|path| (path, launched));
        let mut writer = Output::new(Stdout);
        let receiving = receive(
            &mut session,
            &args,
            &mut status,
            &path,
            &mut writer,
            launched,
        );
        // Always stop USB and close the media session, including startup,
        // conversion, status-write, consumer and authentication failures.
        let cleanup = session.cleanup();
        status["nextSequence"] = json!(session.sequence);
        status["runtimeAuthentications"] = json!(session.authentications);
        status["mediaSessionClosed"] = json!(!session.media_open);
        match session.word(0x82008) {
            Ok(n) => status["stateFinalHex"] = json!(format!("{n:04x}")),
            Err(e) => status["stateReadError"] = json!(format!("{e:#}")),
        }
        if let Ok(b) = session.read(0x80080, 16) {
            status["errorInfoHex"] = json!(hex(&b));
        }
        if let Err(e) = &cleanup {
            status["cleanupError"] = json!(format!("{e:#}"));
        }
        if let Err(e) = receiving {
            if e.is::<Cancelled>() {
                status["cancelledDuringStartup"] = json!(status["result"] != "receiving");
            } else if e
                .downcast_ref::<io::Error>()
                .is_some_and(|e| e.kind() == io::ErrorKind::BrokenPipe)
            {
                status["consumerClosed"] = json!(true);
            } else {
                return Err(e);
            }
        }
        cleanup
    })();
    match &outcome {
        Ok(()) => status["result"] = json!("stopped"),
        Err(e) => {
            status["result"] = json!("error");
            status["error"] = json!(format!("{e:#}"));
        }
    }
    let saved = save(&path, &status);
    eprintln!("{status}");
    outcome?;
    saved?;
    Ok(())
}
