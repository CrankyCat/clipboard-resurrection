"""Parse labelled ClipboardObjectProbe logs; evaluate observations, never change game data.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
LINE = re.compile(r"^\[(?P<time>[^]]+)\] \[(?P<label>.*?) ref=(?P<ref>0x[0-9a-fA-F]+) t=(?P<tick>[0-9.]+)\] (?P<body>.*)$")
CATEGORIES = {"playerplaced": True, "generated": False, "spawned": True,
              "imported": True, "pluginplaced": True}


def parse(path):
    samples, unparsed = {}, []
    for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        match = LINE.match(line)
        if not match:
            if line.strip():
                unparsed.append({"line": number, "text": line})
            continue
        p = match.groupdict()
        identity = (p["label"], p["ref"].lower(), p["tick"])
        if identity not in samples:
            category = re.split(r"[_-]", p["label"])[-1].casefold()
            samples[identity] = dict(label=p["label"], reference=p["ref"], tick=p["tick"],
                category=category, desired_include=CATEGORIES.get(category),
                begin_lines=[], end_lines=[], schemas=[], first_line=number, last_line=number,
                first_time=p["time"], last_time=p["time"], fields={}, duplicates=[])
        s = samples[identity]
        s["last_line"], s["last_time"] = number, p["time"]
        body = p["body"]
        begin = re.fullmatch(r"begin schema=([12])", body, re.IGNORECASE)
        if begin:
            s["begin_lines"].append(number)
            s["schemas"].append(int(begin.group(1)))
            continue
        if "=" not in body:
            unparsed.append({"line": number, "text": line})
            continue
        key, value = body.split("=", 1)
        key = key.casefold()  # Papyrus string interning can alter display casing.
        if key == "end":
            s["end_lines"].append(number)
        if key in s["fields"]:
            s["duplicates"].append(dict(key=key, line=number, value=value))
        s["fields"][key] = dict(value=value, line=number)
    return list(samples.values()), unparsed


def field(s, key):
    return s["fields"].get(key.casefold(), {}).get("value")


def boolean(s, key):
    value = field(s, key)
    return {"true": True, "false": False}.get((value or "").casefold())


def present(s, key):
    value = field(s, key)
    return None if value is None else value.casefold() != "none"


def evaluate(samples, predicate):
    rejected, retained, unknown = [], [], []
    for s in samples:
        result = predicate(s) if s["complete"] and s["desired_include"] is not None else None
        (rejected if result is True else retained if result is False else unknown).append(s)
    return dict(rejected=[s["label"] for s in rejected], retained=[s["label"] for s in retained],
                unknown=[s["label"] for s in unknown],
                false_exclusions=[s["label"] for s in rejected if s["desired_include"] is True],
                missed_exclusions=[s["label"] for s in retained if s["desired_include"] is False])


def any_known(values):
    if any(v is True for v in values):
        return True
    return False if all(v is False for v in values) else None


def tagged(s):
    return boolean(s, "keyword.ss2.plotspawnedkeyword.onreference")


def prevent(s):
    return boolean(s, "keyword.ss2.preventexportkeyword.onreference")


def no_workshop(s):
    value = present(s, "link.workshopitem")
    return None if value is None else not value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--logs", type=Path, default=ROOT / "tmp/ClipboardProbeLogs")
    parser.add_argument("--out", type=Path, default=ROOT / "outputs/object-probe-analysis")
    args = parser.parse_args()
    out = args.out.resolve()
    if not out.is_relative_to((ROOT / "outputs").resolve()):
        parser.error("Analysis output must be beneath the checkout outputs directory.")
    probe = args.logs / "ClipboardObjectProbe.0.log"
    samples, unparsed = parse(probe)
    for s in samples:
        s["complete"] = (len(s["begin_lines"]) == 1 and len(s["end_lines"]) == 1
                         and (field(s, "end") or "").casefold() == "complete")
        s["features"] = dict(plot_spawned=tagged(s), prevent_export=prevent(s),
            created=boolean(s, "reference.created"),
            workshop_link=present(s, "link.workshopitem"),
            stacked_parent=present(s, "link.ss2.workshopstackeditemparentkeyword"),
            stacked_parent_is_plot=boolean(s, "link.ss2.workshopstackeditemparentkeyword.issimplot"),
            base_plugin=field(s, "base.originplugin"), base_id=field(s, "base.id"),
            base_local_id=field(s, "base.localid"), base_editor_id=field(s, "base.editorid"),
            papyrus_class=field(s, "base.papyrusclass"), reference_object=field(s, "reference"),
            links={k: v for k, v in s["fields"].items() if k.startswith("link.") and v["value"].casefold() != "none"})
    rules = {
        "plot_spawned_only": evaluate(samples, tagged),
        "prevent_export_only": evaluate(samples, prevent),
        "plot_spawned_or_prevent_export": evaluate(samples, lambda s: any_known([tagged(s), prevent(s)])),
        "any_stacked_parent": evaluate(samples, lambda s: present(s, "link.ss2.workshopstackeditemparentkeyword")),
        "no_workshop_link": evaluate(samples, no_workshop),
        "ss2_markers_or_no_workshop_link": evaluate(samples, lambda s: any_known([tagged(s), prevent(s), no_workshop(s)])),
    }
    files = [probe, args.logs / "Papyrus.0.log", Path(__file__).resolve()]
    report = dict(schema=1, category_policy=CATEGORIES,
        inputs=[dict(path=str(p.resolve()), bytes=p.stat().st_size,
                     sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in files],
        sample_count=len(samples), complete_count=sum(s["complete"] for s in samples),
        categories=dict(Counter(s["category"] for s in samples)), unparsed_lines=unparsed,
        rules=rules, samples=samples)
    out.mkdir(parents=True, exist_ok=True)
    (out / "analysis.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    lines = ["# Probe sample comparison", "", "User labels define the desired inclusion; rules below are offline comparisons, not implemented policy.", "",
             "| Label | Desired | Base plugin | PlotSpawned | PreventExport | Workshop link | Stacked parent | Log lines |",
             "| --- | --- | --- | --- | --- | --- | --- | --- |"]
    def cell(value):
        return "?" if value is None else str(value).replace("|", "\\|")
    for s in samples:
        f = s["features"]
        desired = "unknown" if s["desired_include"] is None else "include" if s["desired_include"] else "exclude"
        lines.append("| " + " | ".join(map(cell, [s["label"], desired,
                     f["base_plugin"], f["plot_spawned"], f["prevent_export"], f["workshop_link"],
                     f["stacked_parent"], f'{s["first_line"]}-{s["last_line"]}'])) + " |")
    lines += ["", "## Observed rule results", ""]
    for name, rule in rules.items():
        lines += [f"- {name}: {len(rule['rejected'])} rejected; false exclusions: {rule['false_exclusions']}; missed exclusions: {rule['missed_exclusions']}; unknown: {rule['unknown']}"]
    (out / "samples.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("sample_count", "complete_count", "categories", "rules")}, indent=2))


if __name__ == "__main__":
    main()
