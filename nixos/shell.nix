{ pkgs ? import <nixpkgs> { } }:
pkgs.mkShell {
  packages = [ pkgs.cargo pkgs.rustc pkgs.rustfmt pkgs.pkg-config pkgs.libusb1 ];
}
