//! Offline only: validates existing decrypted firmware, prints metadata only.
use std::{env, fs, process};
use torne_firmware_core::image::{self, Trc};
fn main() {
    let args: Vec<_> = env::args().collect();
    if args.len() != 3 || !matches!(args[1].as_str(), "idle" | "trc") {
        eprintln!("usage: inspect idle|trc <decrypted-plaintext.bin>");
        process::exit(2);
    }
    let bytes = fs::read(&args[2]).unwrap_or_else(|e| {
        eprintln!("{e}");
        process::exit(1)
    });
    let result = if args[1] == "idle" {
        image::idle(&bytes).map(|records| {
            for r in records {
                println!(
                    "idle load={:08x} bytes={} checksum=valid",
                    r.address,
                    r.payload.len()
                );
            }
        })
    } else {
        Trc::validate(&bytes).map(|images| {
            for i in images {
                for r in i.records {
                    println!(
                        "trc image={} mask={:08x} load={:08x} bytes={} checksum=valid",
                        i.index,
                        i.mask,
                        r.address,
                        r.payload.len()
                    );
                }
            }
        })
    };
    if let Err(e) = result {
        eprintln!("invalid image: {e:?}");
        process::exit(1);
    }
}
