"""Fetch the pinned PS5 SDK into the user's cache; never replaces an existing SDK."""
import hashlib
from pathlib import Path
import tempfile
import urllib.request
import zipfile

URL = "https://github.com/ps5-payload-dev/sdk/releases/download/v0.44/ps5-payload-sdk.zip"
SHA256 = "4d06eab777bb135705b5a6816688c9132efb246093fe606ac488ab6cf65ca5b0"
destination = Path.home() / ".cache/ps5-mojo/sdk-v0.44"
if destination.exists():
    raise SystemExit(f"Already exists; unchanged: {destination}")
data = urllib.request.urlopen(URL, timeout=60).read()
if hashlib.sha256(data).hexdigest() != SHA256:
    raise SystemExit("SDK SHA-256 mismatch")
destination.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=destination.parent) as tmp:
    archive = Path(tmp) / "sdk.zip"
    archive.write_bytes(data)
    with zipfile.ZipFile(archive) as z:
        for name in z.namelist():
            if Path(name).is_absolute() or ".." in Path(name).parts:
                raise SystemExit("Invalid SDK archive path")
        z.extractall(tmp)
    sdk = Path(tmp) / "ps5-payload-sdk"
    for tool in (sdk / "bin").iterdir():
        if tool.is_file():
            tool.chmod(tool.stat().st_mode | 0o111)
    sdk.rename(destination)
print(f"Verified SDK v0.44 installed: {destination}")
