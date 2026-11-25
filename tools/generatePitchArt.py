#!/usr/bin/env python3
"""Generate 10 consistent 2D isometric stadium football pitch sprites via PixelLab API.

Reads PIXELLAB_API_KEY from .env.local (gitignored) or environment.
Saves clean transparent 128x128 RGBA PNGs into assets/facilities/pitch_1.png .. pitch_10.png.

Usage:
    python tools/generatePitchArt.py                     # generate all 10
    python tools/generatePitchArt.py pitch_4 pitch_9     # regenerate specific sprite(s)
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
    "Isometric 2:1 sports management game sprite of a standalone rectangular football soccer pitch playing field slab, "
    "fixed three-quarter top-down view, centered isometric rectangular field slab with goalposts at both ends. "
    "Crisp 16-bit pixel art, clean 1px black outlines, flat cel-shading lit from upper-left, "
    "no anti-aliasing, no blur, no floor shadow, no outer ground grid, no players, no spectators, no readable text. "
)

NEGATIVE_DESC = (
    "stadium grandstands, bleacher seats, roof dome, stone ruins, dungeon, medieval, fantasy, rpg, "
    "trees, buildings, floor grid, tile grid, magenta background, blur, gradient, anti-aliasing, "
    "readable text, letters, players, people, characters, crowd, shadow, drop shadow, UI, border"
)

LEVELS: list[tuple[str, str]] = [
    (
        "pitch_1",
        "Compact centered patchy dirt and mud sandlot football pitch slab with generous empty transparent margin around all four corners (entire rectangular slab and both goals fully visible inside frame, never cropped): "
        "rough brown packed earth with sparse patchy tufts of dry yellowish-green weeds, "
        "faint uneven white chalk boundary lines, brown muddy puddle ruts near the center, "
        "and two small crooked rusty metal pipe goalposts with tattered sagging nets at the bottom-left and top-right ends.",
    ),
    (
        "pitch_2",
        "Basic community meadow grass football pitch slab: "
        "continuous natural medium-green grass lawn with slightly uneven mowing texture, "
        "crisp white painted touchlines, penalty boxes, and center circle, "
        "simple white painted wooden goal frames with intact white nets at both ends, and tiny red corner flags.",
    ),
    (
        "pitch_3",
        "Manicured striped club turf football pitch slab: "
        "lush vibrant emerald-green grass mowed in alternating light-green and dark-green horizontal stripes, "
        "crisp regulation white pitch markings, white aluminum box-net goals at both ends, "
        "a narrow dark-green turf perimeter border, and two compact silver metal team dugouts along the near touchline.",
    ),
    (
        "pitch_4",
        "Pro stadium heated checkerboard lawn football pitch slab: "
        "rich velvety checkerboard-mowed emerald stadium grass with crisp white markings, "
        "surrounded by a clean royal-blue synthetic running-track apron and low perimeter sponsor boards, "
        "sleek cantilevered pro white goals, recessed glass-covered team benches, and four slim corner pole floodlights.",
    ),
    (
        "pitch_5",
        "Championship hybrid grass-synthetic football pitch slab: "
        "pristine championship-grade bi-tone striped green hybrid turf with crisp white pitch markings, "
        "dark charcoal perimeter apron lined with glowing cyan-and-gold digital LED perimeter ad boards, "
        "sunken luxury glass dugout shelters along the touchline, and subtle pop-up sprinkler nozzles along the edge.",
    ),
    (
        "pitch_6",
        "All-weather reinforced arena turf football pitch slab: "
        "immaculate vivid green arena turf with laser-straight glowing white pitch lines, "
        "surrounded by a sleek multi-tiered dark metallic and glass perimeter curb with continuous glowing cyan and gold LED ribbon boards, "
        "aerodynamic tensioned white goal nets with camera pods, and recessed climate-control vents along the border.",
    ),
    (
        "pitch_7",
        "Smart-sensor telemetry football pitch slab: "
        "vivid emerald turf with subtle glowing cyan optical tracking sensor nodes embedded at the corners and penalty spots, "
        "brushed chrome and dark smart-glass perimeter containment wall with small holographic tactical replay projectors, "
        "compact robotic ball-retriever docks on the sidelines, and sleek silver corner laser-calibration pylons.",
    ),
    (
        "pitch_8",
        "Bio-engineered luminescent nano-turf football pitch slab: "
        "ultra-dense deep emerald and teal bio-engineered nano-turf emitting a crisp glowing cyan bioluminescent light along all pitch lines and the center circle, "
        "framed by sleek white-and-titanium armored perimeter walls, glowing cyan energy-field goal frames, "
        "and small hovering sideline camera drones.",
    ),
    (
        "pitch_9",
        "Sci-fi anti-gravity hover-field football pitch slab: "
        "elevated emerald-and-cyan playing field slab hovering clearly on glowing blue plasma anti-gravity thrusters beneath a beveled titanium perimeter frame, "
        "low translucent hexagonal cyan energy containment barriers along the touchlines, glowing plasma-arc goalposts, "
        "and four tall glowing cyan corner energy-spire floodlights.",
    ),
    (
        "pitch_10",
        "Apex galactic coliseum arena football pitch slab: "
        "radiant cosmic-emerald energy turf interwoven with glowing gold and royal-purple geometric light conduits, "
        "ornate white-and-gold cybernetic perimeter architecture around the rectangular field, crystalline golden-cyan energy-beam goal frames, "
        "a hovering glowing holographic golden championship crest projected above the center circle, "
        "and four monumental crowned white-and-gold light beacons at the corners.",
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


def generate_level(api_key: str, prompt: str, use_pixen: bool = False) -> bytes:
    full_desc = GLOBAL_RULES + prompt
    pixflux_req = (
        "https://api.pixellab.ai/v1/generate-image-pixflux",
        {
            "description": full_desc,
            "negative_description": NEGATIVE_DESC,
            "image_size": {"width": IMG_SIZE, "height": IMG_SIZE},
            "view": "high top-down",
            "direction": "south-east",
            "outline": "single color black outline",
            "shading": "basic shading",
            "detail": "high detail",
            "no_background": True,
        },
    )
    pixen_req = (
        "https://api.pixellab.ai/v2/create-image-pixen",
        {
            "description": full_desc,
            "image_size": {"width": IMG_SIZE, "height": IMG_SIZE},
            "no_background": True,
        },
    )

    candidates = [pixen_req, pixflux_req] if use_pixen else [pixflux_req, pixen_req]

    last_err = None
    for url, payload in candidates:
        req = urllib.request.Request(
            url,
            data=json.dumps(payload).encode("utf-8"),
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
                "User-Agent": "swos-port-pitch-gen/1.0",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                return extract_png_bytes(data)
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", errors="replace")[:350]
            last_err = f"{url} -> HTTP {e.code}: {body}"
            if e.code in (401, 402, 403):
                raise RuntimeError(last_err)
        except Exception as e:
            last_err = f"{url} -> {e}"
    raise RuntimeError(f"PixelLab pitch generation failed: {last_err}")


def main() -> int:
    api_key = load_api_key()
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    use_pixen = "--pixen" in sys.argv[1:]
    wanted = set(args) if args else None

    for name, prompt in LEVELS:
        if wanted is not None and name not in wanted:
            continue
        engine = "pixen-v2" if use_pixen else "pixflux-v1"
        print(f"[GEN ] {name}.png ({engine}, no_background=True) ...", flush=True)
        png_bytes = generate_level(api_key, prompt, use_pixen=use_pixen)
        out_path = OUT_DIR / f"{name}.png"
        out_path.write_bytes(png_bytes)
        print(f"[ OK ] {name}.png ({len(png_bytes)} bytes)", flush=True)
        time.sleep(0.4)

    return 0


if __name__ == "__main__":
    sys.exit(main())
