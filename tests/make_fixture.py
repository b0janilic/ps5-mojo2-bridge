"""Reconstruct a full configuration blob from the captured libusb descriptor tree."""
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
capture = json.loads((root / "tests/fixtures/mojo2-usb-descriptors.json").read_text())
cfg = capture["configurations"][0]
total = cfg["total_length"]
blob = bytes([
    9, 2, total & 255, total >> 8,
    len({i["number"] for i in cfg["interfaces"]}), cfg["value"], 0,
    cfg["attributes"], cfg["max_power"],
]) + bytes.fromhex(cfg["extra_hex"])
for iface in cfg["interfaces"]:
    blob += bytes([
        9, 4, iface["number"], iface["alt"], len(iface["endpoints"]),
        iface["class"], iface["subclass"], iface["protocol"], 0,
    ]) + bytes.fromhex(iface["extra_hex"])
    for ep in iface["endpoints"]:
        blob += bytes([
            7, 5, ep["address"], ep["attributes"], ep["max_packet_size"] & 255,
            ep["max_packet_size"] >> 8, ep["interval"],
        ]) + bytes.fromhex(ep["extra_hex"])
assert len(blob) == total == 176
path = root / "build/mojo2-config1-reconstructed.bin"
path.parent.mkdir(exist_ok=True)
path.write_bytes(blob)
print(f"Fixture: {len(blob)} bytes, reconstructed from the Mac capture")
