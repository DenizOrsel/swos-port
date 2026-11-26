#!/usr/bin/env python3
"""Politely download 64x64-resized club, national team, and league logos for SWOS 96/97.

Uses football-logos.cc's official image-sitemap.xml.gz (1 single catalog fetch, zero HTML scraping),
matches against local SWOS TEAM.* clubs/national teams plus leagues & tournaments, downloads only
matching 700x700 PNGs politely (single-threaded, rate-limited), and resizes locally to crisp 64x64
transparent RGBA PNGs in assets/logos/{clubs,leagues}/.

Usage:
    python tools/downloadFootballLogos.py --dry-run         # preview match coverage
    python tools/downloadFootballLogos.py --delay 0.3       # download missing logos politely
"""

import argparse
import gzip
import io
import re
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = ROOT / "dist" / "swos-port" / "data"
OUT_DIR = ROOT / "assets" / "logos"
CLUBS_DIR = OUT_DIR / "clubs"
LEAGUES_DIR = OUT_DIR / "leagues"
MANIFEST_PATH = OUT_DIR / "manifest.txt"
SITEMAP_CACHE = ROOT / "tmp" / "football-logos-image-sitemap.xml"

HEADERS = {
    "User-Agent": "SWOS-Port-LogoHelper/1.0 (personal retro football project; polite rate limit)"
}

