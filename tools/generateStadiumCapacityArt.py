#!/usr/bin/env python3
"""Generate 5 stateful chained 2D isometric stadium seating sprites via PixelLab API.

Reads PIXELLAB_API_KEY from .env.local (gitignored) or environment.
Saves transparent 128x128 PNGs into assets/facilities/capacity_1.png .. capacity_5.png
(chroma-keying the solid #FF00FF magenta background cleanly to alpha=0).
"""

import base64
import io
import json
import os
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "assets" / "facilities"
IMG_SIZE = 128

GLOBAL_RULES = (
    "Strict 2:1 isometric projection, fixed three-quarter top-down angle, centered 4x4 isometric tile footprint. "
    "16-bit tactical RPG pixel art, crisp pixel clusters, clean 1px dark outlines, flat cel-shading lit from upper-left, "
    "zero anti-aliasing, zero gradients, zero blur. "
    "Solid flat pure magenta (#FF00FF) background, no floor shadow, no ground grid, no UI, no text, no spectators. "
)

LEVELS: list[tuple[str, str, int]] = [
    (
        "capacity_1",
        "Basic Wooden Bleachers: A rudimentary stadium seating section on a weathered grey concrete slab base. "
        "Three low rows of worn splintered wooden plank benches supported by rusty iron brackets. "
        "Simple galvanized metal pipe side railings. Sparse, weathered, entry-level municipal park aesthetic.",
        0,
    ),
    (
        "capacity_2",
        "Standard Aluminum Bleachers: Upgrade the previous stadium seating section keeping the exact same concrete base footprint and isometric angle. "
        "Replace wooden planks with four rows of clean brushed aluminum bench seating with integrated footboards and safety risers. "
        "Sturdier steel handrails on the sides and a small painted section plate on the front concrete wall.",
        320,
    ),
    (
        "capacity_3",
        "Molded Plastic Bucket Seats: Upgrade the previous stadium seating section keeping the exact same concrete base footprint and isometric angle. "
        "Expand to five tiered rows of individual royal-blue molded plastic folding stadium seats with backrests and armrests. "
        "Add concrete center aisle steps with yellow safety trim, stainless steel railings, and a low reinforced concrete barrier wall at the front.",
        300,
    ),
    (
        "capacity_4",
        "Covered Club-Level Seating: Upgrade the previous stadium seating section keeping the exact same base footprint and isometric angle. "
        "Upgrade the seats to six tiered rows of plush padded navy-and-gold ergonomic chairs with cup holders. "
        "Add a cantilevered steel-truss stadium canopy roof overhead with compact LED floodlight bars mounted under the eaves, plus glass windbreak panels along the sides.",
        260,
    ),
    (
        "capacity_5",
        "Luxury VIP Skybox & Tiered Suite: Upgrade the previous stadium seating section keeping the exact same base footprint and isometric angle. "
        "Transform the upper tier into an enclosed glass-fronted VIP luxury skybox lounge with warm interior gold lighting, sleek metallic trim, and a digital LED ribbon board along the fascia. "
        "Below the skybox, keep four rows of wide leather VIP seats with a curved architectural stadium roof canopy and twin broadcast-grade LED light arrays on top.",
        250,
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


def chroma_key_magenta_to_transparent(raw_png: bytes) -> bytes:
    """Extract solid #FF00FF magenta background cleanly to alpha=0 via flood-fill from image corners."""
    im = Image.open(io.BytesIO(raw_png)).convert("RGBA")
    w, h = im.size
    px = im.load()

    def is_magenta(r: int, g: int, b: int, a: int) -> bool:
        if a == 0:
            return True
        # Match pure magenta #FF00FF and near-magenta pixel-art border bleed
        return (r >= 195 and b >= 195 and g <= 85 and abs(r - b) <= 45)

    visited = [[False] * w for _ in range(h)]
    stack: list[tuple[int, int]] = []
    for x in range(w):
        stack.append((x, 0))
        stack.append((x, h - 1))
    for y in range(h):
        stack.append((0, y))
        stack.append((w - 1, y))

    while stack:
        cx, cy = stack.pop()
        if cx < 0 or cy < 0 or cx >= w or cy >= h or visited[cy][cx]:
            continue
        visited[cy][cx] = True
        r, g, b, a = px[cx, cy]
        if is_magenta(r, g, b, a):
            px[cx, cy] = (0, 0, 0, 0)
            stack.append((cx + 1, cy))
            stack.append((cx - 1, cy))
            stack.append((cx, cy + 1))
            stack.append((cx, cy - 1))

    out = io.BytesIO()
    im.save(out, format="PNG")
    return out.getvalue()


def generate_level(api_key: str, prompt: str, prev_raw_png: bytes | None, init_strength: int) -> bytes:
    full_desc = GLOBAL_RULES + prompt
    candidates: list[tuple[str, dict]] = []

    if prev_raw_png is not None and init_strength > 0:
        b64_prev = base64.b64encode(prev_raw_png).decode("ascii")
        # Prefer Pixflux / Bitforge with init_image + init_image_strength for stateful step-by-step upgrades
        candidates.append((
            "https://api.pixellab.ai/v1/generate-image-pixflux",
            {
                "description": full_desc,
                "negative_description": "blur, gradient, anti-aliasing, text, spectators, people, crowd, shadow, ground grid, UI",
                "image_size": {"width": IMG_SIZE, "height": IMG_SIZE},
                "init_image": {"type": "base64", "base64": b64_prev},
                "init_image_strength": init_strength,
                "view": "high top-down",
                "direction": "south-east",
                "outline": "single color black outline",
                "shading": "basic shading",
                "detail": "high detail",
                "no_background": False,
            },
        ))
        candidates.append((
            "https://api.pixellab.ai/v1/generate-image-bitforge",
            {
                "description": full_desc,
                "negative_description": "blur, gradient, anti-aliasing, text, spectators, people, crowd, shadow, ground grid, UI",
                "image_size": {"width": IMG_SIZE, "height": IMG_SIZE},
                "init_image": {"type": "base64", "base64": b64_prev},
                "init_image_strength": init_strength,
                "no_background": False,
            },
        ))
    else:
        candidates.append((
            "https://api.pixellab.ai/v1/generate-image-pixflux",
            {
                "description": full_desc,
                "negative_description": "blur, gradient, anti-aliasing, text, spectators, people, crowd, shadow, ground grid, UI",
                "image_size": {"width": IMG_SIZE, "height": IMG_SIZE},
                "view": "high top-down",
                "direction": "south-east",
                "outline": "single color black outline",
                "shading": "basic shading",
                "detail": "high detail",
                "no_background": False,
            },
        ))
        candidates.append((
            "https://api.pixellab.ai/v2/create-image-pixen",
            {
                "description": full_desc,
                "image_size": {"width": IMG_SIZE, "height": IMG_SIZE},
                "no_background": False,
            },
        ))

    last_err = None
    for url, payload in candidates:
        req = urllib.request.Request(
            url,
            data=json.dumps(payload).encode("utf-8"),
            headers={
                "Authorization": f"Bearer {api_key}",
                "Content-Type": "application/json",
                "User-Agent": "swos-port-stadium-gen/1.0",
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
    prev_raw: bytes | None = None

    for name, prompt, strength in LEVELS:
        print(f"[GEN ] {name}.png (chained={prev_raw is not None}, strength={strength}) ...", flush=True)
        raw_png = generate_level(api_key, prompt, prev_raw, strength)
        prev_raw = raw_png
        clean_png = chroma_key_magenta_to_transparent(raw_png)
        out_path = OUT_DIR / f"{name}.png"
        out_path.write_bytes(clean_png)
        print(f"[ OK ] {name}.png ({len(clean_png)} bytes, transparent RGBA)", flush=True)
        time.sleep(0.4)

    return 0


if __name__ == "__main__":
    sys.exit(main())
