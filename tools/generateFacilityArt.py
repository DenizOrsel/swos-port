#!/usr/bin/env python3
"""Generate 1996/97 SWOS pixel-art facility thumbnails using the PixelLab API.

Reads PIXELLAB_API_KEY from .env.local (gitignored) or the environment.
Outputs 25 crisp 160x92 PNGs into assets/facilities/.
"""

import base64
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "assets" / "facilities"
STYLE_SUFFIX = (
    ", 1996 retro 16-bit Amiga pixel art game icon, crisp pixels, rich colors, "
    "Sensible World of Soccer aesthetic, clean composition, no text, no watermark"
)

PROMPTS: list[tuple[str, str]] = [
    # Stadium Capacity (5 visual tiers covering Lvl 1..10)
    ("capacity_1", "Small non-league British football ground with a tiny grassy bank and wooden bleacher stand, side view"),
    ("capacity_2", "Lower-league English football stadium with a single covered concrete terrace stand and two small floodlight poles"),
    ("capacity_3", "Classic 1990s two-tier English football stadium stand with blue canopy roof, advertising boards, and floodlights"),
    ("capacity_4", "Large modern double-decker European football stadium bowl with roof trusses, bright floodlights, and packed colorful crowd"),
    ("capacity_5", "Iconic 50000-seat European fortress mega-stadium at night with four blazing floodlight towers, sweeping roof, golden lights, and roaring packed stands"),

    # Pitch Quality (Lvl 1..5)
    ("pitch_1", "Top-down pixel art football pitch, muddy brown worn grass field with water puddles and faded chalk lines"),
    ("pitch_2", "Top-down pixel art football pitch, basic uneven green grass field with simple white touchlines and center circle"),
    ("pitch_3", "Top-down pixel art football pitch, neat two-tone mowed green grass stripes with crisp white lines"),
    ("pitch_4", "Top-down pixel art football pitch, lush checkered emerald turf with bright penalty boxes and watering sprinklers"),
    ("pitch_5", "Top-down pixel art football pitch, immaculate Wembley championship velvet emerald grass with crisp white geometry and subtle golden sparkle"),

    # Training Ground (Lvl 1..5)
    ("training_1", "Small muddy football training patch with orange training cones and a simple wooden goalpost"),
    ("training_2", "Grass football practice pitch with orange cones and a small wooden portakabin changing shed"),
    ("training_3", "Classic red brick football training clubhouse with pitched roof, green practice pitch, and training dummies"),
    ("training_4", "Modern football academy complex with glass gym windows, twin green training pitches, and floodlight poles"),
    ("training_5", "World-class football sports-science campus with a white indoor training dome, glass academy tower, and floodlit pitches"),

    # Medical Center (Lvl 1..5: tent -> hospital)
    ("medical_1", "Small white canvas pitchside first-aid medical tent on grass with a bright red cross emblem"),
    ("medical_2", "Small brick football physio cabin with a red cross sign and a white medical van parked outside"),
    ("medical_3", "Two-story brick and white sports medical clinic building with blue windows and a red cross roof sign"),
    ("medical_4", "Modern multi-story sports rehabilitation center with glass hydrotherapy wing, red cross emblem, and sleek facade"),
    ("medical_5", "Grand state-of-the-art sports hospital building with glowing red cross tower, rooftop helipad, glass atrium, and parked ambulance"),

    # Commercial Wing (Lvl 1..5: stall -> VIP plaza)
    ("commercial_1", "Tiny wooden football matchday burger and scarf stall with a red and white striped awning"),
    ("commercial_2", "Cozy red brick football club shop with a football jersey displayed in the front window"),
    ("commercial_3", "Busy 1990s football club superstore with a bright sponsor billboard and supporters outside"),
    ("commercial_4", "Sleek glass corporate VIP stadium hospitality wing with glowing neon sponsor signage and golden trim"),
    ("commercial_5", "Grand luxury football commercial plaza with golden VIP executive suites, giant glowing stadium screen, and illuminated entrance"),
]


def load_api_key() -> str:
    key = os.environ.get("PIXELLAB_API_KEY", "").strip()
    if key:
        return key
    for name in (".env.local", "env.local"):
        p = ROOT / name
        if p.is_file():
            for line in p.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if line.startswith("PIXELLAB_API_KEY="):
                    return line.split("=", 1)[1].strip().strip('"').strip("'")
    raise RuntimeError("PIXELLAB_API_KEY not found in environment or .env.local")


def extract_png_bytes(resp_json: dict) -> bytes:
    # Handle PixelLab v1/v2 image response formats
    img = resp_json.get("image")
    if isinstance(img, dict) and "base64" in img:
        b64 = img["base64"]
        if "," in b64:
            b64 = b64.split(",", 1)[1]
        return base64.b64decode(b64)
    if isinstance(img, str):
        b64 = img.split(",", 1)[1] if "," in img else img
        return base64.b64decode(b64)
    if "data" in resp_json and isinstance(resp_json["data"], dict):
        return extract_png_bytes(resp_json["data"])
    raise ValueError(f"Unrecognized PixelLab response keys: {list(resp_json.keys())}")


def call_pixellab(api_key: str, description: str, width: int = 160, height: int = 92) -> bytes:
    endpoints = [
        ("https://api.pixellab.ai/v2/create-image-pixen", {
            "description": description,
            "image_size": {"width": width, "height": height},
            "no_background": False,
        }),
        ("https://api.pixellab.ai/v1/generate-image-pixflux", {
            "description": description,
            "image_size": {"width": width, "height": height},
            "no_background": False,
        }),
    ]
    last_err = None
    for url, payload in endpoints:
        req = urllib.request.Request(
            url,
            data=json.dumps(payload).encode("utf-8"),
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
                "User-Agent": "swos-port-art-gen/1.0",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=90) as resp:
                raw = resp.read().decode("utf-8")
                data = json.loads(raw)
                return extract_png_bytes(data)
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")[:300]
            last_err = f"{url} -> HTTP {e.code}: {body}"
            if e.code in (401, 402, 403):
                raise RuntimeError(last_err)
        except Exception as e:
            last_err = f"{url} -> {e}"
    raise RuntimeError(f"PixelLab request failed: {last_err}")


def main() -> int:
    api_key = load_api_key()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    only = set(sys.argv[1:])

    for name, prompt in PROMPTS:
        if only and name not in only:
            continue
        out_path = OUT_DIR / f"{name}.png"
        if out_path.is_file() and "--force" not in only:
            print(f"[SKIP] {name}.png already exists")
            continue
        print(f"[GEN ] {name}.png ...", flush=True)
        png = call_pixellab(api_key, prompt + STYLE_SUFFIX, width=160, height=92)
        out_path.write_bytes(png)
        print(f"[ OK ] {name}.png ({len(png)} bytes)", flush=True)
        time.sleep(0.3)

    return 0


if __name__ == "__main__":
    sys.exit(main())