# Explicit overrides for tricky 16-char SWOS 96/97 abbreviations -> football-logos.cc slug
SWOS_EXACT_SLUGS: dict[str, str] = {
    "MANCHESTER UTD": "manchester-united",
    "MANCHESTER CITY": "manchester-city",
    "SHEFFIELD WED": "sheffield-wednesday",
    "SHEFFIELD UTD": "sheffield-united",
    "NOTTM FOREST": "nottingham-forest",
    "TOTTENHAM": "tottenham",
    "NEWCASTLE": "newcastle",
    "WEST HAM": "west-ham",
    "QPR": "queens-park-rangers",
    "WOLVES": "wolverhampton-wanderers",
    "MIDDLESBROUGH": "middlesbrough",
    "BLACKBURN": "blackburn-rovers",
    "BOLTON": "bolton-wanderers",
    "BRIGHTON": "brighton-and-hove-albion",
    "BRISTOL CITY": "bristol-city",
    "BRISTOL ROVERS": "bristol-rovers",
    "CHARLTON": "charlton-athletic",
    "COVENTRY": "coventry-city",
    "CRYSTAL PALACE": "crystal-palace",
    "DERBY": "derby-county",
    "HUDDERSFIELD": "huddersfield-town",
    "IPSWICH": "ipswich-town",
    "LEEDS": "leeds-united",
    "LEICESTER": "leicester-city",
    "NORWICH": "norwich-city",
    "NOTTS COUNTY": "notts-county",
    "OLDHAM": "oldham-athletic",
    "OXFORD": "oxford-united",
    "PLYMOUTH": "plymouth-argyle",
    "PORTSMOUTH": "portsmouth",
    "PRESTON": "preston-north-end",
    "READING": "reading",
    "ROTHERHAM": "rotherham-united",
    "SOUTHAMPTON": "southampton",
    "STOKE": "stoke-city",
    "SUNDERLAND": "sunderland",
    "SWANSEA": "swansea-city",
    "SWINDON": "swindon-town",
    "TRANMERE": "tranmere-rovers",
    "WATFORD": "watford",
    "WEST BROM": "west-bromwich-albion",
    "WIGAN": "wigan-athletic",
    "WIMBLEDON": "afc-wimbledon",
    "AC MILAN": "milan",
    "INTER": "internazionale",
    "INTER MILAN": "internazionale",
    "JUVENTUS": "juventus",
    "AS ROMA": "roma",
    "ROMA": "roma",
    "LAZIO": "lazio",
    "NAPOLI": "napoli",
    "FIORENTINA": "fiorentina",
    "PARMA": "parma",
    "SAMPDORIA": "sampdoria",
    "UDINESE": "udinese",
    "BOLOGNA": "bologna",
    "ATALANTA": "atalanta",
    "CAGLIARI": "cagliari",
    "TORINO": "torino",
    "GENOA": "genoa",
    "VERONA": "hellas-verona",
    "BARCELONA": "barcelona",
    "REAL MADRID": "real-madrid",
    "ATLETICO MADRID": "atletico-de-madrid",
    "ATHLETIC BILBAO": "athletic-club",
    "VALENCIA": "valencia",
    "SEVILLA": "sevilla",
    "REAL BETIS": "real-betis",
    "REAL SOCIEDAD": "real-sociedad",
    "DEPORTIVO": "deportivo-la-coruna",
    "CELTA VIGO": "celta-de-vigo",
    "ESPANYOL": "espanyol",
    "REAL ZARAGOZA": "real-zaragoza",
    "VILLARREAL": "villarreal",
    "BAYERN MUNICH": "bayern-munchen",
    "BORUSSIA DORT": "borussia-dortmund",
    "DORTMUND": "borussia-dortmund",
    "BORUSSIA MGLAD": "borussia-monchengladbach",
    "SCHALKE 04": "schalke-04",
    "HAMBURG": "hamburger-sv",
    "WERDER BREMEN": "werder-bremen",
    "BAYER LEVERK": "bayer-leverkusen",
    "STUTTGART": "stuttgart",
    "VFB STUTTGART": "stuttgart",
    "EINTRACHT FRANK": "eintracht-frankfurt",
    "FC KOLN": "koln",
    "KAISERSLAUTERN": "kaiserslautern",
    "PARIS SG": "paris-saint-germain",
    "PARIS ST GERM": "paris-saint-germain",
    "MARSEILLE": "olympique-de-marseille",
    "LYON": "olympique-lyonnais",
    "MONACO": "as-monaco",
    "BORDEAUX": "bordeaux",
    "NANTES": "nantes",
    "LILLE": "lille",
    "LENS": "lens",
    "AUXERRE": "auxerre",
    "FENERBAHCE": "fenerbahce",
    "GALATASARAY": "galatasaray",
    "BESIKTAS": "besiktas",
    "TRABZONSPOR": "trabzonspor",
    "BURSASPOR": "bursaspor",
    "AJAX": "ajax",
    "PSV": "psv-eindhoven",
    "PSV EINDHOVEN": "psv-eindhoven",
    "FEYENOORD": "feyenoord",
    "BENFICA": "benfica",
    "FC PORTO": "porto",
    "PORTO": "porto",
    "SPORTING": "sporting-cp",
    "SPORTING LISBON": "sporting-cp",
    "CELTIC": "celtic",
    "RANGERS": "rangers",
    "ABERDEEN": "aberdeen",
    "HEARTS": "heart-of-midlothian",
    "HIBERNIAN": "hibernian",
    "ANDERLECHT": "anderlecht",
    "CLUB BRUGGE": "club-brugge",
    "STANDARD LIEGE": "standard-liege",
    "OLYMPIAKOS": "olympiacos",
    "PANATHINAIKOS": "panathinaikos",
    "AEK ATHENS": "aek-athens",
    "BOCA JUNIORS": "boca-juniors",
    "RIVER PLATE": "river-plate",
    "FLAMENGO": "flamengo",
    "SAO PAULO": "sao-paulo",
    "CORINTHIANS": "corinthians",
    "PALMEIRAS": "palmeiras",
    "SANTOS": "santos",
    "GREMIO": "gremio",
    "RED STAR BELG": "crvena-zvezda",
    "PARTIZAN BELG": "partizan",
    "DINAMO ZAGREB": "dinamo-zagreb",
    "HAJDUK SPLIT": "hajduk-split",
    "SPARTAK MOSCOW": "spartak-moscow",
    "CSKA MOSCOW": "cska-moscow",
    "DYNAMO KIEV": "dynamo-kyiv",
    "SHAKHTAR DON": "shakhtar-donetsk",
    "STEAUA BUCH": "fcsb",
    "ROSENBORG": "rosenborg",
    "IFK GOTEBORG": "ifk-goteborg",
    "BRONDBY": "brondby",
    "FC COPENHAGEN": "kobenhavn",
    "SPARTA PRAGUE": "sparta-praha",
    "SLAVIA PRAGUE": "slavia-praha",
    "LEGIA WARSAW": "legia-warszawa",
    "RAPID VIENNA": "rapid-wien",
    "AUSTRIA VIENNA": "austria-wien",
    "STURM GRAZ": "sturm-graz",
    "SV SALZBURG": "red-bull-salzburg",
    "GRASSHOPPERS": "grasshopper",
    "FC BASEL": "basel",
}

