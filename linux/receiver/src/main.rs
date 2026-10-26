#![cfg_attr(not(unix), allow(dead_code))]
mod assets;
mod crypto;
mod media;
mod output;
mod protocol;
#[cfg(unix)]
mod runtime;
mod tuning;
#[cfg(unix)]
mod usb;

use clap::Parser;
use std::path::PathBuf;

fn default_root() -> PathBuf {
    std::env::var_os("TORNE_ROOT")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."))
}
fn default_state() -> PathBuf {
    std::env::var_os("XDG_STATE_HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            std::env::var_os("HOME")
                .map(PathBuf::from)
                .unwrap_or_else(|| PathBuf::from("."))
                .join(".local/state")
        })
        .join("torne")
}
#[derive(Parser, Debug)]
#[command(
    version,
    about = "CECH-ZD1J receiver: UHF 13..52 -> decrypted 188-byte TS on stdout"
)]
pub struct Args {
    #[arg(long)]
    channel: u32,
    #[arg(long,default_value_os_t=default_root())]
    root: PathBuf,
    #[arg(long, default_value = "BCJB95006/USRDIR/mb86h47_aac_trc.bin")]
    operational_firmware: PathBuf,
    #[arg(long,default_value_os_t=default_state())]
    state_dir: PathBuf,
    /// 0: receive until SIGTERM or closed stdout
    #[arg(long, default_value_t = 0.0)]
    seconds: f64,
    /// Override the saved USB API sequence (modulo 64)
    #[arg(long)]
    sequence: Option<u64>,
    #[arg(long,value_parser=["auto","libusb"],default_value="auto")]
    backend: String,
    #[arg(long)]
    stop_file: Option<PathBuf>,
    /// Validate local files and libusb without opening USB
    #[arg(long)]
    check: bool,
}
impl Args {
    fn validate(&self) -> anyhow::Result<()> {
        anyhow::ensure!(
            (13..=52).contains(&self.channel) && self.seconds.is_finite() && self.seconds >= 0.0,
            "Choose channel 13..52 and a finite nonnegative duration"
        );
        Ok(())
    }
}
fn main() -> std::process::ExitCode {
    let args = Args::parse();
    let result = args.validate().and_then(|()| {
        #[cfg(unix)]
        {
            runtime::run(args)
        }
        #[cfg(not(unix))]
        {
            let _ = args;
            anyhow::bail!(
                "This receiver uses the Linux/libusb backend; use BonDriver_Torne on Windows"
            )
        }
    });
    match result {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("torne: {e:#}");
            std::process::ExitCode::FAILURE
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn validate_cli() {
        for (channel, seconds) in [
            ("12", "0"),
            ("53", "0"),
            ("20", "NaN"),
            ("20", "inf"),
            ("20", "-1"),
        ] {
            let parsed = Args::try_parse_from([
                "torne-recv",
                "--channel",
                channel,
                &format!("--seconds={seconds}"),
            ]);
            assert!(parsed.map(|a| a.validate().is_err()).unwrap_or(true));
        }
        assert!(
            Args::try_parse_from(["torne-recv", "--channel", "20", "--check"])
                .unwrap()
                .validate()
                .is_ok()
        );
    }
}
