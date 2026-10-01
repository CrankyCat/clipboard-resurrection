"""Exercise compiled ClipboardManager control flow with bounded engine stubs.

Input is Champollion .pas assembly produced from the newly compiled PEX, not a
Python copy of the production algorithms. This does not emulate engine inventory
internals or certify live Papyrus scheduling/component conversion.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path


INT_MAX = 2_147_483_647
COUNT_BATCH_SIZE = 32


@dataclass(eq=False)
class Ref:
    name: str
    kind: str = "objectreference"
    deleted: bool = False
    inventory: dict[int, int] = field(default_factory=dict)
    properties: dict = field(default_factory=dict)
    removal_limit: int | None = None
    inventory_readable: bool = True


@dataclass(eq=False)
class Form:
    form_id: int
    kind: str = "component"


class PlacementReached(Exception):
    pass


class CompiledManager:
    def __init__(self, assembly: str):
        self.functions = {}
        for name, body in re.findall(r"\.function (\w+)\s+(.*?)\.endFunction[^\n]*", assembly, re.S):
            params = re.findall(r"\.param (\S+) (\S+)", body)
            locals_ = re.findall(r"\.local (\S+) (\S+)", body)
            code_match = re.search(r"\.code\s+(.*?)\.endCode", body, re.S)
            if not code_match:
                continue
            code = [line.split(";", 1)[0].strip() for line in code_match[1].splitlines()]
            code = [line for line in code if line]
            return_type = re.search(r"\.return (\S+)", body)[1].lower()
            self.functions[name.lower()] = (params, locals_, code, return_type)

    @staticmethod
    def default(type_name):
        return {"int": 0, "float": 0.0, "bool": False, "string": ""}.get(type_name.lower())

    def run(self, world, name, args=(), depth=0):
        if depth > 32:
            raise AssertionError("Unexpected recursive control flow")
        params, locals_, code, return_type = self.functions[name.lower()]
        assert len(args) == len(params), (name, args, params)
        types = {key.lower(): typ.lower() for key, typ in params + locals_}
        values = {key: self.default(typ) for key, typ in types.items()}
        values.update({key.lower(): value for (key, _), value in zip(params, args)})
        labels = {line.rstrip(":").lower(): index for index, line in enumerate(code) if line.lower().startswith("_label")}

        def value(token):
            if token.startswith('"'):
                return token[1:-1].replace(r"\n", "\n").replace(r'\"', '"')
            key = token.lower()
            if key in ("none", "true", "false"):
                return {"none": None, "true": True, "false": False}[key]
            if re.fullmatch(r"-?\d+", token):
                return int(token)
            if re.fullmatch(r"-?\d+\.\d+(?:e[+-]?\d+)?", token, re.I):
                return float(token)
            if key == "self":
                return world.self_ref
            if key in values:
                return values[key]
            if key in world.fields:
                return world.fields[key]
            raise AssertionError((name, "unknown operand", token))

        def store(key, item):
            key = key.lower()
            if key in world.fields and key not in values:
                world.fields[key] = item
            else:
                values[key] = item

        index = 0
        for _ in range(200000):
            if index >= len(code):
                if return_type == "none":
                    return None
                raise AssertionError((name, "missing return"))
            words = shlex.split(code[index], posix=False)
            op, a = words[0].lower(), words[1:]
            if op.startswith("comp_"):
                op = "cmp_" + op[5:]
            if op.startswith("_label"):
                pass
            elif op == "assign":
                store(a[0], value(a[1]))
            elif op == "cast":
                item, typ = value(a[1]), types.get(a[0].lower())
                if typ == "component":
                    item = item if isinstance(item, Form) and item.kind == "component" else None
                elif typ == "bool":
                    item = item is not None if isinstance(item, (Ref, Form, list, dict)) else bool(item)
                elif typ == "string":
                    item = "" if item is None else str(item)
                elif typ == "int":
                    item = int(item or 0)
                elif typ == "float":
                    item = float(item or 0)
                store(a[0], item)
            elif op == "not":
                store(a[0], not value(a[1]))
            elif op in ("ineg", "fneg"):
                store(a[0], -value(a[1]))
            elif op.startswith("cmp_"):
                left, right = value(a[1]), value(a[2])
                operations = {"cmp_eq": lambda: left == right, "cmp_lt": lambda: left < right,
                              "cmp_le": lambda: left <= right, "cmp_gt": lambda: left > right,
                              "cmp_ge": lambda: left >= right, "cmp_lte": lambda: left <= right,
                              "cmp_gte": lambda: left >= right}
                store(a[0], operations[op]())
            elif op in ("iadd", "isub", "imul", "idiv", "imod", "fadd", "fsub", "fmul", "fdiv", "strcat"):
                left, right = value(a[1]), value(a[2])
                if op.endswith("add") or op == "strcat": result = left + right
                elif op.endswith("sub"): result = left - right
                elif op.endswith("mul"): result = left * right
                elif op == "idiv": result = int(left / right)
                elif op == "imod": result = left % right
                else: result = left / right
                store(a[0], result)
            elif op in ("jmp", "jmpf", "jmpt"):
                take = op == "jmp" or (op == "jmpf" and not value(a[0])) or (op == "jmpt" and value(a[0]))
                if take:
                    index = labels[a[-1].lower()]
                    continue
            elif op == "array_create":
                array_type = types.get(a[0].lower(), "")
                element_type = array_type[:-2] if array_type.endswith("[]") else ""
                store(a[0], [self.default(element_type)] * value(a[1]))
            elif op == "array_length":
                store(a[0], len(value(a[1]) or []))
            elif op in ("array_getlement", "array_getelement"):
                store(a[0], value(a[1])[value(a[2])])
            elif op == "array_setelement":
                value(a[0])[value(a[1])] = value(a[2])
            elif op == "array_add":
                value(a[0]).extend([value(a[1])] * value(a[2]))
            elif op == "array_findelement":
                # Papyrus assembly order is array, destination, item, start.
                array, needle, start = value(a[0]), value(a[2]), value(a[3])
                found = next((i for i in range(start, len(array)) if array[i] == needle), -1)
                store(a[1], found)
            elif op == "struct_get":
                obj = value(a[1])
                store(a[0], obj[a[2].lower()])
            elif op == "propget":
                receiver = value(a[1])
                if receiver is world.self_ref:
                    result = world.fields.get("::" + a[0].lower() + "_var")
                else:
                    assert receiver is not None, (name, "property on None", a)
                    result = receiver.properties[a[0].lower()]
                store(a[2], result)
            elif op == "callmethod":
                method, receiver, dest = a[0], value(a[1]), a[2]
                call_args = [value(token) for token in a[3:]]
                world.events.append(("call", method.lower()))
                if receiver is world.self_ref and method.lower() in self.functions and method.lower() not in world.stub_self:
                    result = self.run(world, method, call_args, depth + 1)
                else:
                    result = world.method(receiver, method, call_args)
                store(dest, result)
            elif op == "callstatic":
                result = world.static(a[0], a[1], [value(token) for token in a[3:]])
                store(a[2], result)
            elif op == "return":
                return value(a[0])
            else:
                raise AssertionError((name, "unhandled instruction", code[index]))
            index += 1
        raise AssertionError((name, "instruction limit"))


class World:
    stub_self = {"clearselection", "clearimportprogress", "stopdestroyedimport", "enterworkshopmodeifneeded", "getselectedcount", "showfinisheddialogs"}

    def __init__(self):
        self.self_ref = Ref("manager")
        self.player = Ref("player", "actor")
        self.container = Ref("local")
        self.location = Ref("location")
        self.workshop = Ref("workshop", "workshopscript", properties={"mylocation": self.location, "container": self.container})
        self.parent = Ref("parent")
        self.menus = Ref("menus")
        self.fields = {"workshopref": self.workshop, "referenceobject": Ref("tool"), "isdestroyed": False,
                       "importinprogress": False, "destroyinprogress": False,
                       "exportinputinprogress": False, "exportrequestgeneration": 0,
                       "::clipboard_menus_var": self.menus, "::workshopparent_var": self.parent,
                       "::workshopcaravankeyword_var": Ref("caravan")}
        self.links = []
        self.workshops = {}
        self.forms = {1: Form(1), 2: Form(2, "form")}
        self.components = [{"name": "Steel", "formid": 1, "count": 4}]
        self.events = []
        self.costs_enabled = True
        self.approved = True
        self.plugin_approved = True
        self.plugins = ["Fallout4.esm"]
        self.plugin_count = 1
        self.installed_plugins = {"Fallout4.esm"}
        self.save_result = {"objectcount": 2}
        self.selected_count = 5
        self.old_object_count = 0
        self.after_clear = None
        self.pattern_name = "Test export"
        self.after_name = None
        self.after_approval = None
        self.after_remove = None
        self.after_discovery = None
        self.logging_enabled = False
        self.clock = 0.0

    def count_inventory(self, receiver, component_form):
        if receiver is None or receiver.deleted or component_form is None:
            return -1
        count = receiver.inventory.get(component_form.form_id, 0) if receiver.inventory_readable else -1
        self.events.append(("inventory-query", receiver.name, component_form.form_id,
                            component_form.kind == "component", count))
        # A single-source before/after count must be exactly representable.
        # Only aggregates may saturate: clamping a source could hide a real
        # withdrawal and incorrectly charge a fallback source as well.
        return count if 0 <= count <= INT_MAX else -1

    def method(self, receiver, name, args):
        name = name.lower()
        assert receiver is not None, ("method called on None", name)
        if name == "isdeleted": return receiver.deleted
        if name == "getalllinkedlocations":
            if self.after_discovery:
                self.after_discovery()
            return self.links
        if name == "getworkshopfromlocation": return self.workshops.get(args[0])
        if name == "getcontainer": return receiver.properties.get("container")
        if name in ("getitemcount", "getcomponentcount"):
            assert args[0] is not None, "Inventory count called with missing form"
            assert (name == "getcomponentcount") == (args[0].kind == "component")
            return max(0, self.count_inventory(receiver, args[0]))
        if name in ("removeitem", "removecomponents"):
            assert args[0] is not None
            key, requested = args[0].form_id, args[1]
            actual = min(requested, receiver.inventory.get(key, 0))
            if receiver.removal_limit is not None:
                actual = min(actual, receiver.removal_limit)
            receiver.inventory[key] = receiver.inventory.get(key, 0) - actual
            self.events.append(("remove", receiver.name, key, requested, actual))
            if self.after_remove:
                self.after_remove(receiver, key, requested, actual)
            return None
        if name == "showprogressupdate": return None
        if name == "showinformmenu":
            self.events.append(("message", args))
            return None
        if name == "showyesnomenu":
            self.events.append(("approval", args))
            if self.after_approval:
                self.after_approval()
            return self.approved
        if name == "showmissingpluginsmenu": return self.plugin_approved
        if name == "showslotselectionmenu": return 1
        if name == "isinputownervalid": return args[0] is not None and not args[0].deleted
        if name == "showpatternnamemenuforowner":
            assert args[0] is self.fields["referenceobject"]
            if self.after_name: self.after_name()
            return self.pattern_name
        if name == "registerforremoteevent": return None
        if name == "getbaseobject": return Ref("playerbase")
        if name == "getname": return receiver.name
        if name == "getselectedcount": return self.selected_count
        if name == "showfinisheddialogs": return True
        if name == "stopdestroyedimport": return self.fields["isdestroyed"]
        if name == "clearselection" and self.after_clear:
            self.after_clear()
        if name in self.stub_self: return None
        raise AssertionError(("unhandled engine method", name, args))

    def static(self, namespace, name, args):
        name = name.lower()
        self.events.append(("static", name))
        if name == "gettext": return "|".join(map(str, args))
        if name == "getplayer": return self.player
        if name == "getform": return self.forms.get(args[0])
        if name == "countcomponentsource":
            component_form, receiver = args
            return self.count_inventory(receiver, component_form)
        if name == "countcomponentsources":
            component_form, sources, start = args
            assert start == 0 and len(sources or []) <= COUNT_BATCH_SIZE, args
            batch = (sources or [])[start:start + COUNT_BATCH_SIZE]
            self.events.append(("count-batch", start, len(batch)))
            return min(INT_MAX, sum(max(0, self.count_inventory(source, component_form)) for source in batch))
        if name == "getcurrentrealtime":
            assert self.logging_enabled, "Component timing clock must be disabled with diagnostics Off"
            self.clock += 0.25
            return self.clock
        if name == "reportcomponentcosttiming":
            assert self.logging_enabled, "Component timing report must be disabled with diagnostics Off"
            assert len(args) == 4 and args[1] >= 0 and args[2] >= 0 and args[3] >= 0, args
            self.events.append(("timing", *args))
            return None
        if name == "getsettingvaluebool":
            if args[1] == "bEnableComponentCost": return self.costs_enabled
            if args[1] == "bEnableLogging": return self.logging_enabled
            raise AssertionError(("unexpected setting", args))
        if name == "getpatterngeneralinformation":
            return {"objectcount": self.old_object_count if self.saving else 12, "plugincount": self.plugin_count}
        if name == "getpatternplugins": return self.plugins
        if name == "isplugininstalled": return args[0] in self.installed_plugins
        if name == "getpatterncomponentcost": return [dict(x) for x in self.components]
        if name == "pastepatternobjectswithreuse": raise PlacementReached()
        if name == "writepatternfile": return self.save_result
        if name == "logissue":
            self.events.append(("issue", args))
            return None
        if name in ("wait", "trace"): return None
        raise AssertionError(("unhandled static", namespace, name, args))

    saving = False


def check_suite(manager):
    cases = []

    def checked(name, fn):
        fn()
        cases.append(name)

    def removals(world):
        return [event for event in world.events if event[0] == "remove"]

    def source_case():
        w = World()
        remote = Ref("remote")
        invalid = Ref("deleted", deleted=True)
        locations = [Ref(str(i)) for i in range(6)]
        w.links = [None] + locations + [locations[0]]
        w.workshops = {locations[0]: Ref("remote-workshop", properties={"container": remote}),
                       locations[1]: w.workshop,
                       locations[2]: None,
                       locations[3]: Ref("deleted-workshop", deleted=True),
                       locations[4]: Ref("no-container", properties={"container": None}),
                       locations[5]: Ref("bad-container", properties={"container": invalid})}
        actual = manager.run(w, "GetComponentSources")
        assert actual == [w.container, remote, w.player], actual
    checked("unique valid sources preserve local-linked-player order", source_case)

    def absent_workshop():
        w = World(); w.fields["workshopref"] = None
        assert manager.run(w, "GetComponentSources") == [w.player]
    checked("missing workshop still permits valid player source", absent_workshop)

    def confirm_case(available, approval, expected):
        w = World(); w.container.inventory[1] = available; w.approved = approval
        assert manager.run(w, "ConfirmComponentCost", [w.components]) is expected
        assert w.container.inventory[1] == available and not removals(w)
    checked("confirmation never consumes", lambda: confirm_case(10, True, True))
    checked("cancelled approval never consumes", lambda: confirm_case(10, False, False))
    checked("insufficient preview never consumes", lambda: confirm_case(2, True, False))

    def missing_form():
        w = World(); w.forms = {}; w.container.inventory[1] = 100
        assert manager.run(w, "ConfirmComponentCost", [w.components]) is False
        assert not removals(w)
    checked("unresolved required form cannot charge unrelated inventory", missing_form)

    def changed_balance():
        w = World(); w.container.inventory[1] = 5
        assert manager.run(w, "ConfirmComponentCost", [w.components]) is True
        w.container.inventory[1] = 1
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is False
        assert w.container.inventory[1] == 1 and not removals(w)
    checked("changed affordability is rechecked before any payment", changed_balance)

    def changed_topology(kind):
        w = World()
        remote = Ref("remote", inventory={1: 4})
        location = Ref("remote-location")
        linked = Ref("remote-workshop", properties={"container": remote})
        replacement = Ref("replacement", inventory={1: 4})
        w.workshops[location] = linked
        if kind == "unlink":
            w.links = [location]
            w.after_approval = lambda: w.links.clear()
        elif kind == "link":
            w.container.inventory[1] = 4
            def mutate():
                w.container.inventory[1] = 0
                w.links.append(location)
            w.after_approval = mutate
        elif kind == "replace-container":
            w.container.inventory[1] = 4
            w.after_approval = lambda: w.workshop.properties.update(container=replacement)
        else:
            raise AssertionError(kind)
        assert manager.run(w, "ConfirmComponentCost", [w.components]) is True
        assert not removals(w)
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is (kind != "unlink")
        if kind == "unlink":
            assert not removals(w) and remote.inventory[1] == 4
        elif kind == "link":
            assert [(e[1], e[4]) for e in removals(w)] == [("remote", 4)]
        else:
            assert [(e[1], e[4]) for e in removals(w)] == [("replacement", 4)]
            assert w.container.inventory[1] == 4
    for kind in ("unlink", "link", "replace-container"):
        checked("payment rediscovers topology after approval: " + kind, lambda kind=kind: changed_topology(kind))

    def later_component_missing():
        w = World(); w.container.inventory = {1: 10, 2: 0}
        costs = w.components + [{"name": "Item", "formid": 2, "count": 2}]
        assert manager.run(w, "ConsumeComponentCost", [costs]) is False
        assert not removals(w) and w.container.inventory[1] == 10
    checked("all component balances are checked before the first removal", later_component_missing)

    def split_payment():
        w = World(); w.container.inventory = {1: 2, 2: 1}; w.player.inventory = {1: 5, 2: 2}
        costs = w.components + [{"name": "Item", "formid": 2, "count": 2}]
        assert manager.run(w, "ConsumeComponentCost", [costs]) is True
        assert w.container.inventory == {1: 0, 2: 0} and w.player.inventory == {1: 3, 2: 1}
        assert [(e[1], e[2], e[4]) for e in removals(w)] == [("local", 1, 2), ("player", 1, 2), ("local", 2, 1), ("player", 2, 1)]
    checked("component and ordinary item withdrawals respect source priority", split_payment)

    def short_payment():
        w = World(); w.container.inventory[1] = 5; w.container.removal_limit = 1
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is False
        assert w.container.inventory[1] == 4
        assert any(e[0] == "message" and "$Clipboard_ComponentPaymentIncomplete" in e[1][0] for e in w.events)
    checked("short native removal reports incomplete payment", short_payment)

    def failed_source_with_fallback():
        w = World(); w.container.inventory[1] = 5; w.container.removal_limit = 0
        w.player.inventory[1] = 4
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is True
        assert w.container.inventory[1] == 5 and w.player.inventory[1] == 0
    checked("uncredited source removal may be completed by the next valid source", failed_source_with_fallback)

    def removed_source_deleted():
        w = World(); w.container.inventory[1] = 4
        w.after_remove = lambda receiver, *_: setattr(receiver, "deleted", True)
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is False
        assert removals(w) == [("remove", "local", 1, 4, 4)]
        assert any(e[0] == "message" and "$Clipboard_ComponentPaymentIncomplete" in e[1][0] for e in w.events)
    checked("deleted source after withdrawal cannot claim an unverified payment", removed_source_deleted)

    def unverified_post_withdrawal(kind, fallback):
        w = World(); w.container.inventory[1] = 4
        if fallback:
            w.player.inventory[1] = 4
        def mutate(receiver, key, _requested, _actual):
            if kind == "query-failed":
                receiver.inventory_readable = False
            elif kind == "overflow":
                receiver.inventory[key] = INT_MAX + 1
            elif kind == "deleted":
                receiver.deleted = True
            else:
                raise AssertionError(kind)
        w.after_remove = mutate
        reached = False
        try:
            manager.run(w, "PastePattern", [w.fields["referenceobject"], True])
        except PlacementReached:
            reached = True
        assert not reached, "An unverified withdrawal must stop before placement"
        assert removals(w) == [("remove", "local", 1, 4, 4)]
        assert w.player.inventory.get(1, 0) == (4 if fallback else 0)
        first_remove = next(i for i, event in enumerate(w.events) if event[0] == "remove")
        assert not any(e[0] == "inventory-query" and e[1] == "player" for e in w.events[first_remove + 1:])
        assert any(e[0] == "issue" and "post-withdrawal inventory could not be verified" in e[1][0] for e in w.events)
        assert any(e[0] == "message" and "$Clipboard_ComponentPaymentIncomplete" in e[1][0] for e in w.events)
    for kind in ("query-failed", "overflow", "deleted"):
        for fallback in (False, True):
            checked("unverified post-withdrawal " + kind + (" with fallback" if fallback else " without fallback"),
                    lambda kind=kind, fallback=fallback: unverified_post_withdrawal(kind, fallback))

    def balance_restored_during_removal():
        w = World(); w.container.inventory[1] = 4
        w.after_remove = lambda receiver, key, _requested, _actual: receiver.inventory.update({key: 4})
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is False
        assert w.container.inventory[1] == 4
        reads = [e[4] for e in w.events if e[0] == "inventory-query" and e[1:3] == ("local", 1)]
        assert reads == [4, 4, 4], reads
    checked("withdrawal credit uses a fresh post-removal inventory read", balance_restored_during_removal)

    def cross_component_mutation(kind):
        w = World()
        w.forms[2] = Form(2)
        w.container.inventory = {1: 4, 2: 8 if kind == "loss" else 4}
        costs = w.components + [{"name": "Wood", "formid": 2, "count": 4}]
        def mutate(receiver, key, _requested, _actual):
            if key == 1:
                receiver.inventory[2] = 2 if kind == "loss" else 6
        w.after_remove = mutate
        assert manager.run(w, "ConsumeComponentCost", [costs]) is (kind == "leftovers")
        reads = [e[4] for e in w.events if e[0] == "inventory-query" and e[1:3] == ("local", 2)]
        assert reads == ([8, 2, 0] if kind == "loss" else [4, 6, 2]), reads
        assert removals(w)[1][3:] == ((2, 2) if kind == "loss" else (4, 4))
    for kind in ("loss", "leftovers"):
        checked("next component re-reads inventory after junk conversion: " + kind,
                lambda kind=kind: cross_component_mutation(kind))

    def batched_counts():
        w = World()
        sources = [Ref(f"source-{i}", inventory={1: i + 1, 2: 2 * i}) for i in range(67)]
        costs = w.components + [{"name": "Item", "formid": 2, "count": 1}]
        assert manager.run(w, "GetAvailableComponentCounts", [costs, sources]) == [2278, 4422]
        batches = [e[1:] for e in w.events if e[0] == "count-batch"]
        assert batches == [(0, 32), (0, 32), (0, 32), (0, 32), (0, 3), (0, 3)], batches
        queries = [e for e in w.events if e[0] == "inventory-query"]
        assert len(queries) == 134
        for component in (1, 2):
            assert [e[1] for e in queries if e[2] == component] == [source.name for source in sources]
        assert not any(e[0] == "call" and e[1] in ("getitemcount", "getcomponentcount") for e in w.events)
    checked("67 sources use bounded 32-source batches without omitted or repeated rows", batched_counts)

    def missing_batch_form():
        w = World(); w.forms.clear()
        sources = [Ref(f"source-{i}", inventory={1: 100}) for i in range(35)]
        assert manager.run(w, "GetAvailableComponentCounts", [w.components, sources]) == [0]
        assert not any(e[0] == "inventory-query" for e in w.events)
        assert manager.run(w, "CountComponentInSource", [sources[0], None]) == 0
    checked("unresolved batched form produces zero without querying unrelated inventory", missing_batch_form)

    def unavailable_source():
        w = World(); deleted = Ref("deleted", deleted=True, inventory={1: 5})
        assert manager.run(w, "CountComponentInSource", [None, w.forms[1]]) == 0
        assert manager.run(w, "CountComponentInSource", [deleted, w.forms[1]]) == 0
        assert manager.run(w, "GetAvailableComponentCounts", [w.components, [None, deleted]]) == [0]
        assert not any(e[0] == "inventory-query" for e in w.events)
    checked("unavailable and deleted sources contribute zero", unavailable_source)

    def empty_count_inputs():
        w = World()
        assert manager.run(w, "GetAvailableComponentCounts", [w.components, []]) == [0]
        assert manager.run(w, "GetAvailableComponentCounts", [[], [w.container]]) == []
        assert not any(e[0] in ("count-batch", "inventory-query") for e in w.events)
    checked("empty source or component lists return initialized totals without native counts", empty_count_inputs)

    def saturated_counts(kind):
        w = World()
        sources = [Ref(f"source-{i}", inventory={1: 0}) for i in range(35)]
        sources[0].inventory[1] = INT_MAX - 5
        sources[1 if kind == "within-batch" else 33].inventory[1] = 10
        assert manager.run(w, "GetAvailableComponentCounts", [w.components, sources]) == [INT_MAX]
    for kind in ("within-batch", "across-batches"):
        checked("large availability saturates instead of wrapping: " + kind,
                lambda kind=kind: saturated_counts(kind))

    def unrepresentable_source(fallback):
        w = World(); w.container.inventory[1] = INT_MAX + 4
        if fallback:
            w.player.inventory[1] = 4
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is fallback
        assert w.container.inventory[1] == INT_MAX + 4
        assert removals(w) == ([("remove", "player", 1, 4, 4)] if fallback else [])
    for fallback in (False, True):
        checked("unrepresentable single-source count fails closed" + (" with valid fallback" if fallback else ""),
                lambda fallback=fallback: unrepresentable_source(fallback))

    def component_diagnostics(enabled):
        w = World(); w.container.inventory[1] = 10; w.logging_enabled = enabled
        assert manager.run(w, "ConfirmComponentCost", [w.components]) is True
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is True
        clock_calls = w.events.count(("static", "getcurrentrealtime"))
        reports = [e for e in w.events if e[0] == "timing"]
        if enabled:
            assert clock_calls >= 2 and reports, (clock_calls, reports)
        else:
            assert clock_calls == 0 and not reports
    for enabled in (False, True):
        checked("component phase diagnostics " + ("On" if enabled else "Off"),
                lambda enabled=enabled: component_diagnostics(enabled))

    def display_limit():
        w = World()
        w.forms = {index: Form(index) for index in range(1, 21)}
        costs = [{"name": f"Material {index:02}", "formid": index, "count": 4} for index in range(1, 21)]
        w.container.inventory = {index: 5 for index in range(1, 20)}
        manager.run(w, "ShowComponentCost", [costs])
        queries = [e for e in w.events if e[0] == "inventory-query"]
        assert len(queries) == 60 and {e[2] for e in queries} == set(range(1, 21)), queries
        messages = [e[1][0] for e in w.events if e[0] == "message"]
        assert len(messages) == 1 and "[Material 18]" in messages[0] and "[Material 19]" not in messages[0]
        assert "$Clipboard_MoreComponentsLine|2" in messages[0]
        assert "$Clipboard_ComponentAvailableAll" in messages[0]
        w.events.clear()
        assert manager.run(w, "ConfirmComponentCost", [costs]) is False
        assert any(e[0] == "inventory-query" and e[2] == 20 for e in w.events)
        assert not any(e[0] in ("approval", "remove") for e in w.events)
    checked("hidden shortage triggers all-source lookup while display remains limited to 18 rows", display_limit)

    def local_fast_path(operation):
        w = World(); w.container.inventory[1] = 10
        location = Ref("remote-location")
        remote = Ref("remote", inventory={1: 100})
        w.links = [location]
        w.workshops[location] = Ref("remote-workshop", properties={"container": remote})
        if operation == "ShowComponentCost":
            w.approved = False  # No to the explicit read-only all-sources question.
        result = manager.run(w, operation, [w.components])
        if operation != "ShowComponentCost": assert result is True
        assert ("call", "getalllinkedlocations") not in w.events
        assert ("call", "getworkshopfromlocation") not in w.events
        assert ("static", "getplayer") not in w.events
        assert {e[1] for e in w.events if e[0] == "inventory-query"} == {"local"}
        assert remote.inventory[1] == 100
        if operation != "ConsumeComponentCost":
            assert not removals(w)
            assert "$Clipboard_ComponentAvailableLocal" in str(w.events)
    for operation in ("ShowComponentCost", "ConfirmComponentCost", "ConsumeComponentCost"):
        checked("local complete bill skips network and player: " + operation,
                lambda operation=operation: local_fast_path(operation))

    def requested_all_sources():
        w = World(); w.container.inventory[1] = 10; w.player.inventory[1] = 7
        w.approved = True
        manager.run(w, "ShowComponentCost", [w.components])
        assert not removals(w)
        assert w.events.count(("call", "getalllinkedlocations")) == 1
        assert "$Clipboard_ComponentAvailableAll" in str(w.events)
        assert "4 / 17" in str(w.events)
        assert ("call", "consumecomponentcost") not in w.events
        assert ("static", "pastepatternobjectswithreuse") not in w.events
    checked("information view expands explicitly and reports exact total without payment", requested_all_sources)

    def changed_during_full_view():
        w = World(); w.container.inventory[1] = 10; w.player.inventory[1] = 7
        w.after_approval = lambda: w.container.inventory.update({1: 1})
        manager.run(w, "ShowComponentCost", [w.components])
        assert not removals(w)
        messages = [e[1][0] for e in w.events if e[0] == "message"]
        assert len(messages) == 1 and "4 / 8" in messages[0]
        assert "$Clipboard_ComponentAvailableAll" in messages[0]
    checked("explicit all-source view re-reads stock after the prompt", changed_during_full_view)

    def view_does_not_cache_payment():
        w = World(); w.container.inventory[1] = 10; w.approved = False
        manager.run(w, "ShowComponentCost", [w.components])
        w.container.inventory[1] = 1
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is False
        assert not removals(w) and w.container.inventory[1] == 1
    checked("a previous local-only view cannot authorize stale payment", view_does_not_cache_payment)

    def linked_before_player():
        w = World(); w.container.inventory[1] = 1; w.player.inventory[1] = 10
        remote = Ref("remote", inventory={1: 2}); location = Ref("remote-location")
        w.links = [location, location]
        w.workshops[location] = Ref("remote-workshop", properties={"container": remote})
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is True
        assert [(e[1], e[4]) for e in removals(w)] == [("local", 1), ("remote", 2), ("player", 1)]
        assert w.events.count(("call", "getalllinkedlocations")) == 1
    checked("local shortage preserves deduplicated local-linked-player priority", linked_before_player)

    def fast_path_short_withdrawal():
        w = World(); w.container.inventory[1] = 10; w.container.removal_limit = 1
        remote = Ref("remote", inventory={1: 2}); location = Ref("remote-location")
        w.links = [location]; w.player.inventory[1] = 5
        w.workshops[location] = Ref("remote-workshop", properties={"container": remote})
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is True
        assert [(e[1], e[4]) for e in removals(w)] == [("local", 1), ("remote", 2), ("player", 1)]
        first_remove = next(i for i, e in enumerate(w.events) if e[0] == "remove")
        discovery = w.events.index(("call", "getalllinkedlocations"))
        assert discovery > first_remove
        assert len([e for e in removals(w) if e[1] == "local"]) == 1
    checked("verified short local withdrawal expands once without retrying local", fast_path_short_withdrawal)

    def destroyed_during_late_discovery():
        w = World(); w.container.inventory[1] = 10; w.container.removal_limit = 1
        w.player.inventory[1] = 10
        w.after_discovery = lambda: w.fields.update(isdestroyed=True)
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is False
        assert removals(w) == [("remove", "local", 1, 4, 1)]
        assert w.player.inventory[1] == 10
    checked("destruction during late fallback discovery stops further deductions", destroyed_during_late_discovery)

    def local_unavailable(kind):
        w = World(); w.player.inventory[1] = 4
        if kind == "missing-workshop": w.fields["workshopref"] = None
        elif kind == "deleted-workshop": w.workshop.deleted = True
        elif kind == "missing-container": w.workshop.properties["container"] = None
        elif kind == "deleted-container": w.container.deleted = True
        elif kind == "unreadable-container": w.container.inventory_readable = False
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is True
        assert [(e[1], e[4]) for e in removals(w)] == [("player", 4)]
    for kind in ("missing-workshop", "deleted-workshop", "missing-container", "deleted-container", "unreadable-container"):
        checked("unavailable local stock uses valid full-source fallback: " + kind,
                lambda kind=kind: local_unavailable(kind))

    def empty_bill():
        w = World()
        assert manager.run(w, "ConfirmComponentCost", [[]]) is True
        assert manager.run(w, "ConsumeComponentCost", [[]]) is True
        manager.run(w, "ShowComponentCost", [[]])
        assert not removals(w)
        assert not any(e[0] == "inventory-query" for e in w.events)
        assert ("call", "getalllinkedlocations") not in w.events
        assert ("call", "getcontainer") not in w.events
    checked("empty bill does not discover or inspect inventory", empty_bill)

    def destroyed_manager():
        w = World(); w.container.inventory[1] = 10; w.fields["isdestroyed"] = True
        assert manager.run(w, "ConsumeComponentCost", [w.components]) is False
        assert not removals(w)
    checked("destroyed manager never begins payment", destroyed_manager)

    def preflight(kind):
        w = World(); w.container.inventory[1] = 10
        expected_placement = kind in ("off", "success")
        if kind == "off": w.costs_enabled = False
        elif kind == "bad-plugins": w.plugins = []
        elif kind == "plugin-cancel": w.plugins = ["Missing.esp"]; w.plugin_approved = False
        elif kind == "cost-cancel": w.approved = False
        elif kind == "changed": w.after_clear = lambda: w.container.inventory.update({1: 0})
        elif kind == "short": w.container.removal_limit = 1
        reached = False
        try:
            manager.run(w, "PastePattern", [w.fields["referenceobject"], True])
        except PlacementReached:
            reached = True
        assert reached == expected_placement, (kind, w.events)
        if kind in ("off", "bad-plugins", "plugin-cancel", "cost-cancel", "changed"):
            assert not removals(w), (kind, removals(w))
        if kind == "success":
            assert w.container.inventory[1] == 6
            first_remove = next(i for i, e in enumerate(w.events) if e[0] == "remove")
            plugin_check = w.events.index(("static", "getpatternplugins"))
            assert plugin_check < first_remove
        if kind == "off":
            assert ("static", "getpatterncomponentcost") not in w.events
        if kind == "bad-plugins":
            assert any(e[0] == "issue" and e[1][1] is True and "incomplete plugin table" in e[1][0]
                       for e in w.events), "Preflight error must be reported with bEnableLogging=False"
    for kind in ("off", "bad-plugins", "plugin-cancel", "cost-cancel", "changed", "short", "success"):
        checked("PastePattern preflight: " + kind, lambda kind=kind: preflight(kind))

    def export_case(success):
        w = World(); w.saving = True
        if not success: w.save_result = None
        result = manager.run(w, "SavePattern", [w.fields["referenceobject"]])
        assert result == (2 if success else 0), result
        messages = [e[1] for e in w.events if e[0] == "message"]
        if success:
            assert any("$Clipboard_ObjectCount|2" in str(msg) for msg in messages), messages
        else:
            assert any("$Clipboard_PatternSaveFailed" in str(msg) for msg in messages), messages
            assert not any("$Clipboard_PatternSaved" in str(msg) for msg in messages)
    checked("SavePattern reports native failure", lambda: export_case(False))
    checked("SavePattern reports actual exported count", lambda: export_case(True))
    for outcome in ("cancel", "failed", "interrupted", "destroyed", "owner-deleted", "load", "overlap"):
        def no_export(outcome=outcome):
            w = World(); w.saving = True; w.old_object_count = 3
            if outcome in ("cancel", "failed", "interrupted"): w.pattern_name = ""
            if outcome == "destroyed": w.after_name = lambda: w.fields.update(isdestroyed=True)
            if outcome == "owner-deleted": w.after_name = lambda: setattr(w.fields["referenceobject"], 'deleted', True)
            if outcome == "load": w.after_name = lambda: w.fields.update(exportrequestgeneration=w.fields["exportrequestgeneration"] + 1)
            if outcome == "overlap": w.fields["exportinputinprogress"] = True
            assert manager.run(w, "SavePattern", [w.fields["referenceobject"]]) == 0
            assert ("static", "writepatternfile") not in w.events, (outcome, w.events)
        checked("SavePattern preserves existing snapshot after input " + outcome, no_export)
    return cases


def call_count_workloads(manager):
    """Count stub boundaries, not elapsed time or live engine inventory work."""
    reports = {}
    for workload in ("availability", "local-cost-view", "approval-and-payment"):
        w = World()
        w.forms = {index: Form(index, "component" if index % 2 else "form") for index in range(1, 21)}
        w.components = [{"name": f"Material {index:02}", "formid": index, "count": 4} for index in w.forms]
        w.container.inventory = {key: 5 for key in w.forms}
        w.player.inventory = {key: 2 for key in w.forms}
        sources = [w.container]
        for index in range(34):
            source = Ref(f"linked-{index}", inventory={key: 2 for key in w.forms})
            location = Ref(f"location-{index}")
            w.links.append(location)
            w.workshops[location] = Ref(f"workshop-{index}", properties={"container": source})
            sources.append(source)
        sources.append(w.player)
        if workload == "availability":
            assert manager.run(w, "GetAvailableComponentCounts", [w.components, sources]) == [75] * 20
            assert not any(e[0] == "remove" for e in w.events)
        elif workload == "local-cost-view":
            w.approved = False
            manager.run(w, "ShowComponentCost", [w.components])
            assert not any(e[0] == "remove" for e in w.events)
            assert w.container.inventory == {key: 5 for key in w.forms}
        else:
            assert manager.run(w, "ConfirmComponentCost", [w.components]) is True
            assert manager.run(w, "ConsumeComponentCost", [w.components]) is True
            assert w.container.inventory == {key: 1 for key in w.forms}
            assert all(source.inventory == {key: 2 for key in w.forms} for source in sources[1:])
        calls = Counter(e[1] for e in w.events if e[0] in ("call", "static"))
        reports[workload] = {
            "sourceCount": len(sources), "componentCount": len(w.components),
            "inventoryQueryBoundaries": sum(calls[name] for name in (
                "getitemcount", "getcomponentcount", "countcomponentsource", "countcomponentsources")),
            "underlyingInventoryQueries": sum(e[0] == "inventory-query" for e in w.events),
            "batchedCountCalls": calls["countcomponentsources"],
            "batchedSourceHandlesPassed": sum(e[2] for e in w.events if e[0] == "count-batch"),
            "singleSourceNativeCountCalls": calls["countcomponentsource"],
            "legacyEngineCountCalls": calls["getitemcount"] + calls["getcomponentcount"],
            "sourceDiscoveryPasses": calls["getcomponentsources"],
            "workshopLookupCalls": calls["getworkshopfromlocation"],
            "containerLookupCalls": calls["getcontainer"],
            "isDeletedCalls": calls["isdeleted"],
            "withdrawalCalls": calls["removecomponents"] + calls["removeitem"],
            "progressUpdateCalls": calls["showprogressupdate"],
            "translationCalls": calls["gettext"],
            "timingClockCalls": calls["getcurrentrealtime"],
        }
        assert reports[workload]["timingClockCalls"] == 0
        assert (reports[workload]["batchedSourceHandlesPassed"]
                + reports[workload]["singleSourceNativeCountCalls"]
                + reports[workload]["legacyEngineCountCalls"]
                == reports[workload]["underlyingInventoryQueries"]), reports[workload]
    return reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assembly", required=True, type=Path)
    parser.add_argument("--baseline-assembly", type=Path,
                        help="Optional prior compiled PEX assembly for deterministic call-count comparison")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    raw = args.assembly.read_bytes()
    manager = CompiledManager(raw.decode("utf-8-sig"))
    cases = check_suite(manager)
    report = {"passed": True, "assembly": str(args.assembly.resolve()),
              "assemblySha256": hashlib.sha256(raw).hexdigest().upper(),
              "caseCount": len(cases), "cases": cases,
              "callCountWorkloads": call_count_workloads(manager),
              "scope": "Compiled Papyrus control flow with engine stubs; not live inventory or scheduling validation."}
    if args.baseline_assembly:
        baseline_raw = args.baseline_assembly.read_bytes()
        baseline = call_count_workloads(CompiledManager(baseline_raw.decode("utf-8-sig")))
        report["baseline"] = {"assembly": str(args.baseline_assembly.resolve()),
                              "assemblySha256": hashlib.sha256(baseline_raw).hexdigest().upper(),
                              "callCountWorkloads": baseline}
        for name, candidate in report["callCountWorkloads"].items():
            previous = baseline[name]
            assert candidate["underlyingInventoryQueries"] <= previous["underlyingInventoryQueries"], (name, previous, candidate)
            assert candidate["withdrawalCalls"] == previous["withdrawalCalls"], (name, previous, candidate)
            assert candidate["inventoryQueryBoundaries"] <= previous["inventoryQueryBoundaries"], (name, previous, candidate)
        report["comparisonScope"] = (
            "Deterministic counts for 36 sources and 20 components with unchanged inventory results. "
            "Counts distinguish Papyrus query boundaries from underlying reads and source discovery; "
            "they do not measure wall time, frame time or live engine junk conversion.")
    text = json.dumps(report, indent=2)
    if args.report:
        args.report.write_text(text + "\n", encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
