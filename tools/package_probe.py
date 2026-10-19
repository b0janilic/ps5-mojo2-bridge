"""Package the compiled diagnostic with a manifest and matching source snapshot."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

root = Path(__file__).resolve().parent.parent
match = re.search(r'#define PROBE_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"',
                  (root / "payloads/mojo2-probe/main.c").read_text())
if not match:
    raise SystemExit("No valid payload version")
version = match.group(1)
dist = root / "dist"
dist.mkdir(exist_ok=True)
manifest_path = dist / "BUILD.json"
if manifest_path.exists():
    old = json.loads(manifest_path.read_text())
    if old["version"] != version:
        archive = dist / "archive"
        archive.mkdir(exist_ok=True)
        shutil.copyfile(dist / "mojo2-probe.elf", archive / f'mojo2-probe-{old["version"]}.elf')
        shutil.copyfile(manifest_path, archive / f'BUILD-{old["version"]}.json')
elf = dist / "mojo2-probe.elf"
shutil.copyfile(root / "build/mojo2-probe.elf", elf)
manifest = {
    "payload": "mojo2-probe", "version": version, "target_firmware": "5.50",
    "console_tested": False, "mode": "resident audio bridge with experimental HDMI mute, retry and relaunch-to-stop",
    "usb_output_exercised": True, "capture_probe_implemented": True,
    "game_audio_routing_implemented": True, "bridge_window_ms": None,
    "reconnect_retry_implemented": True, "reconnect_console_tested": False,
    "power_button_behavior_tested": False, "stop_method": "relaunch ELF or FTP stop marker",
    "digital_gain": 0.25, "resident": True, "hdmi_output_changed": "experimental mute while USB audio plays; unmute on session exit",
    "hdmi_console_tested": False, "hdmi_firmware_gate": "0x05500000/0xffff0000 (5.50)",
    "hdmi_native_menu_registration": False,
    "hdmi_backend": "existing AudioOut IPMI client; HDMI-only command 0x45/mask 0x10",
    "previous_hdmi_attempt": "0.12.0 direct /dev/az open rejected with EALREADY (37); audio stayed mirrored",
    "relaunch_stop_tested_previous_version": True,
    "hdmi_recovery": "persistent intent marker; unmute on next launch after interrupted run; no prior-mute getter or independent crash watchdog",
    "feedback_method": "USB_FS fractional packet clock with two OUT slots, backed by captured PCM",
    "sdk": "ps5-payload-dev/sdk v0.44",
    "sdk_sha256": "4d06eab777bb135705b5a6816688c9132efb246093fe606ac488ab6cf65ca5b0",
    "compiler": "LLVM 18.1.8", "elf_bytes": elf.stat().st_size,
    "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
    "hardware_baseline": "0.10.0 user confirmed perfect playback: 239936 OUT packets in 30 s, zero USB/capture errors or underruns, cleanup/restoration passed; 6876 ring frames discarded in total",
    "host_checks": ["-Wall -Wextra -Werror", "descriptor/rate parser ASan/UBSan",
                    "truncated inputs", "missing feedback", "zero-length descriptor", "20000 mutations",
                    "USB_FS lifecycle/dispatch/in-flight buffer fault injection ASan/UBSan",
                    "fractional packet clock conservation/bounds ASan/UBSan",
                    "tone duration/ramps/peak/stereo/S32_LE and silent tail ASan/UBSan",
                    "capture setup/read-validation/cleanup fault injection ASan/UBSan",
                    "PCM ring/gain/stereo/ramp/backlog/underrun and 30-second clock model ASan/UBSan",
                    "ring to USB_FS buffer integration ASan/UBSan",
                    "single-instance process lock/stop/process-exit recovery ASan/UBSan",
                    "capture restart without module reload ASan/UBSan",
                    "HDMI firmware gate/route cycles/preflight/partial-failure rollback/stale-marker recovery and journal failures ASan/UBSan",
                    "HDMI IPC binding/method/header/192-byte record/mute-unmute/status fault injection ASan/UBSan",
                    "clang static analyzer"],
}
# Validation applies only to the exact tested ELF, never to a rebuilt binary.
validation_path = root / "docs" / "validation" / f"{version}.json"
if validation_path.exists():
    validation = json.loads(validation_path.read_text())
    if validation.get("version") == version and validation.get("elf_sha256") == manifest["elf_sha256"]:
        allowed = {"console_tested", "hdmi_console_tested", "reconnect_console_tested",
                   "power_button_behavior_tested", "relaunch_stop_console_tested", "console_test_scope"}
        manifest.update({k: v for k, v in validation.get("manifest_updates", {}).items() if k in allowed})
        manifest["console_validation"] = str(validation_path.relative_to(root))
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
files = [root / name for name in ("README.md", "Makefile", "COPYING", "NOTICE.md", ".gitignore")]
for folder in ("payloads/mojo2-probe", "docs", "tests", "tools"):
    for path in (root / folder).rglob("*"):
        if not path.is_file() or "__pycache__" in path.parts:
            continue
        if path.name in ("audio_device_probe.py", "mbus_info.c"):
            continue
        if path.suffix not in (".c", ".h", ".py", ".md", ".json") and path.name != "Makefile":
            continue
        files.append(path)
manifest["source_sha256"] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                             for p in sorted(files) if p.parts[-3:-1] == ("payloads", "mojo2-probe")}
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
files.append(manifest_path)
archive = dist / f"mojo2-probe-{version}-source.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
    for path in sorted(files):
        z.write(path, Path(f"mojo2-probe-{version}") / path.relative_to(root))
with zipfile.ZipFile(archive) as z:
    if z.testzip() is not None:
        raise SystemExit("Source archive verification failed")
print(json.dumps(manifest, indent=2))
