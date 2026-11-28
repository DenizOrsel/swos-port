#!/usr/bin/env python3
"""Generate high-res 16-bit animated stadium spectator tiles via PixelLab v2 API.

Reads PIXELLAB_API_KEY from .env.local (gitignored) or environment.
Outputs 64x64 pixel-art tiles into assets/crowd/:
  - top_a_0.png .. top_a_3.png       (Top stand facing down, variant A, 4 frames)
  - top_b_0.png .. top_b_3.png       (Top stand facing down, variant B, 4 frames)
  - top_stairs.png                   (Top stand concrete aisle steps flanked by fans)
  - bot_front_0.png .. bot_front_3.png (Bottom stand front row, viewed from behind, transparent top edge)
  - bot_a_0.png .. bot_a_3.png       (Bottom stand terrace rows viewed from behind, variant A, 4 frames)
  - bot_b_0.png .. bot_b_3.png       (Bottom stand terrace rows viewed from behind, variant B, 4 frames)

Team-color convention:
  - Pure crimson RED jerseys/shirts  -> dynamically recolored to team primary kit color in C++
  - Pure cobalt BLUE scarves/caps    -> dynamically recolored to team secondary kit color in C++
  - Skin, hair, neutral jackets, and concrete terrace steps stay natural.
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
OUT_DIR = ROOT / "assets" / "crowd"

STYLE_BASE = (
    "16-bit retro Amiga Sensible World of Soccer top-down stadium pixel art tile, "
    "crisp pixel clusters, clean 1px dark outlines, flat cel-shading, zero anti-aliasing, zero blur, no text, "
    "fills the entire square canvas edge-to-edge with seamless horizontal continuity"
)

SPECS = [
    (
        "top_a",
        f"{STYLE_BASE}. Top-down three-quarter view looking slightly downward at a packed dark grey concrete football stadium terrace "
        "of 3 neat horizontal rows (4 fans per row) facing DOWN toward the bottom of the frame. "
        "Dark grey concrete steps behind them, NO grass, NO green. "
        "Fans have varied hair (blonde, black, grey, brown, blue cap, red cap), warm peach skin, wearing BRIGHT PURE CRIMSON RED football jerseys "
        "and BRIGHT PURE COBALT BLUE scarves mixed with 2 dark charcoal jackets. Crisp readable chibi pixel faces and shoulders.",
        "packed football stadium spectators jumping up and down cheering loudly, raising both arms high in the air and waving bright blue scarves overhead, keeping dark grey concrete steps behind them",
        False,
    ),
    (
        "top_b",
        f"{STYLE_BASE}. Front-facing three-quarter top-down view of a densely packed dark grey concrete stadium terrace "
        "filled completely from top edge to bottom edge with 3 tightly stacked horizontal rows of 4 chibi 16-bit sports fans per row "
        "(12 fans total, standing shoulder to shoulder facing forward/down). "
        "Dark charcoal grey horizontal concrete bleacher steps behind them — NO grass, NO green, NO field, NO sky. "
        "Different arrangement of hair (brown, black, blonde, ginger), wearing BRIGHT PURE CRIMSON RED jerseys and BRIGHT PURE COBALT BLUE scarves.",
        "packed stadium crowd of tiny fans bouncing and cheering enthusiastically, waving bright blue scarves and raising fists overhead, keeping dark grey concrete steps behind them, no green",
        False,
    ),
    (
        "bot_a",
        f"{STYLE_BASE}. Rear three-quarter top-down view of the BACKS of a densely packed dark grey concrete stadium terrace "
        "filled completely from top edge to bottom edge with 3 tightly stacked horizontal rows of 4 chibi 16-bit sports fans per row "
        "(12 fans total viewed strictly from BEHIND, backs of heads and shoulders only). "
        "Dark charcoal grey horizontal concrete bleacher steps everywhere between rows — STRICTLY NO green, NO grass, NO field, NO lines, NO sky. "
        "Varied hair colors (black, brown, blonde, grey), wearing BRIGHT PURE CRIMSON RED football jerseys and BRIGHT PURE COBALT BLUE scarves draped over their backs, mixed with a few dark charcoal jackets.",
        "packed stadium fans viewed from behind jumping up and down cheering, throwing both arms high into the air and waving bright blue scarves overhead, keeping dark grey concrete steps behind them, no green",
        False,
    ),
    (
        "bot_b",
        f"{STYLE_BASE}. Rear three-quarter top-down view of the BACKS of a densely packed dark grey concrete stadium terrace "
        "filled completely from top edge to bottom edge with 3 tightly stacked horizontal rows of 4 chibi 16-bit sports fans per row "
        "(12 fans total viewed strictly from BEHIND, backs of heads and shoulders only). "
        "Dark charcoal grey horizontal concrete bleacher steps everywhere between rows — STRICTLY NO green, NO grass, NO field, NO lines, NO sky. "
        "Different hair and cap arrangement (brown, blonde, black, grey, red cap, blue cap), wearing BRIGHT PURE CRIMSON RED jerseys and BRIGHT PURE COBALT BLUE scarves.",
        "packed stadium fans viewed from behind bouncing and throwing both arms up cheering excitedly while waving bright blue scarves, keeping dark grey concrete steps behind them, no green",
        False,
    ),
    (
        "bot_front",
        "16-bit retro Amiga Sensible World of Soccer top-down stadium pixel art sprite, "
        "crisp pixel clusters, clean 1px dark outlines, flat cel-shading, zero anti-aliasing, zero blur, no text. "
        "Front two rows of a lower football stadium stand viewed from BEHIND: top 25 percent is completely empty transparent space, "
        "lower 75 percent shows 2 neat horizontal rows of 4 tiny spectators viewed from behind on dark grey concrete terrace steps, "
        "wearing BRIGHT PURE CRIMSON RED football jerseys and BRIGHT PURE COBALT BLUE scarves. No side walls, no white lines, no grass.",
        "tiny football spectators viewed from behind throwing both arms up in the air cheering and waving bright blue scarves, keeping the top area completely transparent",
        True,
    ),
]

TOP_STAIRS_PROMPT = (
    f"{STYLE_BASE}. Top-down three-quarter view looking slightly downward at a vertical grey concrete stadium terrace stairway aisle "
    "with horizontal concrete steps and thin dark iron handrails running straight down the center third of the tile, "
    "flanked on the far left and far right edges by partial tiny seated spectators facing DOWN wearing BRIGHT PURE CRIMSON RED jerseys "
    "and BRIGHT PURE COBALT BLUE scarves. Crisp 16-bit SWOS-scale pixel art."
)


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


def api_post(api_key: str, url: str, payload: dict) -> dict:
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
            "User-Agent": "swos-port-crowd-gen/1.0",
        },
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=180) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")[:500]
        raise RuntimeError(f"POST {url} -> HTTP {e.code}: {body}")


def api_get(api_key: str, url: str) -> dict:
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {api_key}",
            "User-Agent": "swos-port-crowd-gen/1.0",
        },
        method="GET",
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")[:500]
        raise RuntimeError(f"GET {url} -> HTTP {e.code}: {body}")


def decode_b64_image(obj) -> bytes:
    if isinstance(obj, dict) and "base64" in obj:
        b64 = obj["base64"]
    elif isinstance(obj, str):
        b64 = obj
    else:
        raise ValueError(f"Unexpected image object: {type(obj)}")
    if "," in b64:
        b64 = b64.split(",", 1)[1]
    return base64.b64decode(b64)


def generate_pixen(api_key: str, desc: str, size: int, no_bg: bool) -> bytes:
    resp = api_post(
        api_key,
        "https://api.pixellab.ai/v2/create-image-pixen",
        {
            "description": desc,
            "image_size": {"width": size, "height": size},
            "no_background": no_bg,
        },
    )
    if "image" in resp:
        return decode_b64_image(resp["image"])
    if "data" in resp and isinstance(resp["data"], dict) and "image" in resp["data"]:
        return decode_b64_image(resp["data"]["image"])
    raise ValueError(f"Unrecognized pixen response keys: {list(resp.keys())}")


def submit_anim_job(api_key: str, first_png: bytes, action: str, frame_count: int, no_bg: bool) -> str:
    b64_first = "data:image/png;base64," + base64.b64encode(first_png).decode("ascii")
    job = api_post(
        api_key,
        "https://api.pixellab.ai/v2/animate-with-text-v3",
        {
            "first_frame": {"base64": b64_first},
            "action": action,
            "frame_count": frame_count,
            "no_background": no_bg,
        },
    )
    job_id = job.get("background_job_id") or job.get("job_id")
    if not job_id:
        raise RuntimeError(f"Missing background_job_id in response: {job}")
    return job_id


def poll_anim_jobs(api_key: str, jobs: dict[str, str]) -> None:
    pending = dict(jobs)
    for attempt in range(120):
        if not pending:
            return
        time.sleep(3.0)
        for name, job_id in list(pending.items()):
            resp = api_get(api_key, f"https://api.pixellab.ai/v2/background-jobs/{job_id}")
            st = resp.get("status", "")
            if st == "failed":
                raise RuntimeError(f"Job {name} ({job_id}) failed: {resp}")
            if st == "completed":
                last_resp = resp.get("last_response") or {}
                images = last_resp.get("images") or resp.get("images") or []
                if not images:
                    raise RuntimeError(f"Job {name} completed with no images: {list(last_resp.keys())}")
                for idx, img_obj in enumerate(images):
                    raw = decode_b64_image(img_obj)
                    out_p = OUT_DIR / f"{name}_{idx}.png"
                    out_p.write_bytes(raw)
                print(f"[DONE] {name}: saved {len(images)} frames", flush=True)
                del pending[name]
        if pending and attempt % 4 == 0:
            print(f"[POLL] #{attempt+1} still waiting on: {', '.join(pending.keys())}", flush=True)
    raise RuntimeError(f"Timed out waiting on jobs: {list(pending.keys())}")


def main() -> int:
    api_key = load_api_key()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    only = set(sys.argv[1:]) if len(sys.argv) > 1 else None

    # 1. Generate static top stairway tile
    if only is None or "top_stairs" in only:
        print("[GEN ] top_stairs.png (64x64 pixen-v2) ...", flush=True)
        png = generate_pixen(api_key, TOP_STAIRS_PROMPT, 64, no_bg=False)
        (OUT_DIR / "top_stairs.png").write_bytes(png)
        print(f"[ OK ] top_stairs.png ({len(png)} bytes)", flush=True)
        time.sleep(0.4)

    # 2. Generate first frames and submit all 4-frame animation jobs in parallel on PixelLab's queue
    jobs: dict[str, str] = {}
    for name, prompt, action, no_bg in SPECS:
        if only is not None and name not in only:
            continue
        print(f"[GEN ] {name}_0.png (64x64 pixen-v2, no_bg={no_bg}) ...", flush=True)
        first_png = generate_pixen(api_key, prompt, 64, no_bg=no_bg)
        (OUT_DIR / f"{name}_0.png").write_bytes(first_png)
        print(f"[ OK ] {name}_0.png ({len(first_png)} bytes) -> submitting 4-frame animation ...", flush=True)
        job_id = submit_anim_job(api_key, first_png, action, frame_count=4, no_bg=no_bg)
        jobs[name] = job_id
        print(f"[JOB ] {name} -> {job_id}", flush=True)
        time.sleep(0.4)

    if jobs:
        poll_anim_jobs(api_key, jobs)

    print("All PixelLab crowd assets generated cleanly!", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
