"""Copy the canonical WebUI into PlatformIO's LittleFS staging directory."""

from pathlib import Path
import shutil

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.

project_dir = Path(env["PROJECT_DIR"])  # type: ignore[name-defined]
web_dir = project_dir / "web"
data_dir = project_dir / "data"

assets = {
    web_dir / "index.html": data_dir / "index.html",
    web_dir / "style.css": data_dir / "style.css",
    web_dir / "src" / "app.js": data_dir / "src" / "app.js",
}

for source, destination in assets.items():
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)

