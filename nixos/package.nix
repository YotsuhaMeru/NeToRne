{ lib, rustPlatform, pkg-config, libusb1 }:
rustPlatform.buildRustPackage {
  pname = "torne-recv";
  version = "0.1.0";
  # Only Rust sources enter the Nix store. Firmware/modules remain external.
  src = lib.fileset.toSource {
    root = ../linux/receiver;
    fileset = lib.fileset.unions [
      ../linux/receiver/Cargo.toml
      ../linux/receiver/Cargo.lock
      ../linux/receiver/src
    ];
  };
  cargoLock.lockFile = ../linux/receiver/Cargo.lock;
  nativeBuildInputs = [ pkg-config ];
  buildInputs = [ libusb1 ];
  meta = {
    description = "Native Sony torne CECH-ZD1J receiver for Mirakurun";
    mainProgram = "torne-recv";
    platforms = lib.platforms.linux;
  };
}
