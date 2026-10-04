"""Compare build 112 and current compiled Manager operations with bounded stubs.

Runs Champollion assembly through the existing instruction interpreter. Records
engine-facing effects and dispatch counts; it does not emulate live scheduling.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import sys
from collections import Counter
from pathlib import Path

spec = importlib.util.spec_from_file_location("component_payment", Path(__file__).with_name("Test-ClipboardComponentPayment.py"))
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
CompiledManager, Ref = module.CompiledManager, module.Ref


class World:
    stub_self = {"showfinisheddialogs", "showpluginlist"}

    def __init__(self, count=0, slot=1):
        self.self_ref = Ref("manager")
        self.tool, self.menus = Ref("tool"), Ref("menus")
        self.fields = {"referenceobject": self.tool, "::clipboard_menus_var": self.menus}
        self.objects = [Ref(str(i)) for i in range(count)]
        self.center = [11.25, -7.5, 4.0]
        self.slot = slot
        self.events, self.effects = [], []
        self.counts = Counter()

    def method(self, receiver, name, args):
        name = name.lower()
        if name == "showfinisheddialogs": return False
        if name == "showslotselectionmenu": return self.slot
        if name in ("setmotiontype", "setactorowner"):
            self.effects.append((name, receiver.name, args))
            return None
        if name in ("showprogressupdate", "showpluginlist"):
            self.effects.append((name, args))
            return None
        raise AssertionError((receiver, name, args))

    def static(self, owner, name, args):
        name = name.lower()
        self.counts[name] += 1
        if name == "getselectedobjectreferences": return self.objects
        if name == "gettext": return "|".join(str(x) for x in args)
        if name == "getselectioncenter": return self.center
        if name == "getselectiondetails":
            return dict(zip(("centerx", "centery", "centerz"), self.center))
        if name == "getpatternplugins": return ["Fallout4.esm", "Test.esl"]
        if name == "getpatternreferenceinformation": return {}
        if name in ("rotateselectionz", "updateselectedwires", "sendworkshopeventtoselectedobjects", "wait"):
            self.effects.append((name, [x.name if isinstance(x, Ref) else x for x in args]))
            return None
        raise AssertionError((owner, name, args))


def compare(baseline, candidate):
    workloads, cases = [], 0
    for function, args in (("SetMotionTypeSelected", [4]), ("ClearOwnershipSelected", [])):
        for count in (0, 1, 127, 128, 129, 1000):
            old, new = World(count), World(count)
            baseline.run(old, function, args)
            candidate.run(new, function, args)
            assert old.effects == new.effects, (function, count)
            assert old.counts["gettext"] == count
            assert new.counts["gettext"] == 1
            assert len([e for e in new.effects if e[0] == "showprogressupdate"]) == count
            workloads.append(dict(operation=function, rows=count, oldLabelCalls=count,
                                  newLabelCalls=1, unchangedEffects=True))
            cases += 1
    for point in (None, Ref("pivot", properties={"x": -900.5, "y": 1200.25})):
        for angle in (-90.0, 0.0, 37.5):
            old, new = World(3), World(3)
            baseline.run(old, "RotateSelected", [angle, point])
            candidate.run(new, "RotateSelected", [angle, point])
            assert old.effects == new.effects
            assert old.counts["getselectiondetails"] == (point is None)
            assert new.counts["getselectiondetails"] == 0
            assert new.counts["getselectioncenter"] == (point is None)
            assert [e[0] for e in new.effects] == ["rotateselectionz", "wait", "updateselectedwires", "wait", "sendworkshopeventtoselectedobjects"]
            cases += 1
    for slot in (-1, 0, 1, 500):
        old, new = World(slot=slot), World(slot=slot)
        baseline.run(old, "ListPluginsPattern")
        candidate.run(new, "ListPluginsPattern")
        assert old.effects == new.effects
        assert old.counts["getpatternreferenceinformation"] == (slot > 0)
        assert new.counts["getpatternreferenceinformation"] == 0
        cases += 1
    # Verify the compiled import still calls the required cleanup native.
    import_code = "\n".join(candidate.functions["pastepattern"][2])
    assert "EndImportedPowerProgress" in import_code
    return cases + 1, workloads


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assembly", type=Path, required=True)
    parser.add_argument("--baseline-assembly", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    count, workloads = compare(CompiledManager(args.baseline_assembly.read_text(encoding="utf-8-sig")),
                               CompiledManager(args.assembly.read_text(encoding="utf-8-sig")))
    result = dict(result="passed", caseCount=count, workCounts=workloads,
                  assemblySha256=hashlib.sha256(args.assembly.read_bytes()).hexdigest().upper(),
                  baselineSha256=hashlib.sha256(args.baseline_assembly.read_bytes()).hexdigest().upper(),
                  livePerformanceCertified=False)
    args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
