#!/usr/bin/env python3
"""Generate 10 consistent 2D isometric stadium seating sprites via PixelLab API.

Reads PIXELLAB_API_KEY from .env.local (gitignored) or environment.
Saves clean transparent 128x128 RGBA PNGs into assets/facilities/capacity_1.png .. capacity_10.png.

Usage:
    python tools/generateStadiumCapacityArt.py                                          # regenerate all 10
    python tools/generateStadiumCapacityArt.py capacity_6 capacity_7 capacity_8 capacity_9 capacity_10
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
    "Isometric 2:1 sports management game sprite of a modern football stadium grandstand seating section, "
    "fixed three-quarter top-down view facing front-left, centered on a compact square grey concrete base. "
    "Crisp 16-bit pixel art, clean 1px black outlines, flat cel-shading lit from upper-left, "
    "no anti-aliasing, no blur, no floor shadow, no ground grid, no text, no spectators. "
)

NEGATIVE_DESC = (
    "stone ruins, dungeon, moss, vines, grass, green field, football pitch, soccer field, turf, medieval, fantasy, rpg, crates, rocks, boulders, "
    "dirt, plants, trees, floor grid, tile grid, magenta background, blur, gradient, anti-aliasing, "
    "text, letters, spectators, people, crowd, shadow, drop shadow, UI, border"
)

LEVELS: list[tuple[str, str]] = [
    (
        "capacity_1",
        "Basic wooden football bleachers on a grey concrete slab base: "
        "three stepped rows of brown wooden plank benches on dark iron brackets, "
        "with simple silver metal pipe railings along the left and right sides.",
    ),
    (
        "capacity_2",
        "Aluminum football stadium bleachers on a grey concrete base: "
        "four stepped rows of bright silver brushed aluminum metal bench seats with metal footboards and risers, "
        "sturdy steel handrails on both sides, clean municipal stadium stand.",
    ),
    (
        "capacity_3",
        "Football stadium grandstand with molded plastic bucket seats on a concrete base: "
        "five tiered rows of bright royal-blue individual plastic stadium seats with backrests, "
        "a grey concrete center aisle staircase with bright yellow step edges, steel side railings, "
        "and a low concrete front barrier wall.",
    ),
    (
        "capacity_4",
        "Covered club-level football stadium grandstand on a concrete base: "
        "six tiered rows of dark navy-blue and gold padded stadium seats, "
        "covered overhead by a modern white-and-steel cantilevered stadium roof canopy with glowing white LED floodlight bars underneath, "
        "plus clear light-blue glass windbreak panels on the sides.",
    ),
    (
        "capacity_5",
        "Luxury VIP football stadium grandstand with skybox on a concrete base: "
        "four lower tiered rows of wide dark-red and gold leather VIP stadium seats, "
        "topped by an enclosed glass-fronted VIP luxury skybox lounge glowing with warm golden interior light, "
        "a bright blue-and-gold glowing LED ribbon banner across the middle front fascia, "
        "and a sleek curved white stadium roof canopy with twin bright broadcast floodlight arrays on top.",
    ),
    (
        "capacity_6",
        "Massive double-decker megastructure football stadium grandstand on a grey concrete base: "
        "lower tier with six rows of red-and-gold executive cushioned stadium seats, "
        "middle tier with a wide panoramic glass VIP skybox concourse glowing with warm gold interior light, "
        "upper cantilevered seating deck with four more stadium seat rows beneath a reinforced steel-truss roof canopy, "
        "with dual broadcast camera pods and four bright white stadium floodlight banks mounted along the roof.",
    ),
    (
        "capacity_7",
        "Retractable-dome football stadium arena grandstand with Jumbotron on a grey concrete base: "
        "double-decker red-and-gold stadium seating tiers framed by shiny chrome structural columns, "
        "a curved wrap-around pixel-art Jumbotron video scoreboard on the upper fascia displaying glowing green and cyan pitch telemetry graphics, "
        "flanked by bright LED light arrays, topped by a heavy mechanized retractable dome roof segment with articulated steel hydraulic struts and sliding polycarbonate roof panels.",
    ),
    (
        "capacity_8",
        "Sleek near-future smart-glass cyber football stadium grandstand on a concrete base: "
        "tiered rows of dark carbon-fiber ergonomic pod seats with glowing cyan accent trim, "
        "a seamless curved dark-tinted smart-glass VIP pavilion with neon cyan and gold edge lighting, "
        "glowing cyan holographic stat-projector emitters along the railing, "
        "and sleek brushed silver-titanium armor paneling across the exterior frame.",
    ),
    (
        "capacity_9",
        "Sci-fi anti-gravity hover-box stadium seating grandstand section on a solid grey concrete base (seats only, no pitch): "
        "six stepped lower rows of dark carbon-fiber stadium seats with glowing cyan accent trim filling the front of the base, "
        "a detached floating anti-gravity luxury glass VIP skybox pod hovering clearly in mid-air above the upper tier via glowing blue plasma thruster nodes underneath, "
        "a translucent hexagonal cyan energy barrier shield framing the upper viewing deck, "
        "small docked robotic camera drones on the roof pylons, and twin vertical glowing cyan plasma light spires on the rear corners.",
    ),
    (
        "capacity_10",
        "Apex galactic coliseum citadel football stadium monument on a gilded square base: "
        "ornate white-and-gold cybernetic grandstand architecture fused with glowing royal-purple and cyan energy conduits, "
        "multi-tiered floating golden VIP throne seats, "
        "a monumental crowned white-and-gold roof spire housing a miniature hovering glowing holographic golden trophy orb in the center, "
        "radiant cyan-and-gold energy-beam floodlights, and gilded harmonic stabilizers framing the base.",
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
                "User-Agent": "swos-port-stadium-gen/2.0",
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
    raise RuntimeError(f"PixelLab stadium generation failed: {last_err}")


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
