"""Deploy our Mojo payload or fetch its report; never starts a payload."""
import argparse
from datetime import datetime, timezone
from ftplib import FTP, error_perm
import hashlib
import io
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
config_path = ROOT / ".ps5-local.json"
config = json.loads(config_path.read_text()) if config_path.exists() else {}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("command", choices=("deploy", "get-log", "status", "stop"))
parser.add_argument("--host", default=config.get("host"))
parser.add_argument("--port", type=int, default=config.get("port", 2121))
parser.add_argument("--remote-dir", default=config.get("remote_dir"))
args = parser.parse_args()
if not args.host:
    parser.error("Supply --host or configure host in .ps5-local.json")
if args.command in ("deploy", "status") and not args.remote_dir:
    parser.error("Supply --remote-dir or configure remote_dir in .ps5-local.json")
if args.remote_dir and (not args.remote_dir.startswith("/") or any(c in args.remote_dir for c in "\r\n")):
    parser.error("remote-dir must be an absolute FTP path without newlines")
REMOTE_DIR = args.remote_dir.rstrip("/") if args.remote_dir else None
with FTP() as ftp:
    ftp.connect(args.host, args.port, timeout=10)
    ftp.login()
    if args.command == "deploy":
        source = ROOT / "dist/mojo2-probe.elf"
        data = source.read_bytes()
        expected = json.loads((ROOT / "dist/BUILD.json").read_text())["elf_sha256"]
        if data[:4] != b"\x7fELF" or hashlib.sha256(data).hexdigest() != expected:
            raise SystemExit("Local ELF/manifest verification failed")
        target = REMOTE_DIR + "/mojo2-probe.elf"
        old = bytearray()
        try:
            ftp.retrbinary("RETR " + target, old.extend)
        except error_perm as exc:
            if not str(exc).startswith("550"):
                raise
        if old:
            backups = ROOT / "build/ftp-backups"
            backups.mkdir(parents=True, exist_ok=True)
            (backups / (hashlib.sha256(old).hexdigest() + ".elf")).write_bytes(old)
        temporary = REMOTE_DIR + "/mojo2-probe.uploading"
        ftp.storbinary("STOR " + temporary, io.BytesIO(data))
        received = bytearray()
        ftp.retrbinary("RETR " + temporary, received.extend)
        if hashlib.sha256(received).hexdigest() != expected:
            raise SystemExit("FTP readback hash mismatch; active payload not replaced")
        ftp.rename(temporary, target)
        final = bytearray()
        ftp.retrbinary("RETR " + target, final.extend)
        if hashlib.sha256(final).hexdigest() != expected:
            raise SystemExit("Final FTP readback hash mismatch")
        print(f"Deployed and verified {len(final)} bytes: {target}")
        print(f"SHA-256: {expected}; execution remains manual")
    elif args.command == "stop":
        ftp.storbinary("STOR /data/mojo2-stop", io.BytesIO(b"stop\n"))
        print("Stop requested; wait for the running payload to fade, clean up and report stopped.")
    elif args.command == "get-log":
        data = bytearray()
        ftp.retrbinary("RETR /data/mojo2-probe.txt", data.extend)
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        path = ROOT / "captures" / f"ps5-{stamp}-internal-mojo2-probe.txt"
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(data)
        print(f"Saved {len(data)} bytes: {path}")
        print(data.decode("utf-8", errors="replace"))
    else:
        print(ftp.getwelcome())
        ftp.retrlines("LIST " + REMOTE_DIR, lambda line: print(line) if "mojo" in line.lower() else None)