# League & Competition logos to fetch into assets/logos/leagues/<key>.png
LEAGUE_TARGETS: dict[str, list[str]] = {
    "england_premier": ["english-premier-league"],
    "england_div1": ["efl-championship"],
    "england_div2": ["efl-league-one"],
    "england_div3": ["efl-league-two"],
    "england_facup": ["fa-cup", "emirates-fa-cup"],
    "england_leaguecup": ["efl-cup", "carabao-cup"],
    "italy_seriea": ["italy-serie-a", "serie-a"],
    "italy_serieb": ["italy-serie-b", "serie-b"],
    "spain_laliga": ["la-liga", "spain-la-liga"],
    "spain_segunda": ["segunda-division", "la-liga-2"],
    "germany_bundesliga": ["bundesliga", "germany-bundesliga"],
    "germany_2bundesliga": ["2-bundesliga", "germany-2-bundesliga"],
    "france_ligue1": ["ligue-1", "france-ligue-1"],
    "france_ligue2": ["ligue-2", "france-ligue-2"],
    "turkey_superlig": ["super-lig", "turkey-super-lig", "trendyol-super-lig"],
    "netherlands_eredivisie": ["eredivisie"],
    "portugal_primeira": ["primeira-liga", "liga-portugal"],
    "scotland_premier": ["scottish-premiership"],
    "belgium_pro": ["belgian-pro-league", "jupiler-pro-league"],
    "austria_bundesliga": ["austrian-bundesliga", "austria-bundesliga"],
    "switzerland_super": ["swiss-super-league"],
    "greece_super": ["super-league-greece"],
    "denmark_superliga": ["danish-superliga", "superliga"],
    "sweden_allsvenskan": ["allsvenskan"],
    "norway_eliteserien": ["eliteserien"],
    "poland_ekstraklasa": ["ekstraklasa"],
    "czech_liga": ["czech-first-league"],
    "russia_premier": ["russian-premier-league"],
    "ukraine_premier": ["ukrainian-premier-league"],
    "croatia_hnl": ["croatian-football-league", "hnl"],
    "argentina_primera": ["argentina-primera-division", "liga-profesional-argentina"],
    "brazil_seriea": ["brazilian-serie-a", "brasileirao"],
    "usa_mls": ["mls", "major-league-soccer"],
    "japan_j1": ["j1-league"],
    "uefa_champions": ["uefa-champions-league"],
    "uefa_europa": ["uefa-europa-league"],
    "uefa_conference": ["uefa-conference-league"],
    "fifa_worldcup": ["fifa-world-cup-2026", "fifa-world-cup"],
    "uefa_euro": ["uefa-euro-2024", "uefa-euro"],
    "copa_america": ["copa-america", "conmebol-copa-america"],
    "libertadores": ["conmebol-libertadores", "copa-libertadores"],
}


def fetch_sitemap() -> str:
    if SITEMAP_CACHE.is_file() and SITEMAP_CACHE.stat().st_size > 10000:
        return SITEMAP_CACHE.read_text(encoding="utf-8")
    SITEMAP_CACHE.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request("https://football-logos.cc/image-sitemap.xml.gz", headers=HEADERS)
    with urllib.request.urlopen(req, timeout=30) as r:
        raw = gzip.decompress(r.read()).decode("utf-8", "replace")
    SITEMAP_CACHE.write_text(raw, encoding="utf-8")
    return raw


