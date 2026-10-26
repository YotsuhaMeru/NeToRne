{ config, lib, pkgs, ... }:
let
  cfg = config.services.torneReceiver;
  receiverPackage = cfg.package;
  receiverCommand = pkgs.writeShellScriptBin "torne-recv" ''
    exec ${receiverPackage}/bin/torne-recv \
      --root ${lib.escapeShellArg cfg.root} --state-dir /var/lib/torne-receiver \
      --operational-firmware ${lib.escapeShellArg cfg.operationalFirmware} "$@"
  '';
  receiverWatchdogCommand = pkgs.writeShellScriptBin "torne-recv-watchdog" ''
    set -u
    stopping=0
    child=
    stop() {
      stopping=1
      if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || true; fi
    }
    trap stop TERM INT
    attempt=0
    while [ "$stopping" -eq 0 ]; do
      ${receiverCommand}/bin/torne-recv --root ${lib.escapeShellArg cfg.root} \
        --state-dir /var/lib/torne-receiver \
        --operational-firmware ${lib.escapeShellArg cfg.operationalFirmware} "$@" &
      child=$!
      wait "$child"
      status=$?
      child=
      if [ "$stopping" -ne 0 ] || [ "$status" -eq 0 ]; then exit 0; fi
      attempt=$((attempt + 1))
      echo "torne watchdog: receiver exited with status $status; retry $attempt in 3s" >&2
      sleep 3 &
      child=$!
      wait "$child" || true
      child=
    done
    exit 0
  '';
in {
  options.services.torneReceiver = {
    enable = lib.mkEnableOption "CECH-ZD1J receiver for Mirakurun";
    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.callPackage ./package.nix { };
      description = "Native Rust receiver package.";
    };
    root = lib.mkOption {
      type = lib.types.str;
      default = "/opt/tvtuner";
      description = "Directory containing the user's local torne firmware/modules.";
    };
    operationalFirmware = lib.mkOption {
      type = lib.types.str;
      default = "BCJB95006/USRDIR/mb86h47_aac_trc.bin";
      description = "Known operational firmware file; relative paths use root. Set to firmware/build/mb86h47_aac_trc-psi-passthrough.bin for source PAT/PMT preservation.";
    };
    channels = lib.mkOption {
      type = lib.types.listOf (lib.types.submodule {
        options = {
          name = lib.mkOption { type = lib.types.str; };
          physical = lib.mkOption { type = lib.types.ints.between 13 52; };
        };
      });
      default = [
        { name = "NHK Eテレ・名古屋"; physical = 13; }
        { name = "三重テレビ"; physical = 14; }
        { name = "CCNet12"; physical = 15; }
        { name = "CCNet11"; physical = 16; }
        { name = "CBCテレビ"; physical = 18; }
        { name = "中京テレビ"; physical = 19; }
        { name = "NHK総合・名古屋"; physical = 20; }
        { name = "東海テレビ"; physical = 21; }
        { name = "メ〜テレ"; physical = 22; }
        { name = "テレビ愛知"; physical = 23; }
      ];
      description = "Physical channels; defaults reflect the actual antenna scan on 2026-10-11.";
    };
  };
  config = lib.mkIf cfg.enable {
    assertions = [{
      assertion = lib.hasPrefix "/" cfg.root;
      message = "services.torneReceiver.root must be an absolute Linux path";
    }];
    environment.systemPackages = [ receiverCommand receiverWatchdogCommand pkgs.usbutils ];
    services.udev.extraRules = builtins.readFile ../linux/torne-receiver.rules;
    services.mirakurun = {
      enable = true;
      openFirewall = lib.mkDefault false;
      allowSmartCardAccess = false;
      tunerSettings = [{
        name = "Sony torne CECH-ZD1J";
        types = [ "GR" ];
        command = "${receiverWatchdogCommand}/bin/torne-recv-watchdog --channel <channel>";
        isDisabled = false;
      }];
      channelSettings = map (ch: { name = ch.name; type = "GR"; channel = toString ch.physical; }) cfg.channels;
    };
    # The official Mirakurun service runs as mirakurun:video. Writable API
    # sequence and lock files must live outside the Nix store/project.
    systemd.services.mirakurun.serviceConfig.StateDirectory = lib.mkForce [ "mirakurun" "torne-receiver" ];
  };
}
