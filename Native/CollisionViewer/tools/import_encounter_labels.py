"""Import only explicit, certain Name in File mappings from a local HTML export.

Usage: python import_encounter_labels.py document.html
Download the supplied Google Doc's HTML export manually; this script has no network access.
Ambiguous/missing/question-mark names are deliberately excluded.
"""
from html.parser import HTMLParser
from pathlib import Path
import json
import re
import sys

SOURCE = "https://docs.google.com/document/d/15BZ6j8a0nQukOvY93aHMsUO2MStfqPIX33Rl-8CGRR8/edit"
# Level resource names verified against the installed game's base/game folders.
MAPS = {
    "Fortress of Doom": "hub", "Hell on Earth": "e1m1_intro", "Exultia": "e1m2_battle",
    "Cultist Base": "e1m3_cult", "Doom Hunter Base": "e1m4_boss", "Super Gore Nest": "e2m1_nest",
    "ARC Complex": "e2m2_base", "Mars Core": "e2m3_core", "Taras Nabad": "e3m1_slayer",
    "Nekravol Part 1": "e3m2_hell", "Nekravol Part 2": "e3m2_hell_b", "Urdak": "e3m3_maykr",
    "Final Sin": "e3m4_boss", "UAC Atlantica": "e4m1_rig", "Blood Swamps": "e4m2_swamp",
    "The Holt": "e4m3_mcity", "The World Spear": "e5m1_spear", "Reclaimed Earth": "e5m2_earth",
    "Immora": "e5m3_hell",
}

class Blocks(HTMLParser):
    def __init__(self, html):
        super().__init__()
        self.blocks, self.tag, self.parts = [], None, []
        self.white_classes = {name for name, body in re.findall(r"\.([\w-]+)\{([^}]+)\}", html)
                              if re.search(r"(?:^|;)\s*color:\s*(?:#fff(?:fff)?|white)(?:;|$)", body, re.I)}
        self.spans, self.uncertain = [], False
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        white = bool(set(attrs.get("class", "").split()) & self.white_classes) or bool(
            re.search(r"(?:^|;)\s*color:\s*(?:#fff(?:fff)?|white)(?:;|$)", attrs.get("style", ""), re.I))
        if tag in ("h1", "h2", "h3", "h4", "p"):
            self.tag, self.parts = tag, []
            self.spans, self.uncertain = [], white
        elif tag == "span" and self.tag:
            self.spans.append(white)
    def handle_data(self, data):
        if self.tag:
            self.parts.append(data)
            if data.strip() and any(self.spans):
                self.uncertain = True
    def handle_endtag(self, tag):
        if tag == "span" and self.spans:
            self.spans.pop()
        if tag == self.tag:
            self.blocks.append((tag, " ".join("".join(self.parts).split()), self.uncertain))
            self.tag = None

def plain(text):
    text = text.replace("⚔", "Combat ").replace("⦻", "Progression ").replace("¬", "")
    return " ".join(text.replace("’", "'").replace("“", '"').replace("”", '"').replace("%", " percent ").replace("^", "").split())

def main():
    html = Path(sys.argv[1]).read_text(encoding="utf-8")
    parser = Blocks(html)
    parser.feed(html)
    level, headings, labels = "", {}, {}
    uncertain_heading = False
    for tag, text, uncertain in parser.blocks:
        if tag == "h1":
            level, headings = text, {}
            uncertain_heading = uncertain
        elif tag in ("h2", "h3", "h4"):
            depth = int(tag[1])
            headings = {d: value for d, value in headings.items() if d < depth}
            headings[depth] = plain(text)
            uncertain_heading = uncertain if depth == 2 else uncertain_heading or uncertain
        elif text.startswith("Name in File:") and level in MAPS:
            if uncertain or uncertain_heading:
                continue
            name = text.partition(":")[2].strip()
            if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
                continue
            label = level + " / " + " / ".join(headings.values())
            labels.setdefault((MAPS[level], name), set()).add(label)
    entries = [(m, n, next(iter(values))) for (m, n), values in sorted(labels.items()) if len(values) == 1]
    output = Path(__file__).resolve().parents[1] / "src" / "encounter_labels.inc"
    output.write_text("// Generated from the user-supplied encounter reference, 2026-10-26.\n// " + SOURCE + "\n" +
                      "".join("{" + ",".join(json.dumps(v, ensure_ascii=True) for v in entry) + "},\n" for entry in entries), encoding="utf-8")
    print(f"Imported {len(entries)} certain map-scoped names; uncertain/missing/ambiguous entries excluded.")

if __name__ == "__main__":
    main()
