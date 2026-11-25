#!/usr/bin/env python3
"""Generate 10 consistent 2D isometric stadium merchandise/commercial sprites via PixelLab API.

Reads PIXELLAB_API_KEY from .env.local (gitignored) or environment.
Saves clean transparent 128x128 RGBA PNGs into assets/facilities/commercial_1.png .. commercial_10.png.

Usage:
    python tools/generateCommercialArt.py                           # generate all 10
    python tools/generateCommercialArt.py commercial_4 commercial_9 # regenerate specific sprite(s)
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
    "Isometric 2:1 sports management game sprite of a football stadium merchandise shop facility, "
    "fixed three-quarter top-down view facing front-left, centered on a compact square grey concrete slab base. "
    "Crisp 16-bit pixel art, clean 1px black outlines, flat cel-shading lit from upper-left, "
    "no anti-aliasing, no blur, no floor shadow, no ground grid, no readable text, no people, no NPCs. "
)

NEGATIVE_DESC = (
    "stone ruins, dungeon, moss, vines, grass, green field, football pitch, soccer field, medieval, fantasy, rpg, "
    "rocks, boulders, dirt, trees, floor grid, tile grid, magenta background, blur, gradient, anti-aliasing, "
    "readable text, letters, people, characters, crowd, shadow, drop shadow, UI, border"
)

LEVELS: list[tuple[str, str]] = [
    (
        "commercial_1",
        "Rudimentary outdoor stadium merchandise stall on a weathered grey concrete slab base facing front-left: "
        "two battered folding trestle tables covered with a faded royal-blue cloth, stacked brown cardboard boxes underneath, "
        "a portable silver metal clothing rack holding several hanging team jerseys, and a small painted wooden A-frame sidewalk sign.",
    ),
    (
        "commercial_2",
        "Pop-up canvas canopy stadium merchandise tent on a grey concrete slab base facing front-left: "
        "a striped royal-blue and white canvas gazebo pop-up tent supported by thin steel corner poles with a hanging fabric valance, "
        "sturdy wooden display counters underneath holding neatly folded team scarves, caps, and foam fingers.",
    ),
    (
        "commercial_3",
        "Permanent wooden stadium merchandise kiosk booth on a grey concrete slab base facing front-left: "
        "painted blue-and-white timber and corrugated-metal concession booth with a pitched shingle roof, "
        "a wide open roll-up service counter window revealing wall-mounted team jersey display racks inside, "
        "and two small triangular pennant flags on the roof corners.",
    ),
    (
        "commercial_4",
        "Solid red-brick and steel stadium corner fan shop on a grey concrete slab base facing front-left: "
        "large clear glass storefront windows displaying mannequins wearing team jerseys, "
        "a recessed sliding glass entrance door, a royal-blue and white striped fabric storefront awning, "
        "and illuminated glass display cases along the front brick wall.",
    ),
    (
        "commercial_5",
        "Sleek modern football club apparel store on a grey concrete slab base facing front-left: "
        "dark charcoal steel architectural cladding, floor-to-ceiling tempered glass storefront walls revealing brightly backlit multi-tier jersey walls inside, "
        "a cantilevered silver metal entrance canopy, and a glowing gold-and-blue pixel-art club crest emblem mounted above the doorway.",
    ),
    (
        "commercial_6",
        "Two-story official football team flagship megastore on a grey concrete slab base facing front-left: "
        "ground floor with triple glass entrance bays and exterior merchandise display kiosks, "
        "second floor with panoramic wrap-around glass windows showcasing hanging championship banners inside, "
        "exterior steel balcony trim, and a rooftop HVAC unit with bright architectural spotlights.",
    ),
    (
        "commercial_7",
        "Glass atrium football flagship superstore with LED ticker on a grey concrete slab base facing front-left: "
        "two-story modern retail building topped with a soaring angled glass-and-chrome atrium skylight roof, "
        "a wrap-around glowing cyan-and-gold LED ticker ribbon board running along the second-floor fascia, "
        "sleek interactive touchscreen kiosk pods flanking the front entrance, and bright architectural floodlights.",
    ),
    (
        "commercial_8",
        "High-tech near-future automated smart-retail fan gear hub on a grey concrete slab base facing front-left: "
        "sleek brushed silver-titanium and dark smart-glass panels with glowing neon cyan edge lighting, "
        "transparent pneumatic merchandise delivery tubes visible behind the glass, "
        "glowing cyan holographic jersey preview pedestals out front, and sleek automated pickup lockers along the side wall.",
    ),
    (
        "commercial_9",
        "Sci-fi anti-gravity drone-dispatch stadium fan armory on a grey concrete slab base facing front-left: "
        "sleek ground-floor smart-glass retail hub with translucent cyan energy awnings, "
        "an elevated floating anti-gravity glass rotunda showroom hovering clearly above the ground floor on glowing blue plasma thruster nodes, "
        "a rooftop automated delivery-drone launch pad with docked cargo drones, and twin vertical glowing cyan neon light pylons.",
    ),
    (
        "commercial_10",
        "Apex galactic stadium fan vault and hologram citadel on a gilded square base facing front-left: "
        "ornate white-and-gold cybernetic architecture fused with glowing royal-purple and cyan energy conduits, "
        "tiered floating glass showcase vaults displaying gleaming golden trophies and relics, "
        "a monumental crowned white-and-gold spire roof projecting a giant hovering glowing holographic golden team crest orb, "
        "and radiant cyan-and-gold energy-beam beacons framing the four corners of the base.",
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
                "User-Agent": "swos-port-commercial-gen/1.0",
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
    raise RuntimeError(f"PixelLab commercial generation failed: {last_err}")


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
