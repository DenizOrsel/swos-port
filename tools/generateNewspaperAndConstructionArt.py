#!/usr/bin/env python3
"""Generate SWOS-style Man of the Match portraits and an 8-frame construction animation via PixelLab v2 API.

Reads PIXELLAB_API_KEY from .env.local (gitignored) or environment.
Outputs:
  - assets/newspaper/motm_1.png .. motm_4.png  (64x64 16-bit SWOS-style footballer portraits)
  - assets/facilities/construction_0.png .. construction_7.png  (128x128 8-frame animated construction overlay)
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
NEWS_DIR = ROOT / "assets" / "newspaper"
FAC_DIR = ROOT / "assets" / "facilities"

MOTM_PORTRAITS: list[tuple[str, str]] = [
    (
        "motm_1",
        "16-bit Amiga Sensible World of Soccer 1996 retro pixel art football player portrait bust, "
        "front three-quarter head and shoulders of a determined 1990s European striker with short dark brown hair, "
        "wearing a classic collared royal-blue and white football jersey, confident expression, "
        "clean 1px dark outlines, warm retro stadium floodlight backdrop, crisp pixel clusters, no text, no blur.",
    ),
    (
        "motm_2",
        "16-bit Amiga Sensible World of Soccer 1996 retro pixel art football player portrait bust, "
        "front three-quarter head and shoulders of a charismatic 1990s playmaker midfielder with wavy blonde hair, "
        "wearing a classic collared crimson-red and white football jersey, slight triumphant smile, "
        "clean 1px dark outlines, warm retro stadium floodlight backdrop, crisp pixel clusters, no text, no blur.",
    ),
    (
        "motm_3",
        "16-bit Amiga Sensible World of Soccer 1996 retro pixel art football player portrait bust, "
        "front three-quarter head and shoulders of a powerful 1990s box-to-box footballer with short cropped black hair, "
        "wearing a classic collared emerald-green and white football jersey, fierce focused gaze, "
        "clean 1px dark outlines, warm retro stadium floodlight backdrop, crisp pixel clusters, no text, no blur.",
    ),
    (
        "motm_4",
        "16-bit Amiga Sensible World of Soccer 1996 retro pixel art football goalkeeper portrait bust, "
        "front three-quarter head and shoulders of a heroic 1990s goalkeeper with short brown hair, "
        "wearing a bright yellow-and-black retro padded goalkeeper jersey with raised goalie gloves near shoulders, "
        "clean 1px dark outlines, warm retro stadium floodlight backdrop, crisp pixel clusters, no text, no blur.",
    ),
]

CONSTRUCTION_FIRST_FRAME_PROMPT = (
    "Isometric 2:1 sports management game pixel art sprite of a bright yellow construction tower crane "
    "with black-and-yellow hazard stripes, hanging steel hook lifting a grey steel I-beam girder, "
    "flashing orange warning beacon light on top of the crane cab, and a compact yellow mini-excavator at the base, "
    "fixed three-quarter top-down view, crisp 16-bit pixel art, clean 1px black outlines, flat cel-shading, "
    "transparent background, no ground slab, no floor shadow, no text."
)

CONSTRUCTION_ACTION = (
    "yellow construction crane gently swinging its boom while hoisting and lowering the hanging steel I-beam girder, "
    "orange warning beacon light pulsing bright and dim on top, subtle welding sparks and mini-excavator arm moving, "
    "smooth seamless looping pixel art animation"
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
            "User-Agent": "swos-port-art-gen/1.0",
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
            "User-Agent": "swos-port-art-gen/1.0",
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


def generate_construction_animation(api_key: str) -> None:
    FAC_DIR.mkdir(parents=True, exist_ok=True)
    print("[GEN ] construction first frame (128x128 pixen-v2, no_background=True) ...", flush=True)
    first_png = generate_pixen(api_key, CONSTRUCTION_FIRST_FRAME_PROMPT, 128, no_bg=True)
    (FAC_DIR / "construction_0.png").write_bytes(first_png)
    print(f"[ OK ] construction_0.png ({len(first_png)} bytes)", flush=True)

    b64_first = "data:image/png;base64," + base64.b64encode(first_png).decode("ascii")
    print("[ANIM] Submitting /v2/animate-with-text-v3 (8 frames, no_background=True) ...", flush=True)
    job = api_post(
        api_key,
        "https://api.pixellab.ai/v2/animate-with-text-v3",
        {
            "first_frame": {"base64": b64_first},
            "action": CONSTRUCTION_ACTION,
            "frame_count": 8,
            "no_background": True,
        },
    )
    job_id = job.get("background_job_id") or job.get("job_id")
    if not job_id:
        raise RuntimeError(f"Missing background_job_id in response: {job}")
    print(f"[ANIM] Job {job_id} accepted; polling ...", flush=True)

    for attempt in range(90):
        time.sleep(3.0)
        status_resp = api_get(api_key, f"https://api.pixellab.ai/v2/background-jobs/{job_id}")
        st = status_resp.get("status", "")
        if attempt % 5 == 0 or st in ("completed", "failed"):
            print(f"[POLL] #{attempt+1} status={st}", flush=True)
        if st == "failed":
            raise RuntimeError(f"PixelLab animation job failed: {status_resp}")
        if st == "completed":
            last_resp = status_resp.get("last_response") or {}
            images = last_resp.get("images") or status_resp.get("images") or []
            if not images:
                raise RuntimeError(f"Completed job returned no images: {list(last_resp.keys())}")
            for idx, img_obj in enumerate(images):
                raw = decode_b64_image(img_obj)
                out_p = FAC_DIR / f"construction_{idx}.png"
                out_p.write_bytes(raw)
                print(f"[ OK ] {out_p.name} ({len(raw)} bytes)", flush=True)
            return
    raise RuntimeError(f"Timed out waiting for animation job {job_id}")


def main() -> int:
    api_key = load_api_key()
    only = set(sys.argv[1:]) if len(sys.argv) > 1 else None

    if only is None or "motm" in only:
        NEWS_DIR.mkdir(parents=True, exist_ok=True)
        for name, prompt in MOTM_PORTRAITS:
            print(f"[GEN ] {name}.png (64x64 pixen-v2) ...", flush=True)
            png = generate_pixen(api_key, prompt, 64, no_bg=False)
            out_p = NEWS_DIR / f"{name}.png"
            out_p.write_bytes(png)
            print(f"[ OK ] {out_p.name} ({len(png)} bytes)", flush=True)
            time.sleep(0.4)

    if only is None or "construction" in only:
        generate_construction_animation(api_key)

    return 0


if __name__ == "__main__":
    sys.exit(main())
