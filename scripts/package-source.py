"""Create a reproducible source-only ZIP from Git's non-ignored file list."""
import hashlib
from pathlib import Path, PurePosixPath
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TOP_LEVEL = {".gitignore", ".gitattributes", "README.md"}
DIRECTORIES = {"bondriver", "linux", "nixos", "windows", "firmware", "scripts", "tests"}
EXTENSIONS = {".md", ".txt", ".c", ".cpp", ".h", ".s", ".ld", ".rs", ".py",
              ".ps1", ".ini", ".cfg", ".rules", ".yml", ".nix", ".toml", ".lock", ".json"}
EXECUTABLES = {"linux/torne-recv", "linux/torne-recv-watchdog"}
PRIVATE_PARTS = {"archive", "rel", "research", "diagnostics", "BCJB95006", "decrypted_modules",
                 "target", "build", "dist", ".tmp", ".git", "__pycache__", "node_modules"}


def validate_name(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise ValueError(f"Unsafe archive path: {name}")
    if name in TOP_LEVEL:
        return
    if (len(path.parts) < 2 or path.parts[0] not in DIRECTORIES
            or any(part in PRIVATE_PARTS for part in path.parts)
            or (path.suffix not in EXTENSIONS and name not in EXECUTABLES)):
        raise ValueError(f"Not a source distribution file: {name}")
    if path.suffix == ".json" and path.parent != PurePosixPath("firmware/patches"):
        raise ValueError(f"Only firmware patch JSON is distributable: {name}")


def source_files(root=ROOT):
    listing = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=root, check=True, stdout=subprocess.PIPE).stdout
    names = sorted(set(name.decode("utf-8") for name in listing.split(b"\0") if name))
    # Also exclude ignored files already tracked by an existing repository.
    ignored = subprocess.run(
        ["git", "check-ignore", "--no-index", "--stdin", "-z"], cwd=root,
        input=b"\0".join(name.encode("utf-8") for name in names) + b"\0",
        stdout=subprocess.PIPE)
    if ignored.returncode not in (0, 1):
        raise RuntimeError("git check-ignore failed")
    excluded = set(ignored.stdout.split(b"\0"))
    files = []
    for name in names:
        if name.encode("utf-8") in excluded:
            continue
        validate_name(name)
        path = root / name
        if not path.exists():
            continue  # staged/tracked deletion
        if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
            raise ValueError(f"Not a regular in-tree file: {name}")
        data = path.read_bytes()
        if b"\0" in data:
            raise ValueError(f"Unexpected binary file: {name}")
        data.decode("utf-8-sig")  # Reject binary or locale-specific encodings.
        files.append((name, data.replace(b"\r\n", b"\n")))
    if not files or not any(name == "README.md" for name, _ in files):
        raise ValueError("No source tree found")
    return files


def main():
    files = source_files()
    output = ROOT / "dist/tvtuner-source.zip"
    output.parent.mkdir(exist_ok=True)
    temporary = output.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in files:
            info = zipfile.ZipInfo("tvtuner-source/" + name, (2026, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (0o100755 if name in EXECUTABLES else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data, compresslevel=9)
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() is not None or len(archive.infolist()) != len(files):
            raise ValueError("ZIP integrity check failed")
    temporary.replace(output)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_suffix(".zip.sha256").write_text(f"{digest}  {output.name}\n", encoding="utf-8")
    print(f"{output}\n{len(files)} source files; {output.stat().st_size:,} bytes\nSHA-256: {digest}")


if __name__ == "__main__":
    main()
