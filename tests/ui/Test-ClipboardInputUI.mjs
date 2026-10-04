// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the production AS3 input methods after erasing type annotations.
// This is a host logic check, not a Flash renderer or engine-input emulator.
import fs from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const sourcePath = process.argv[2] || path.join(root, 'src/ui/ClipboardInput/ClipboardInput.as');
const source = fs.readFileSync(sourcePath, 'utf8');
const names = ['onAdded', 'onKey', 'onKeyUp', 'ProcessUserEvent', 'submit', 'onClick', 'action', 'ControllerAction'];
const methods = names.map(name => {
    const match = new RegExp(`(?:public|private) function ${name}\\s*\\(([^)]*)\\)\\s*:\\s*\\w+\\s*\\{`).exec(source);
    if (!match) return '';
    let end = match.index + match[0].length, depth = 1;
    // These small production methods contain no braces inside string literals.
    for (; end < source.length && depth; ++end) {
        if (source[end] === '{') ++depth;
        if (source[end] === '}') --depth;
    }
    assert.equal(depth, 0, `Unclosed method ${name}`);
    const eraseTypes = text => text.replace(/:\s*(?:Boolean|String|KeyboardEvent|MouseEvent|Event|int|TextField)\b(?=\s*(?:[,)=;]|$))/g, '');
    return `function ${name}(${eraseTypes(match[1])}) {${eraseTypes(source.slice(match.index + match[0].length, end))}`;
}).join('\n');
let checks = 0;
const check = (ok, label) => { ++checks; assert.ok(ok, label); };
function dialog({ initialized = true, valid = true } = {}) {
    const listeners = new Map(), calls = [];
    const context = vm.createContext({
        initialized, accepted: false, enterDown: false, token: 'owned-token',
        entry: { text: '12.345678', type: 'input' }, error: { text: '' }, errorLabel: 'Invalid',
        mouseEnabled: true, mouseChildren: true, controller: true, buttons: [],
        String, TextField: x => x, Sprite: x => x,
        Keyboard: { ENTER: 13, ESCAPE: 27, TAB: 9 },
        KeyboardEvent: { KEY_DOWN: 'down', KEY_UP: 'up' }, Event: { ADDED_TO_STAGE: 'added' },
        removeEventListener() {}, layout() {}, paintButton() {},
        stage: { addEventListener(type, fn) { listeners.set(type, fn); } },
        ClipboardSubmit(token, value, cancelled) { calls.push({ token, value, cancelled }); return cancelled || valid; }
    });
    vm.runInContext(methods, context);
    context.onAdded(null);
    return { context, calls, listeners, key(type, code = 13) {
        const event = { keyCode: code, prevented: false, stopped: false,
            preventDefault() { this.prevented = true; }, stopImmediatePropagation() { this.stopped = true; } };
        listeners.get(type)?.(event);
        return event;
    }, native(name, pressed) { return context.ProcessUserEvent?.(name, pressed) === true; } };
}

// Acceptance must not release the modal menu while the physical key is down.
// The original movie fails here, before its missing native callback is checked.
{
    const d = dialog();
    d.key('down');
    check(d.calls.length === 0 && !d.context.accepted, 'ENTER must not close the modal dialog before key release');
}
// IMenu marks the raw event handled when this callback returns true; the outer
// UI dispatcher also enforces kModal while the menu remains on its stack.
for (const initialized of [false, true]) {
    const d = dialog({ initialized });
    for (const accepted of [false, true]) {
        d.context.accepted = accepted;
        for (const name of ['Accept', 'Activate', 'DISABLED']) {
            for (const pressed of [true, false]) {
                check(d.native(name, pressed), `${name} is consumed during init/open/closing, phase ${pressed}`);
            }
        }
    }
}
{
    const d = dialog();
    check(d.listeners.has('up'), 'ENTER release is subscribed');
    check(d.key('down').prevented && d.calls.length === 0, 'press cannot submit or dismiss');
    check(d.key('down').stopped && d.calls.length === 0, 'held/repeated ENTER stays contained');
    check(d.key('up').stopped && d.calls.length === 1, 'release submits exactly once');
    check(d.calls[0].value === '12.345678' && !d.calls[0].cancelled, 'exact entry reaches normal validation');
    check(d.context.accepted && d.context.entry.type === 'dynamic', 'acknowledged submission disables editing');
    d.key('up'); d.key('down'); d.key('up');
    check(d.calls.length === 1 && d.native('Accept', false), 'duplicates stay consumed after acceptance');
}
{
    const d = dialog();
    d.key('up');
    check(d.calls.length === 0, 'unpaired release cannot approve a new prompt');
    const event = d.key('down', 65);
    check(!event.prevented && !event.stopped && !d.context.enterDown, 'ordinary text editing remains available');
    d.key('up', 65);
    check(d.calls.length === 0, 'ordinary key release does not submit');
}
{
    const d = dialog({ initialized: false });
    d.key('down'); d.context.initialized = true; d.key('up');
    check(d.calls.length === 0, 'press before readiness cannot accept after readiness');
}
{
    const d = dialog({ valid: false });
    d.key('down'); d.key('up');
    check(d.calls.length === 1 && !d.context.accepted && d.context.entry.type === 'input', 'invalid entry keeps prompt editable');
    check(d.context.error.text === 'Invalid' && d.context.stage.focus === d.context.entry, 'invalid entry restores focus');
    d.key('up');
    check(d.calls.length === 1, 'invalid entry is not resubmitted by duplicate release');
    d.key('down'); d.key('up');
    check(d.calls.length === 2 && d.native('Activate', false), 'new press retries while underlying activation remains consumed');
}
for (const code of [9, 27]) {
    const d = dialog();
    d.key('down', code);
    check(d.calls.length === 1 && d.calls[0].cancelled, 'Tab/Escape cancellation retained');
}
for (const action of ['ACCEPT', 'CANCEL']) {
    const d = dialog();
    d.context.onClick({ currentTarget: { name: action } });
    check(d.calls.length === 1 && d.calls[0].cancelled === (action === 'CANCEL'), 'mouse action retained');
}
for (const action of ['Accept', 'Cancel']) {
    const d = dialog();
    d.context.ControllerAction(action);
    check(d.calls.length === 1 && d.calls[0].cancelled === (action === 'Cancel'), 'controller action retained');
}
console.log(JSON.stringify({ result: 'passed', checks, source: sourcePath,
    scope: 'Production AS3 handler logic with host fixtures; not live Scaleform or engine input routing' }));
