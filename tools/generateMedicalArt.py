#!/usr/bin/env python3
"""Generate 10 consistent 2D isometric stadium medical facility sprites via PixelLab API.

Reads PIXELLAB_API_KEY from .env.local (gitignored) or environment.
Saves clean transparent 128x128 RGBA PNGs into assets/facilities/medical_1.png .. medical_10.png.

Usage:
    python tools/generateMedicalArt.py                        # generate all 10
    python tools/generateMedicalArt.py medical_4 medical_9    # regenerate specific sprite(s)
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
    "Isometric 2:1 sports management game sprite of a football stadium sports medicine and medical clinic facility, "
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
        "medical_1",
        "Rudimentary outdoor stadium sideline first-aid station on a weathered grey concrete slab base facing front-left: "
        "a simple folding white canvas stretcher cot, an open red plastic first-aid toolbox, "
        "a small metal folding table with rolled white bandages and antiseptic bottles, a white sun umbrella, "
        "and a small white signboard with a bright red cross symbol.",
    ),
    (
        "medical_2",
        "White canvas field-medic tent on a grey concrete slab base facing front-left: "
        "a crisp white pitched canvas medical tent with a bright red cross emblem printed on the roof, "
        "open front tent flap revealing two examination cots, a silver IV drip stand, a blue-and-white ice cooler chest, "
        "and portable green oxygen cylinders inside.",
    ),
    (
        "medical_3",
        "Modular prefabricated portable stadium clinic trailer on a grey concrete slab base facing front-left: "
        "clean white rectangular medical cabin with a bright crimson stripe along the walls resting on steel leveling jacks, "
        "metal entry stairs with safety handrails, tinted sliding windows, a wall-mounted first-aid cabinet, "
        "and a glowing red-cross lightbox sign mounted above the door.",
    ),
    (
        "medical_4",
        "Permanent white-brick and steel stadium sports infirmary building on a grey concrete slab base facing front-left: "
        "wide double automatic glass entrance doors beneath a covered ambulance drop-off canopy with red-and-white safety bollards, "
        "large frosted clinic windows revealing exam beds and rolling medical carts inside, and rooftop silver ventilation units.",
    ),
    (
        "medical_5",
        "Sleek modern sports physical therapy and recovery center on a grey concrete slab base facing front-left: "
        "crisp white aluminum cladding and teal-tinted floor-to-ceiling glass walls revealing hydrotherapy plunge tubs, "
        "physical therapy stretch tables, and diagnostic monitors inside, topped with a clean architectural roof overhang "
        "and a glowing crimson-and-white medical cross emblem.",
    ),
    (
        "medical_6",
        "Two-story stadium sports medicine hospital wing on a grey concrete slab base facing front-left: "
        "ground floor with an emergency stretcher entrance bay and twin glass doors with red-cross signage, "
        "second floor with panoramic wrap-around glass wards showing adjustable hospital beds and glowing heart-rate monitors, "
        "exterior steel emergency stairs along the side, and a rooftop mechanical condenser array.",
    ),
    (
        "medical_7",
        "Two-story stadium trauma center with rooftop medevac helipad on a grey concrete slab base facing front-left: "
        "steel-and-glass clinical building with second-floor diagnostic imaging bay showing a white circular MRI scanner ring inside, "
        "a glowing cyan-and-red biometric status ticker along the fascia, "
        "topped by a cantilevered circular rooftop medevac helipad marked with a white-and-red H-cross symbol and perimeter beacon lights.",
    ),
    (
        "medical_8",
        "High-tech near-future cybernetic sports bio-regeneration lab on a grey concrete slab base facing front-left: "
        "sleek sterile white titanium and dark smart-glass cladding with glowing neon cyan and crimson LED trim, "
        "vertical translucent glowing cyan cryo-recovery pods visible behind the glass walls, "
        "robotic surgical-arm stations inside, and glowing cyan holographic anatomical scanner projectors flanking the entrance.",
    ),
    (
        "medical_9",
        "Sci-fi anti-gravity hover-ward and stasis pod medical clinic on a grey concrete slab base facing front-left: "
        "sleek white-and-cyan ground-floor clinic with glowing crimson cross emblem, "
        "crowned by a detached floating anti-gravity glass cylinder intensive-care ward hovering clearly in mid-air above the clinic on glowing blue plasma thruster nodes, "
        "glowing cyan stasis-healing chambers visible inside the floating cylinder, translucent hexagonal cyan energy barriers, and twin vertical glowing bio-beacon spires.",
    ),
    (
        "medical_10",
        "Apex galactic bio-sanctuary medical citadel on a gilded square base facing front-left: "
        "ornate white-and-gold cybernetic sanctuary architecture fused with glowing cyan, emerald-green, and royal-purple healing energy conduits, "
        "tiered floating golden glass bio-stasis chambers, "
        "a monumental crowned white-and-gold spire roof projecting a giant hovering glowing holographic cyan DNA-helix and golden medical cross orb, "
        "and radiant emerald-and-cyan healing-beam pylons framing the four corners of the base.",
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
                "User-Agent": "swos-port-medical-gen/1.0",
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
    raise RuntimeError(f"PixelLab medical generation failed: {last_err}")


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
