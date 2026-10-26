"""Fetch a pinned local FR30 toolchain. Python 3.14+ (zstd tar support).

Offline execution of firmware is not part of setup. No registry installation,
machine PATH changes, USB access or Flash writes. Preserve upstream licences.
"""
import hashlib
from pathlib import Path
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / ".tmp/fr30"
ARCHIVE = "https://cygwin.com/ftp/cygwin-archive/20221123/"
PACKAGES = [
    ("https://www.maintech.de/fileadmin/Downloads/fr30-toolchain-20100305-cygwin.tbz2",
     "toolchain.tbz2", "sha256", "c4d7c5b7c984eebf6f7bd0b06752458d9aaf499e42cdfb60f3e1bfdc97cdc79e"),
    (ARCHIVE + "x86/release/cygwin/cygwin-3.3.6-1.tar.xz", "cygwin.tar.xz", "sha512",
     "9ac078b10c98783a0161217cc80cda26990def19b0564f45a21b3cfaba7040f681fb4756f6ef37abef693cc805eb9c6ac8eacb5726ba2cd446e697e2b056e049"),
    (ARCHIVE + "x86/release/gettext/libintl8/libintl8-0.21.1-2.tar.xz", "libintl8-0.21.1-2.tar.xz", "sha512",
     "1d711809f14cfa646459c94425161000289b2f0349d7dc7eaa8148a0b5a3e95d46aba3c34d42e64bb6950b5415320908d6a5665835514797ba17d798cce65dff"),
    (ARCHIVE + "x86/release/libiconv/libiconv2/libiconv2-1.17-1.tar.xz", "libiconv2-1.17-1.tar.xz", "sha512",
     "c3e13e84a04d4eb754074b6bd6f39cbc8ddb000e7941ee69fb4d468bb9b0dd97019d7f4f05d835266ff6cab1963078ff2d58f52f34dcfdea1e37fca9c7b20628"),
    (ARCHIVE + "x86/release/zlib/zlib0/zlib0-1.2.13-1.tar.zst", "zlib0-1.2.13-1.tar.zst", "sha512",
     "6b0f83d3f6661318fedbf131cd096114ac0a1a928ade47c77e95a35d889823c33ecb36fa1a134c38c8a7ba5462e77180524f2c45e3e716f078640f3d12de3c2b"),
    (ARCHIVE + "x86/release/gcc/libgcc1/libgcc1-11.3.0-1.tar.zst", "libgcc1-11.3.0-1.tar.zst", "sha512",
     "fa2edf846ed776463935bdd542445d952c5e950238e618634637257d06843cba10e1862a49212e6523333082362ccdc622aba0b4e6385e5bf95373709865e3c4"),
]

def confined(path):
    resolved = path.resolve()
    if not resolved.is_relative_to(DEST.resolve()):
        raise ValueError(f"Archive path leaves tool directory: {path}")
    return resolved

def main():
    DEST.mkdir(parents=True, exist_ok=True)
    archives = []
    for url, name, algorithm, expected in PACKAGES:
        path = DEST / name
        if not path.exists():
            # Only the pinned official distributor URLs above are fetched.
            with urllib.request.urlopen(url, timeout=60) as response:
                path.write_bytes(response.read())
        if hashlib.new(algorithm, path.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Checksum mismatch for {name}; refusing extraction")
        archives.append(path)
    tools_bin = DEST / "usr/local/fr30/bin"
    tools_bin.mkdir(parents=True, exist_ok=True)
    for index, path in enumerate(archives):
        with tarfile.open(path) as archive:
            # Copy regular files only; Windows symlinks are not required.
            for member in archive:
                if not member.isfile():
                    continue
                if index == 0:
                    target = confined(DEST / member.name)
                elif member.name.endswith(".dll"):
                    target = confined(tools_bin / Path(member.name).name)
                elif "/doc/" in member.name:
                    target = confined(DEST / "cygwin-licenses" / member.name)
                else:
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source, target.open("wb") as destination:
                    shutil.copyfileobj(source, destination)
            if index == 0:
                # Resolve the archive's verified hardlinks by copying bytes.
                for member in archive.getmembers():
                    if member.islnk():
                        target = confined(DEST / member.name)
                        with archive.extractfile(member) as source, target.open("wb") as destination:
                            shutil.copyfileobj(source, destination)
    print(f"FR30 GCC 3.3.6 / binutils 2.19 ready in {tools_bin.parent}")
    print("Run firmware/build-fr30.ps1. Generated core is not yet a loadable firmware.")

if __name__ == "__main__":
    main()
