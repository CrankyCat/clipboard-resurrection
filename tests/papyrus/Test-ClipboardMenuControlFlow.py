"""Execute compiled menu branches with deterministic engine stubs.

Input is Champollion's assembly of the newly compiled ClipboardQuest. This is
an offline control-flow test, not a substitute for real Papyrus scheduling or
Scaleform integration. Unrecognised instructions fail closed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shlex
from decimal import Decimal, InvalidOperation
from pathlib import Path


class Program:
    def __init__(self, assembly: str):
        self.functions = {}
        for match in re.finditer(r"(?im)^\s*\.function (\S+)\s*$(.*?)^\s*\.endFunction", assembly, re.S | re.M):
            name, body = match.groups()
            params = re.findall(r"(?im)^\s*\.param (\S+) (\S+)", body)
            types = dict((k.lower(), t.lower()) for k, t in params + re.findall(r"(?im)^\s*\.local (\S+) (\S+)", body))
            code_match = re.search(r"(?ims)^\s*\.code\s*$(.*?)^\s*\.endCode", body)
            if not code_match:
                continue
            code = []
            for line in code_match[1].splitlines():
                lexer = shlex.shlex(line, posix=False)
                lexer.whitespace_split = True
                lexer.commenters = ';'
                tokens = list(lexer)
                if tokens:
                    code.append(tokens)
            labels = {line[0].rstrip(':').lower(): i for i, line in enumerate(code) if line[0].startswith('_label')}
            self.functions[name.lower()] = (params, types, code, labels)


class VM:
    def __init__(self, program: Program):
        self.program = program
        self.fields = dict(setup=True, waitingoninput=False, inputrequestactive=False, inputrequestgeneration=0, timinput='',
                           ownedinputrequestactive=False, ownedinputgeneration=0, ownedinputtoken='',
                           ownedinputowner=None, ownedinputstatechanged=False, activetool='tool')
        self.buttons = []
        self.responses = []
        self.current_response = None
        self.waits = self.opens = self.invalids = self.notices = 0
        self.pages = []
        self.ranges = []
        self.subscriptions = set()
        self.plugin = self.registered = True
        self.occupied = False
        self.interrupt_load = False
        self.nested_request = False
        self.nested_result = None
        self.delay = 0
        self.close_delay = 0
        self.close_waits = 0
        self.load_after_result = False
        self.token = ''
        self.state = 0
        self.finished = True
        self.drop_events = False
        self.acknowledgments = []
        self.deleted_owner = False
        self.inject_legacy = False
        self.stale_event = False
        self.destroy_during_wait = False
        self.ack_failure = False
        self.load_during_begin = False
        self.abandoned = []
        self.begin_arguments = []
        self.scales = [100]
        self.scale_errors = []
        self.interrupt_scale_error = False

    def run(self, name, *arguments):
        params, types, code, labels = self.program.functions[name.lower()]
        local = {key.lower(): value for (key, _), value in zip(params, arguments)}
        local['self'] = 'self'

        def read(token):
            lowered = token.lower()
            if token.startswith('"'):
                return json.loads(token)
            if lowered == 'none': return None
            if lowered in ('true', 'false'): return lowered == 'true'
            if re.fullmatch(r'-?\d+', token): return int(token)
            if re.fullmatch(r'-?(?:\d+\.\d*|\d*\.\d+|\d+[eE][+-]?\d+)(?:[eE][+-]?\d+)?', token): return float(token)
            if lowered in local: return local[lowered]
            if lowered in self.fields: return self.fields[lowered]
            if lowered.startswith('::') and lowered.endswith('_var'):
                return lowered[2:-4]
            raise AssertionError(('unknown variable', name, token))

        def write(token, value):
            lowered = token.lower()
            if lowered in self.fields and lowered not in types:
                self.fields[lowered] = value
            else:
                local[lowered] = value

        index = 0
        for _ in range(5000000):
            if index >= len(code): return None
            words = code[index]
            op = words[0].lower()
            op = {'cmp_lte': 'cmp_le', 'cmp_gte': 'cmp_ge', 'comp_gte': 'cmp_ge', 'comp_lte': 'cmp_le'}.get(op, op)
            if op.startswith('_label'): pass
            elif op == 'assign': write(words[1], read(words[2]))
            elif op == 'cast':
                value = read(words[2]); kind = types.get(words[1].lower())
                if kind == 'string': value = '' if value is None else str(value)
                elif kind == 'bool': value = bool(value)
                elif kind == 'int':
                    try: value = int(float(value))
                    except (ValueError, TypeError): value = 0
                elif kind == 'float': value = float(value)
                elif kind in ('clipboardcopypylonscript', 'clipboardmanager'): value = None
                write(words[1], value)
            elif op == 'not': write(words[1], not read(words[2]))
            elif op in ('ineg', 'fneg'): write(words[1], -read(words[2]))
            elif op in ('iadd', 'fadd', 'isub', 'fsub', 'imul', 'fmul', 'idiv', 'fdiv', 'strcat', 'cmp_eq', 'cmp_lt', 'cmp_le', 'cmp_gt', 'cmp_ge'):
                a, b = read(words[2]), read(words[3])
                if op in ('iadd', 'fadd'): value = a + b
                elif op in ('isub', 'fsub'): value = a - b
                elif op in ('imul', 'fmul'): value = a * b
                elif op == 'idiv': value = int(a / b)
                elif op == 'fdiv': value = a / b
                elif op == 'strcat': value = str(a) + str(b)
                elif op == 'cmp_eq': value = a == b
                elif op == 'cmp_lt': value = a < b
                elif op == 'cmp_le': value = a <= b
                elif op == 'cmp_gt': value = a > b
                else: value = a >= b
                write(words[1], value)
            elif op in ('jmp', 'jmpf', 'jmpt'):
                if op == 'jmp' or (op == 'jmpf' and not read(words[1])) or (op == 'jmpt' and read(words[1])):
                    index = labels[words[-1].lower()]
                    continue
            elif op == 'array_length': write(words[1], len(read(words[2])))
            elif op == 'array_create': write(words[1], [None] * read(words[2]))
            elif op == 'array_getlement': write(words[1], read(words[2])[read(words[3])])
            elif op == 'array_setelement': read(words[1])[read(words[2])] = read(words[3])
            elif op == 'propget': write(words[3], words[1].lower())
            elif op == 'callmethod':
                method, receiver, dest = words[1], read(words[2]), words[3]
                args = [read(x) for x in words[4:]]
                write(dest, self.method(method.lower(), receiver, args))
            elif op == 'callstatic':
                owner, method, dest = words[1], words[2], words[3]
                args = [read(x) for x in words[4:]]
                write(dest, self.static(owner.lower(), method.lower(), args))
            elif op == 'return': return read(words[1])
            else: raise AssertionError(('unsupported instruction', name, words))
            index += 1
        raise AssertionError(('instruction limit', name))

    def method(self, name, receiver, args):
        if name in ('updatelabel', 'setup'): return None
        if name == 'showinformmenu':
            self.invalids += 1
            if self.interrupt_scale_error:
                self.fields['ownedinputgeneration'] += 1
            return None
        if name == 'getat':
            assert 0 <= args[0] < 10
            return 'small_dialog'
        if name == 'show':
            if receiver == 'pick_from_list_paged': self.pages.append(tuple(args[:2]))
            assert self.buttons, 'unexpected extra menu'
            return self.buttons.pop(0)
        if name in ('registerforexternalevent', 'registerforremoteevent'):
            self.subscriptions.add(tuple(args[:1] if name == 'registerforexternalevent' else args)); return None
        if name in ('unregisterforexternalevent', 'unregisterforremoteevent'):
            self.subscriptions.discard(tuple(args)); return None
        if name == 'isdeleted': return self.deleted_owner
        if receiver == 'self' and name in self.program.functions: return self.run(name, *args)
        raise AssertionError(('unexpected method', name, receiver, args))

    def static(self, owner, name, args):
        if owner == 'clipboardextension' and name == 'getselectionscaleinputerror':
            assert args[0] == 'tool'
            try:
                amount = Decimal(args[1])
                valid = (amount > 0 and amount <= (1000 if args[2] else 99) and bool(self.scales))
                for current in self.scales:
                    target = Decimal(current) * (1 + amount / 100 if args[2] else 1 - amount / 100)
                    valid = valid and 1 <= target <= 1000 and target == target.to_integral_value()
            except InvalidOperation:
                valid = False
            if valid: return ''
            self.scale_errors.append(args[1])
            return '$Clipboard_ScaleRestricted'
        if owner == 'math' and name == 'min': return min(args)
        if owner == 'clipboardextension' and name == 'gettext':
            if args[0] in ('$Clipboard_ListPreviousPage', '$Clipboard_ListNextPage'):
                self.ranges.append(tuple(int(x) for x in args[1:3]))
            return args[0]
        if owner == 'game' and name == 'getplayer': return 'player'
        if owner == 'debug' and name == 'notification': self.notices += 1; return None
        if owner == 'clipboardextension' and name == 'isownedinputavailable': return self.plugin and self.registered
        if owner == 'clipboardextension' and name == 'beginownedinput':
            assert self.fields['ownedinputrequestactive']
            assert args[0] == 'tool'
            self.begin_arguments.append(list(args))
            if not self.plugin or not self.registered or self.occupied: return ''
            self.opens += 1
            self.token = f'request-{self.opens}'
            self.state = 1
            self.finished = False
            self.current_response = self.responses.pop(0) if self.responses else None
            if self.load_during_begin:
                self.load_during_begin = False
                candidates = [x for x in self.program.functions if 'onplayerloadgame' in x]
                self.run(candidates[0], 'player')
            return self.token
        if owner == 'clipboardextension' and name == 'getownedinputstate': return self.state if args[0] == self.token else 0
        if owner == 'clipboardextension' and name == 'isownedinputfinished': return self.finished or args[0] != self.token
        if owner == 'clipboardextension' and name == 'getownedinputresult':
            assert args[0] == self.token and self.state == 3 and self.finished
            return self.current_response[1]
        if owner == 'clipboardextension' and name == 'acknowledgeownedinput':
            self.acknowledgments.append(args[0])
            accepted = args[0] == self.token and self.finished and self.state == 3 and not self.ack_failure
            self.token = ''; self.state = 0
            return accepted
        if owner == 'clipboardextension' and name == 'cancelownedinput':
            assert args == ['tool']
            self.state = 4; self.finished = True
            return None
        if owner == 'clipboardextension' and name == 'abandonownedinput':
            self.abandoned.append(args[0])
            if args[0] == self.token:
                self.token = ''; self.state = 0; self.finished = True
            return None
        if owner == 'utility' and name == 'waitmenumode':
            self.waits += 1
            if self.nested_request:
                self.nested_request = False
                self.nested_result = self.run('RequestTextInput', '$header', 'old', 2, 50)
            if self.interrupt_load:
                self.interrupt_load = False
                candidates = [x for x in self.program.functions if 'onplayerloadgame' in x]
                assert len(candidates) == 1, candidates
                self.run(candidates[0], 'player')
                self.token = ''; self.state = 0; self.finished = True
            elif self.waits > self.delay:
                if self.state < 3:
                    assert self.current_response is not None, 'no native result scheduled'
                    action, text = self.current_response
                    self.state = {'accept': 3, 'cancel': 4, 'fail': 5, 'interrupt': 6, 'unknown': 0}[action]
                    self.occupied = self.close_delay > 0
                    self.finished = not self.occupied
                    self.interrupt_load = self.load_after_result
                    if not self.drop_events: self.run('ReceiveOwnedInputState', self.token, self.state)
                elif self.occupied:
                    assert self.fields['ownedinputrequestactive'], 'released receiver before menu close'
                    self.close_waits += 1
                    if self.close_waits >= self.close_delay:
                        self.occupied = False
                        self.finished = True
            else:
                self.state = 2
            if self.inject_legacy:
                self.inject_legacy = False
                self.run('ReceiveInput', 'stale TIM result')
                self.run('FinishTextInputRequest')
            if self.stale_event:
                self.run('ReceiveOwnedInputState', 'older-request', 3)
            if self.destroy_during_wait:
                self.destroy_during_wait = False
                self.deleted_owner = True
            return None
        raise AssertionError(('unexpected static', owner, name, args))


class PylonVM(VM):
    """Run real compiled action/destruction branches with UI/engine stubs."""
    def __init__(self, program):
        super().__init__(program)
        self.fields.update(isblocked=False, isdestroyed=False, toolactiongeneration=0,
                           clipboard='manager', activeselectionmethod='selection', workshopmode=True,
                           actonselectionbutton='action', changeselectionmethodbutton='change',
                           selectionmethodactionbutton='secondary', clearselectionbutton='clear', destroytoolbutton='destroy')
        self.action = 4
        self.scale_action = 0
        self.value = 30
        self.interruption = ''
        self.mutations = []
        self.cancel_owners = []
        self.unblocks = 0
        self.quick_choice = 6

    def method(self, name, receiver, args):
        if name == 'getclipboardmanager': return 'manager'
        if name == 'getclipboardmenumanager': return 'menus'
        if name == 'getselectedcount': return 1
        if name in ('getselectedwirecount', 'getselectedplugincount', 'getselectionmethodtype'): return 0
        if name == 'getselectionmethodname': return 'test'
        if name == 'showquickmenu':
            if self.interruption == 'destroy': self.run('DestroyTool')
            if self.interruption == 'load': self.run('InterruptInputAction')
            if self.interruption == 'replacement': self.fields['toolactiongeneration'] += 1
            return self.quick_choice
        if name == 'showactionselectmenu': return self.action
        if name in ('showrotationselectmenu', 'showmovedirectionmenu'): return 0
        if name == 'showscaletypemenu':
            if self.scale_action in (2, 5):
                if self.interruption == 'destroy': self.run('DestroyTool')
                if self.interruption == 'load': self.run('InterruptInputAction')
                if self.interruption == 'replacement':
                    self.fields['toolactiongeneration'] += 1
                    self.fields['isblocked'] = True
            return self.scale_action
        if name in ('showrotationamountselectmenuforowner', 'showrotationanglemenuforowner', 'showmoveamountmenuforowner', 'showscaleupmenuforowner', 'showscaledownmenuforowner'):
            assert args == ['self'], (name, args)
            if self.interruption == 'destroy': self.run('DestroyTool')
            if self.interruption == 'load': self.run('InterruptInputAction')
            if self.interruption == 'replacement':
                self.fields['toolactiongeneration'] += 1
                self.fields['isblocked'] = True
            return self.value
        if name in ('rotateselected', 'moveselected', 'scaleselected', 'translatearoundself'):
            self.mutations.append((name, args)); return None
        if name == 'removekeyword': self.unblocks += 1; return None
        if name in ('addkeyword', 'hide', 'disable', 'delete', 'begindestroy', 'clearselection', 'destroy', 'setactivetool'): return None
        if name == 'getlinkedref': return 'workshop'
        if name == 'getactivetool': return 'self'
        return super().method(name, receiver, args)

    def static(self, owner, name, args):
        if owner == 'clipboardextension' and name == 'islegacycleanupactive': return False
        if owner == 'clipboardextension' and name == 'cancelownedinput':
            self.cancel_owners.append(args[0]); return None
        return super().static(owner, name, args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assembly', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--pylon-assembly', type=Path)
    args = parser.parse_args()
    program = Program(args.assembly.read_text(encoding='utf-8-sig'))
    cases = []

    def assert_released(vm):
        assert not vm.fields['ownedinputrequestactive'] and vm.fields['ownedinputtoken'] == ''
        assert vm.fields['ownedinputowner'] is None
        assert not any(item[0].startswith('TIM::') for item in vm.subscriptions)

    def pagination(count, buttons, expected, description):
        vm = VM(program); vm.buttons = list(buttons)
        options = None if count is None else [f'item{i}' for i in range(count)]
        assert vm.run('ShowPickFromList', 'item', options) == expected, description
        if count:
            assert all(1 <= start <= end <= count for start, end in vm.pages + vm.ranges), (description, vm.pages, vm.ranges)
        assert not vm.buttons, (description, vm.buttons)
        cases.append(description)

    pagination(None, [], -1, 'null list')
    for count in (0, 1, 9, 10, 11, 19, 20, 21):
        pagination(count, [] if count == 0 else [0], -1, f'{count}: cancel')
        if not count: continue
        if count <= 10:
            pagination(count, [count], count - 1, f'{count}: last item')
            pagination(count, [count + 1], -1, f'{count}: invalid button')
        else:
            pages = (count + 9) // 10
            last_start = (pages - 1) * 10
            pagination(count, [2] * (pages - 1) + [count - last_start + 2], count - 1, f'{count}: last item')
            pagination(count, [2] * pages + [3], 0, f'{count}: next wraps')
            pagination(count, [1, 3], last_start, f'{count}: previous wraps')
            pagination(count, [1, count - last_start + 3, 0], -1, f'{count}: unused/out-of-range button')

    for function, valid, expected in (('ShowRotationAmountSelectMenu', '180', 180), ('ShowMoveAmountMenu', '9999', 9999), ('ShowScaleUpMenu', '1000', 11.0), ('ShowScaleDownMenu', '99', 0.01)):
        vm = VM(program); vm.responses = [('accept', '0'), ('accept', '999999'), ('accept', valid)]
        if function == 'ShowScaleUpMenu': vm.scales = [1]
        assert abs(vm.run(function) - expected) < 1e-9
        assert vm.invalids == 2 and vm.opens == 3
        assert_released(vm)
        cases.append(function + ': invalid retries then valid')
        vm = VM(program); vm.responses = [('cancel', valid)]
        assert vm.run(function) == 0
        assert_released(vm)
        cases.append(function + ': cancel')

    for function, value, expected, maximum in (
        ('ShowRotationAngleMenu', '0.000001', 0.000001, 180),
        ('ShowRotationAngleMenu', '12.345678', 12.345678, 180),
        ('ShowScaleUpMenu', '0.5', 1.005, 1000),
        ('ShowScaleDownMenu', '0.5', 0.995, 99)):
        vm = VM(program); vm.responses = [('accept', value)]
        vm.scales = [200]
        assert abs(vm.run(function) - expected) < 1e-9
        assert vm.begin_arguments[0][-4:] == [1, 64, 0, maximum]
        assert_released(vm)
        cases.append(function + ': fractional value reaches float calculation')
        for outcome in ('cancel', 'interrupt', 'unknown'):
            vm = VM(program); vm.responses = [(outcome, value)]
            assert vm.run(function) == 0
            assert_released(vm)
            cases.append(function + ': decimal ' + outcome + ' prevents action')

    for function in ('ShowScaleUpMenu', 'ShowScaleDownMenu'):
        for scales, invalid, valid in (([100], '12.345678', '12'), ([50, 100, 150], '1', '2'), ([100], '12.000001', '12')):
            vm = VM(program); vm.scales = scales
            vm.responses = [('accept', invalid), ('accept', valid)]
            result = vm.run(function)
            expected = 1 + int(valid) / 100 if function == 'ShowScaleUpMenu' else 1 - int(valid) / 100
            assert abs(result - expected) < 1e-9 and vm.scale_errors == [invalid] and vm.opens == 2
            assert_released(vm)
            cases.append(function + ': exact scale retry for ' + str(scales) + '/' + invalid)
        vm = VM(program); vm.responses = [('accept', '12.345678'), ('cancel', '')]
        assert vm.run(function) == 0 and vm.opens == 2 and len(vm.scale_errors) == 1
        assert_released(vm)
        cases.append(function + ': cancel after rejected scale never approves a factor')
        vm = VM(program); vm.responses = [('accept', '12.345678')]; vm.interrupt_scale_error = True
        assert vm.run(function) == 0 and vm.opens == 1
        assert_released(vm)
        cases.append(function + ': load/new request during rejection prevents retry')

    vm = VM(program); vm.responses = [('accept', 'Vault')]; vm.delay = 36000; vm.nested_request = True
    assert vm.run('ShowPatternNameMenu', 'default') == 'Vault'
    assert vm.waits == 36001 and vm.opens == 1 and vm.nested_result == '' and vm.notices == 1
    assert_released(vm)
    cases.append('long typing wait plus overlapping request refused')
    vm = VM(program); vm.responses = [('accept', 'Vault')]; vm.close_delay = 25
    assert vm.run('ShowPatternNameMenu', 'default') == 'Vault'
    assert vm.close_waits == 25 and not vm.occupied
    assert_released(vm)
    cases.append('accepted result waits for actual menu close before releasing receiver')
    vm = VM(program); vm.responses = [('accept', 'Vault')]; vm.close_delay = 25; vm.load_after_result = True
    assert vm.run('ShowPatternNameMenu', 'default') == ''
    assert vm.close_waits == 0
    assert_released(vm)
    cases.append('load cancels accepted result while menu close is still pending')
    for failure in ('plugin', 'registered', 'occupied', 'load'):
        vm = VM(program)
        if failure == 'load': vm.interrupt_load = True
        elif failure == 'occupied': vm.occupied = True
        else: setattr(vm, failure, False)
        assert vm.run('RequestTextInput', '$header', 'default', 2, 50) == ''
        assert vm.opens == (1 if failure == 'load' else 0)
        assert_released(vm)
        # An unsolicited late callback cannot populate an idle receiver.
        vm.run('ReceiveInput', 'late')
        assert vm.fields['timinput'] == ''
        cases.append(f'owned input {failure}: clean cancellation')

    for outcome in ('fail', 'interrupt', 'unknown', 'cancel'):
        vm = VM(program); vm.responses = [(outcome, 'must not be returned')]; vm.drop_events = True
        assert vm.run('ShowPatternNameMenu', 'default') == ''
        assert_released(vm)
        cases.append(outcome + ': lost terminal event recovers from retained status without accepting text')
    vm = VM(program); vm.responses = [('accept', 'Recovered')]; vm.drop_events = True; vm.close_delay = 4
    assert vm.run('ShowPatternNameMenu', 'default') == 'Recovered'
    assert vm.close_waits == 4 and vm.acknowledgments == ['request-1']
    assert_released(vm)
    cases.append('lost accepted and close events recover result exactly once after release')
    vm = VM(program); vm.responses = [('accept', 'Current')]; vm.delay = 3; vm.inject_legacy = True; vm.stale_event = True
    assert vm.run('ShowPatternNameMenu', 'default') == 'Current'
    assert_released(vm)
    cases.append('legacy TIM callback and cleanup plus stale token events cannot consume owned request')
    vm = VM(program); vm.responses = [('accept', 'New')]
    vm.fields.update(waitingoninput=True, inputrequestactive=True, timinput='old pending text')
    vm.subscriptions.update({('TIM::Accept',), ('TIM::Cancel',)})
    assert vm.run('ShowPatternNameMenu', 'default') == 'New'
    assert not vm.fields['waitingoninput'] and not vm.fields['inputrequestactive'] and vm.fields['timinput'] == ''
    assert vm.fields['inputrequestgeneration'] == 1
    assert_released(vm)
    cases.append('legacy lost TIM wait without load subscription is cancelled before owned request starts')
    vm = VM(program); vm.responses = [('accept', 'Late')]; vm.delay = 5; vm.destroy_during_wait = True
    assert vm.run('ShowPatternNameMenu', 'default') == ''
    assert_released(vm)
    cases.append('destroyed owner cancels wait and never receives accepted text')
    vm = VM(program); vm.responses = [('accept', 'Late')]; vm.ack_failure = True
    assert vm.run('ShowPatternNameMenu', 'default') == ''
    assert_released(vm)
    cases.append('failed terminal acknowledgment invalidates previously read result')
    vm = VM(program); vm.load_during_begin = True
    assert vm.run('ShowPatternNameMenu', 'default') == '' and vm.opens == 1 and vm.waits == 0
    assert vm.abandoned == ['request-1', 'request-1'] and not vm.acknowledgments
    assert_released(vm)
    cases.append('load during native Begin abandons exact late token without leaving an orphan consumer')
    vm = VM(program); vm.responses = [('accept', '123')]
    assert vm.run('RequestTextInput', '$header', '', 0, 10) == '123'
    assert vm.begin_arguments[0][-2:] == [0, 2147483647]
    cases.append('legacy public numeric RequestTextInput retains general integer bounds')
    for function, buttons, expected in (('ShowRotationAmountSelectMenu', [2], 10), ('ShowMoveAmountMenu', [8], 256), ('ShowScaleUpMenu', [2], 1.25), ('ShowScaleDownMenu', [2], 0.9)):
        vm = VM(program); vm.plugin = False; vm.buttons = buttons
        assert abs(vm.run(function) - expected) < 1e-6 and vm.opens == 0
        cases.append(function + ': pre-request unavailable service retains preset fallback')
    vm = VM(program); vm.plugin = False
    assert vm.run('ShowPatternNameMenu', 'Default name') == 'Default name' and vm.opens == 0
    cases.append('pre-request unavailable naming service retains generated default')

    if args.pylon_assembly:
        pylon = Program(args.pylon_assembly.read_text(encoding='utf-8-sig'))
        for action, scale_action, expected in ((4, 0, 'rotateselected'), (5, 0, 'moveselected'), (6, 0, 'scaleselected'), (6, 1, 'scaleselected'), (6, 3, 'scaleselected'), (6, 4, 'scaleselected')):
            for interruption in ('', 'cancel', 'destroy', 'load', 'replacement'):
                vm = PylonVM(pylon); vm.action = action; vm.scale_action = scale_action; vm.interruption = interruption
                if interruption == 'cancel': vm.value = 0
                vm.run('ButtonPressed', 'action')
                assert [name for name, _ in vm.mutations] == ([expected] if not interruption else []), (action, scale_action, interruption, vm.mutations)
                if interruption == 'destroy':
                    assert vm.fields['isdestroyed'] and vm.cancel_owners == ['self'] and vm.unblocks == 0
                elif interruption == 'replacement':
                    assert vm.fields['isblocked'] and vm.unblocks == 0
                else:
                    assert not vm.fields['isblocked'] and vm.unblocks == 5
                cases.append(f'pylon {action}/{scale_action}: {interruption or "accept"} applies only current action and releases correct owner')
        for scale_action, whole in ((2, True), (5, False)):
            for interruption in ('', 'destroy', 'load', 'replacement'):
                vm = PylonVM(pylon); vm.action = 6; vm.scale_action = scale_action; vm.interruption = interruption
                vm.run('ButtonPressed', 'action')
                assert vm.mutations == ([] if interruption else [('scaleselected', [-1, whole, True])]), vm.mutations
                if interruption == 'destroy':
                    assert vm.fields['isdestroyed'] and vm.cancel_owners == ['self'] and vm.unblocks == 0
                elif interruption == 'replacement':
                    assert vm.fields['isblocked'] and vm.unblocks == 0
                else:
                    assert not vm.fields['isblocked'] and vm.unblocks == 5
                cases.append(f'pylon restore {scale_action}: {interruption or "accept"} preserves absolute sentinel and correct owner')
        vm = PylonVM(pylon); vm.action = 6; vm.scale_action = -1
        vm.run('ButtonPressed', 'action')
        assert not vm.mutations and not vm.fields['isblocked'] and vm.unblocks == 5
        cases.append('pylon scale menu cancellation never restores or resizes selection')
        for action, scale_action, expected in ((4, 0, 'rotateselected'), (6, 0, 'scaleselected'), (6, 1, 'scaleselected'), (6, 3, 'scaleselected'), (6, 4, 'scaleselected')):
            vm = PylonVM(pylon); vm.action = action; vm.scale_action = scale_action; vm.value = 12.345678
            vm.run('ButtonPressed', 'action')
            assert len(vm.mutations) == 1 and vm.mutations[0][0] == expected
            assert vm.mutations[0][1][0] == 12.345678, vm.mutations
            cases.append(f'pylon {action}/{scale_action}: fractional mutation argument is not cast to integer')
        for interruption in ('', 'destroy', 'load', 'replacement'):
            vm = PylonVM(pylon); vm.interruption = interruption
            vm.run('OnHotkey', 'activateActionQuickBtn')
            assert [name for name, _ in vm.mutations] == ([] if interruption else ['translatearoundself'])
            cases.append('pylon quick-menu ' + (interruption or 'accept') + ': stale menu result cannot move interrupted tool')

    report = dict(result='passed', kind='offline compiled control flow with deterministic engine stubs', assembly=str(args.assembly.resolve()), sha256=hashlib.sha256(args.assembly.read_bytes()).hexdigest(), caseCount=len(cases), cases=cases, limitations=['Not a real Papyrus VM or Scaleform test.', 'Native protocol behavior is stubbed here and requires its separate state-machine tests plus live menu acceptance.'])
    if args.pylon_assembly:
        report['pylonAssembly'] = str(args.pylon_assembly.resolve())
        report['pylonAssemblySha256'] = hashlib.sha256(args.pylon_assembly.read_bytes()).hexdigest()
    text = json.dumps(report, indent=2)
    if args.output: args.output.write_text(text + '\n', encoding='utf-8')
    print(text)


if __name__ == '__main__': main()
