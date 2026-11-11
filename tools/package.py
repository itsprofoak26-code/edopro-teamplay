"""Packages an EDOPro Team Play release.

Writes into --out:
  update-v<N>.zip           what the in-game updater installs over an existing copy
                            (ygoprodll.exe, ocgcore.dll, config/user_configs.json)
  EDOPro-TeamPlay-v<N>.zip  a complete first install for new players
and updates the GitHub Pages site in --site:
  check/<k>.json  for every k < N: tells a v<k> client to download update-v<N>.zip
  latest.json     the newest version and its download links
  index.html      a small download page

The client of version k fetches check/<k>.json (its URL is baked in at build time). The newest
version has no check file, so it never offers itself an update.
"""
import argparse
import hashlib
import json
import os
import subprocess
import zipfile

OWNER = "itsprofoak26-code"
# Files we ship on top of the asset pack (e.g. config/user_configs.json: image URLs and the
# CardScripts/BabelCDB repositories that keep base cards current)
PACKAGE_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "package")
UPDATES_REPO = "teamplay-updates"


def release_url(version, name):
    return f"https://github.com/{OWNER}/{UPDATES_REPO}/releases/download/v{version}/{name}"


def git_files(repo):
    out = subprocess.run(["git", "-C", repo, "ls-files", "-z"], check=True, capture_output=True).stdout
    return [p for p in out.decode("utf-8").split("\0") if p]


def add_file(zf, path, arcname):
    zf.write(path, arcname.replace("\\", "/"))


def build_update_zip(path, bin_dir, run_dir):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        add_file(zf, os.path.join(bin_dir, "ygoprodll.exe"), "ygoprodll.exe")
        add_file(zf, os.path.join(bin_dir, "ocgcore.dll"), "ocgcore.dll")
        add_file(zf, os.path.join(PACKAGE_DIR, "config", "user_configs.json"), "config/user_configs.json")


def build_install_zip(path, bin_dir, run_dir, cardscripts_dir):
    top = "EDOPro-TeamPlay/"
    added = set()

    def put(src, arc):
        arc = arc.replace("\\", "/")
        if arc in added or not os.path.isfile(src):
            return
        added.add(arc)
        zf.write(src, top + arc)

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        # Our build and settings first (they win over the asset pack's copies)
        put(os.path.join(bin_dir, "ygoprodll.exe"), "ygoprodll.exe")
        put(os.path.join(bin_dir, "ocgcore.dll"), "ocgcore.dll")
        put(os.path.join(PACKAGE_DIR, "config", "user_configs.json"), "config/user_configs.json")
        put(os.path.join(run_dir, "cacert.pem"), "cacert.pem")
        for name in sorted(os.listdir(os.path.join(run_dir, "deck"))):
            if name.endswith(".ydk"):
                put(os.path.join(run_dir, "deck", name), "deck/" + name)
        # ProjectIgnis/Distribution: the files it tracks, as committed (not our local edits)
        for rel in git_files(run_dir):
            if rel in ("config/system.conf", ".editorconfig", ".gitattributes", ".gitignore", ".gitmodules", "update.sh"):
                continue
            put(os.path.join(run_dir, rel), rel)
        system_conf = subprocess.run(["git", "-C", run_dir, "show", "HEAD:config/system.conf"],
                                     check=True, capture_output=True).stdout
        zf.writestr(top + "config/system.conf", system_conf)
        added.add("config/system.conf")
        # Base card databases and scripts (the client keeps them current from GitHub afterwards)
        exp = os.path.join(run_dir, "expansions")
        for name in sorted(os.listdir(exp)):
            if name.endswith(".cdb"):
                put(os.path.join(exp, name), "expansions/" + name)
        for rel in git_files(cardscripts_dir):
            if rel.startswith(".github/") or not rel.endswith(".lua"):
                continue
            put(os.path.join(cardscripts_dir, rel), "script/" + rel)
        puzzles = os.path.join(run_dir, "puzzles", "Canon collection")
        for rel in git_files(puzzles):
            put(os.path.join(puzzles, rel), "puzzles/Canon collection/" + rel)
    return len(added)


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def update_site(site, version, update_zip, install_zip, notes):
    os.makedirs(os.path.join(site, "check"), exist_ok=True)
    manifest = [{
        "name": os.path.basename(update_zip),
        "url": release_url(version, os.path.basename(update_zip)),
        "md5": md5(update_zip),
    }]
    for k in range(1, version):
        with open(os.path.join(site, "check", f"{k}.json"), "w", newline="\n") as f:
            json.dump(manifest, f, indent=1)
    install_url = release_url(version, os.path.basename(install_zip))
    with open(os.path.join(site, "latest.json"), "w", newline="\n") as f:
        json.dump({"version": version, "install": install_url, "update": manifest[0]["url"], "notes": notes}, f, indent=1)
    with open(os.path.join(site, ".nojekyll"), "w") as f:
        pass
    with open(os.path.join(site, "index.html"), "w", encoding="utf-8", newline="\n") as f:
        f.write(f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>EDOPro Team Play</title>
<style>body{{font-family:system-ui,sans-serif;max-width:40rem;margin:2rem auto;padding:0 1rem;line-height:1.5}}
a.button{{display:inline-block;padding:.6rem 1rem;background:#2563eb;color:#fff;border-radius:6px;text-decoration:none}}</style>
</head><body>
<h1>EDOPro Team Play</h1>
<p>A 2v2 mode for EDOPro where both teammates play at the same time.</p>
<p><a class="button" href="{install_url}">Download v{version} for Windows</a></p>
<p>Unzip it anywhere and run <code>ygoprodll.exe</code>. The game keeps itself up to date: card updates
come from Project Ignis, and Team Play updates are offered from the main menu.</p>
<p>Rules: <a href="https://github.com/{OWNER}/edopro-teamplay/blob/main/TEAMPLAY_RULES.md">TEAMPLAY_RULES.md</a>.
Source: <a href="https://github.com/{OWNER}/edopro/tree/teamplay">EDOPro fork</a>,
<a href="https://github.com/{OWNER}/ygopro-core/tree/teamplay">engine fork</a> (GNU AGPL v3).</p>
</body></html>
""")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", type=int, required=True)
    ap.add_argument("--bin", required=True, help="folder with the built ygoprodll.exe and ocgcore.dll")
    ap.add_argument("--run", required=True, help="the run/ folder (ProjectIgnis/Distribution checkout)")
    ap.add_argument("--cardscripts", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--site", required=True, help="checkout of the teamplay-updates Pages repo")
    ap.add_argument("--notes", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    update_zip = os.path.join(a.out, f"update-v{a.version}.zip")
    install_zip = os.path.join(a.out, f"EDOPro-TeamPlay-v{a.version}.zip")
    build_update_zip(update_zip, a.bin, a.run)
    count = build_install_zip(install_zip, a.bin, a.run, a.cardscripts)
    update_site(a.site, a.version, update_zip, install_zip, a.notes)
    for p in (update_zip, install_zip):
        print(f"{os.path.basename(p)}: {os.path.getsize(p) / 1e6:.1f} MB")
    print(f"install zip files: {count}")


if __name__ == "__main__":
    main()
