"""Apply a hash-pinned XOR delta to a user-supplied firmware. No USB access."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PATCHES = ROOT / "firmware/patches"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def apply_delta(source, patch):
    if patch.get("format") != "torne-xor-v1":
        raise ValueError("Unsupported patch format")
    size = patch.get("size")
    if type(size) is not int or size <= 0 or len(source) != size:
        raise ValueError("Input size does not match patch")
    if sha256(source) != patch.get("input_sha256"):
        raise ValueError("Input SHA-256 does not match the supported original")
    segments = patch.get("segments")
    if not isinstance(segments, list) or not segments:
        raise ValueError("Patch has no segments")
    result = bytearray(source)
    previous_end = 0
    for segment in segments:
        offset = segment.get("offset")
        delta = bytes.fromhex(segment["xor"])
        if (type(offset) is not int or offset < previous_end or not delta
                or offset + len(delta) > size):
            raise ValueError("Invalid, overlapping or unordered patch segment")
        for index, value in enumerate(delta, offset):
            result[index] ^= value
        previous_end = offset + len(delta)
    if sha256(result) != patch.get("output_sha256"):
        raise ValueError("Output SHA-256 mismatch; no output was written")
    return bytes(result)


def write_result(path, data, source_path):
    if path.resolve() == source_path.resolve():
        raise ValueError("Output must differ from input")
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError("Output exists with different contents; refusing overwrite")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)


def main():
    variants = sorted(path.stem for path in PATCHES.glob("*.json"))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="List available variants")
    parser.add_argument("--variant", choices=variants)
    parser.add_argument("--input", type=Path,
                        default=ROOT / "BCJB95006/USRDIR/mb86h47_aac_trc.bin")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.list:
        print("\n".join(variants))
        return
    if not args.variant:
        parser.error("--variant is required (or use --list)")
    try:
        patch = json.loads((PATCHES / (args.variant + ".json")).read_text(encoding="utf-8"))
        filename = patch["output_name"]
        if Path(filename).name != filename or "/" in filename or "\\" in filename:
            raise ValueError("Patch output name must be a filename")
        output = args.output or ROOT / "firmware/build" / filename
        result = apply_delta(args.input.read_bytes(), patch)
        write_result(output, result, args.input)
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        parser.exit(1, f"Patch failed: {error}\n")
    print(f"Verified: {output}\nSHA-256: {sha256(result)}")


if __name__ == "__main__":
    main()