def slugify(name: str) -> str:
    s = name.lower().strip()
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    return s


def strip_club_tokens(slug: str) -> str:
    drop = {"fc", "cf", "sc", "ac", "as", "sv", "fk", "sk", "bk", "if", "nk", "nk", "cd", "ud", "sd", "rc", "rcd", "afc", "cfc", "ssc", "us", "ss"}
    parts = [p for p in slug.split("-") if p and p not in drop]
    return "-".join(parts) if parts else slug


def parse_sitemap_catalog(xml_text: str) -> tuple[dict[str, str], dict[str, str], dict[str, str]]:
    """Return (slug -> 700x700 URL, stripped_slug -> URL, national_slug -> URL)."""
    by_slug: dict[str, str] = {}
    by_stripped: dict[str, str] = {}
    national_by_country: dict[str, str] = {}

    urls = re.findall(r"<image:loc>([^<]+)</image:loc>", xml_text)
    for u in urls:
        # Prefer 700x700 PNGs (lightweight ~30-50 KB), skip --white/--no-text/--unofficial variants
        if "/700x700/" not in u or "--" in u:
            continue
        m = re.search(r"/logos/([^/]+)/700x700/([^./]+)\.[0-9a-f]+\.png$", u)
        if not m:
            continue
        cat, slug = m.group(1), m.group(2)
        if slug not in by_slug:
            by_slug[slug] = u
        st = strip_club_tokens(slug)
        if st not in by_stripped:
            by_stripped[st] = u
        if slug.endswith("-national-team"):
            c_key = slug[: -len("-national-team")]
            national_by_country[c_key] = u
        elif slug == cat:
            national_by_country.setdefault(cat, u)

    return by_slug, by_stripped, national_by_country


def scan_swos_teams() -> list[tuple[str, str]]:
    teams: list[tuple[str, str]] = []
    if not DATA_DIR.is_dir():
        return teams
    for tf in sorted(DATA_DIR.glob("team.*")):
        b = tf.read_bytes()
        if len(b) < 2:
            continue
        cnt = b[1]
        off = 2
        for _ in range(cnt):
            if off + 684 > len(b):
                break
            raw_name = b[off + 5 : off + 21]
            name = raw_name.split(b"\x00", 1)[0].decode("ascii", "ignore").strip()
            if name:
                teams.append((tf.name.upper(), name))
            off += 684
    return teams


def resolve_team_url(
    team_file: str,
    team_name: str,
    by_slug: dict[str, str],
    by_stripped: dict[str, str],
    national_by_country: dict[str, str],
) -> tuple[str, str] | None:
    upper = team_name.upper().strip()
    is_national = team_file in ("TEAM.080", "TEAM.081", "TEAM.082")

    if is_national:
        nslug = slugify(upper)
        nat_aliases = {
            "usa": "united-states",
            "rep-of-ireland": "republic-of-ireland",
            "n-ireland": "northern-ireland",
            "faroe-isles": "faroe-islands",
            "bosnia": "bosnia-and-herzegovina",
            "south-korea": "korea-republic",
            "ivory-coast": "cote-divoire",
            "holland": "netherlands",
            "yugoslavia": "serbia",
            "zaire": "dr-congo",
        }
        nslug = nat_aliases.get(nslug, nslug)
        if nslug in national_by_country:
            return (f"nat-{nslug}", national_by_country[nslug])
        if f"{nslug}-national-team" in by_slug:
            return (f"nat-{nslug}", by_slug[f"{nslug}-national-team"])

    if upper in SWOS_EXACT_SLUGS:
        target = SWOS_EXACT_SLUGS[upper]
        if target in by_slug:
            return (target, by_slug[target])

    sl = slugify(upper)
    if sl in by_slug:
        return (sl, by_slug[sl])

    # Expand common SWOS abbreviations
    expanded = sl.replace("-utd", "-united").replace("nottm-", "nottingham-").replace("-wed", "-wednesday")
    if expanded in by_slug:
        return (expanded, by_slug[expanded])

    st = strip_club_tokens(expanded)
    if st in by_stripped and len(st) >= 4:
        return (st, by_stripped[st])

    return None


