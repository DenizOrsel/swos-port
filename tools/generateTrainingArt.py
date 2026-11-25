#!/usr/bin/env python3
"""Generate 10 consistent 2D isometric stadium training ground sprites via PixelLab v2 Pixen API.

Reads PIXELLAB_API_KEY from .env.local (gitignored) or environment.
Saves clean transparent 128x128 RGBA PNGs into assets/facilities/training_1.png .. training_10.png.

Usage:
    python tools/generateTrainingArt.py                          # generate all 10
    python tools/generateTrainingArt.py training_4 training_9    # regenerate specific sprite(s)
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
IMG_SIZE = 128

GLOBAL_RULES = (
    "Isometric 2:1 sports management game sprite of a football stadium athletic training ground facility, "
    "fixed three-quarter top-down view facing front-left, centered on a compact square 4x4 grey concrete slab base "
    "with generous transparent margin around all four sides so tall netting cages, domes, and spires never clip. "
    "Crisp 16-bit pixel art, clean 1px black outlines, flat cel-shading lit from upper-left, "
    "no anti-aliasing, no blur, no floor shadow, no outer ground grid, no athletes, no people, no readable text. "
)

LEVELS: list[tuple[str, str]] = [
    (
        "training_1",
        "Rudimentary outdoor dirt football training patch on a weathered square grey concrete border and packed brown dirt ground facing front-left: "
        "scattered bright orange plastic agility cones, a flat yellow-and-black rope agility ladder laid on the dirt, "
        "two worn wooden hurdle bars, a battered mesh soccer ball bag, and a rusty chain-link backstop fence along the rear corner.",
    ),
    (
        "training_2",
        "Grass football drill yard and wooden rebound wall on a square grey concrete base facing front-left: "
        "natural medium-green training grass enclosed by a low timber perimeter curb, "
        "a painted white-and-blue wooden ball-rebound target board along the back edge, "
        "three bright yellow free-kick mannequin silhouettes on spring bases, and a neat metal rack of black-and-white soccer balls.",
    ),
    (
        "training_3",
        "Artificial turf football training cage and floodlit drill box on a square grey concrete base facing front-left: "
        "vibrant striped green synthetic turf enclosed on the back and side edges by tall black steel-mesh ball-stop netting, "
        "a mini pop-up white target goal, yellow slalom speed poles, black plyometric jump boxes, and twin slim corner pole floodlights.",
    ),
    (
        "training_4",
        "Covered conditioning pavilion and turf sprint track on a square grey concrete base facing front-left: "
        "half of the 4x4 footprint is a green striped sprint-track strip, alongside an open-sided red-brick and steel conditioning pavilion with a pitched metal roof, "
        "revealing squat racks, barbell weight plates, black battle ropes, and a glowing red digital interval timer board mounted on a beam.",
    ),
    (
        "training_5",
        "Modern pro football performance lab and sled track on a square grey concrete base facing front-left: "
        "one half enclosed in sleek dark charcoal steel and floor-to-ceiling tempered glass walls revealing treadmills, stationary bikes, and free-weight stations inside; "
        "the other half is bi-tone striped hybrid turf with a weighted metal prowler sprint sled and an automated yellow pneumatic ball-launcher machine.",
    ),
    (
        "training_6",
        "Two-story pro football academy training complex on a square grey concrete base facing front-left: "
        "two-story dark-steel and panoramic glass sports academy building on the rear half with an exterior steel observation balcony overlooking the front striped turf drill zone, "
        "rooftop silver climate-control HVAC units, and bright white high-intensity LED floodlight bars.",
    ),
    (
        "training_7",
        "Biomechanical motion-capture and video-wall football training arena on a square grey concrete base facing front-left: "
        "two-story chrome-and-glass performance building with a large glowing cyan-and-gold tactical analytics display screen on the facade, "
        "an overhead steel truss rig spanning the front turf drill zone holding optical motion-capture tracking cameras, "
        "and compact robotic multi-angle ball-cannon turrets on the sidelines.",
    ),
    (
        "training_8",
        "High-tech near-future cybernetic VR and holographic football drill simulator on a square grey concrete base facing front-left: "
        "sleek brushed silver-titanium and dark smart-glass building with glowing neon cyan LED edge trim, "
        "dark synthetic turf embedded with glowing cyan hexagonal reaction-light tiles, "
        "translucent cyan holographic defender silhouettes standing on the drill turf, and small hovering automated drone ball-feeders.",
    ),
    (
        "training_9",
        "Sci-fi anti-gravity zero-G athletic conditioning chamber on a square grey concrete base facing front-left: "
        "lower glowing cyan high-intensity agility grid turf enclosed by translucent hexagonal cyan energy barriers, "
        "crowned by a detached floating anti-gravity glass cylinder performance chamber hovering clearly in mid-air above the turf on glowing blue plasma thruster nodes, "
        "and twin vertical glowing cyan plasma light spires on the rear corners.",
    ),
    (
        "training_10",
        "Apex galactic athletic forge training citadel on a gilded square base facing front-left: "
        "ornate white-and-gold cybernetic architecture fused with glowing royal-purple and cyan energy conduits, "
        "tiered floating golden kinetic-training rings hovering around a central training deck, "
        "a monumental crowned white-and-gold spire roof projecting a giant hovering glowing holographic golden-cyan athlete-telemetry orb, "
        "and radiant cyan-and-gold energy-beam pylons framing the four corners of the base.",
    ),
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


def generate_level(api_key: str, prompt: str) -> bytes:
    url = "https://api.pixellab.ai/v2/create-image-pixen"
    payload = {
        "description": GLOBAL_RULES + prompt,
        "image_size": {"width": IMG_SIZE, "height": IMG_SIZE},
        "no_background": True,
    }
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "User-Agent": "swos-port-training-gen/1.0",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            return extract_png_bytes(data)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")[:350]
        raise RuntimeError(f"{url} -> HTTP {e.code}: {body}")


def main() -> int:
    api_key = load_api_key()
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    wanted = set(args) if args else None

    for name, prompt in LEVELS:
        if wanted is not None and name not in wanted:
            continue
        print(f"[GEN ] {name}.png (pixen-v2, no_background=True) ...", flush=True)
        png_bytes = generate_level(api_key, prompt)
        out_path = OUT_DIR / f"{name}.png"
        out_path.write_bytes(png_bytes)
        print(f"[ OK ] {name}.png ({len(png_bytes)} bytes)", flush=True)
        time.sleep(0.4)

    return 0


if __name__ == "__main__":
    sys.exit(main())