def download_and_resize_64(url: str, dest: Path) -> bool:
    if dest.is_file() and dest.stat().st_size > 200:
        return False  # already present
    req = urllib.request.Request(url, headers=HEADERS)
    with urllib.request.urlopen(req, timeout=25) as r:
        raw = r.read()
    im = Image.open(io.BytesIO(raw)).convert("RGBA")
    im.thumbnail((64, 64), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    ox = (64 - im.width) // 2
    oy = (64 - im.height) // 2
    canvas.paste(im, (ox, oy), im)
    dest.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(dest, format="PNG", optimize=True)
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true", help="Print match counts without downloading")
    ap.add_argument("--delay", type=float, default=0.30, help="Polite delay in seconds between downloads")
    ap.add_argument("--limit", type=int, default=0, help="Optional cap on number of downloads (0 = all matched)")
    args = ap.parse_args()

    xml = fetch_sitemap()
    by_slug, by_stripped, national_by_country = parse_sitemap_catalog(xml)
    print(f"Catalog loaded: {len(by_slug)} slugs, {len(national_by_country)} national teams")

    teams = scan_swos_teams()
    manifest_rows: list[tuple[str, str]] = []
    download_queue: dict[Path, str] = {}

    matched_clubs = 0
    matched_nations = 0
    for tf, name in teams:
        res = resolve_team_url(tf, name, by_slug, by_stripped, national_by_country)
        if res:
            out_slug, url = res
            rel = f"clubs/{out_slug}.png"
            manifest_rows.append((name.upper(), rel))
            download_queue[CLUBS_DIR / f"{out_slug}.png"] = url
            if out_slug.startswith("nat-"):
                matched_nations += 1
            else:
                matched_clubs += 1

    matched_leagues = 0
    for lkey, candidates in LEAGUE_TARGETS.items():
        for cand in candidates:
            if cand in by_slug:
                download_queue[LEAGUES_DIR / f"{lkey}.png"] = by_slug[cand]
                matched_leagues += 1
                break

    print(f"SWOS teams scanned: {len(teams)}")
    print(f"  Matched clubs:    {matched_clubs}")
    print(f"  Matched nations:  {matched_nations}")
    print(f"  Matched leagues:  {matched_leagues} / {len(LEAGUE_TARGETS)}")
    print(f"  Unique PNG files: {len(download_queue)}")

    if args.dry_run:
        return 0

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    CLUBS_DIR.mkdir(parents=True, exist_ok=True)
    LEAGUES_DIR.mkdir(parents=True, exist_ok=True)

    # Write manifest first so C++ can map every matched SWOS team name directly
    seen = set()
    lines = []
    for k, rel in manifest_rows:
        if k not in seen:
            seen.add(k)
            lines.append(f"{k}|{rel}")
    MANIFEST_PATH.write_text("\n".join(sorted(lines)) + "\n", encoding="utf-8")

    downloaded = 0
    skipped = 0
    errors = 0
    total = len(download_queue)
    for idx, (dest, url) in enumerate(sorted(download_queue.items()), 1):
        if args.limit > 0 and downloaded >= args.limit:
            break
        try:
            did = download_and_resize_64(url, dest)
            if did:
                downloaded += 1
                if downloaded % 25 == 0 or downloaded <= 5:
                    print(f"[{idx}/{total}] Downloaded {dest.parent.name}/{dest.name} (total new={downloaded})", flush=True)
                time.sleep(args.delay)
            else:
                skipped += 1
        except Exception as e:
            errors += 1
            print(f"[WARN] {dest.name}: {e}", flush=True)
            time.sleep(args.delay)

    print(f"Done! downloaded={downloaded}, skipped_existing={skipped}, errors={errors}, manifest={len(lines)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
