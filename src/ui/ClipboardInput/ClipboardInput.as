// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Everett C Sands
package {
    import flash.display.MovieClip;
    import flash.display.Sprite;
    import flash.events.Event;
    import flash.events.KeyboardEvent;
    import flash.events.MouseEvent;
    import flash.text.TextField;
    import flash.text.TextFormat;
    import flash.ui.Keyboard;

    // All prompt strings arrive already localized. No TIM/SPECIALMenu assets,
    // globals, event names or external network resources are used.
    public dynamic class ClipboardInput extends MovieClip {
        public const clipboardProtocol:int = 1;
        public var ClipboardReady:Function;
        public var ClipboardSubmit:Function;
        private var token:String = "";
        private var entry:TextField;
        private var title:TextField;
        private var error:TextField;
        private var accepted:Boolean = false;
        private var enterDown:Boolean = false;
        private var initialized:Boolean = false;
        private var maximumChars:int = 50;
        private var errorLabel:String = "";
        private var inputType:int = 2;
        private var buttons:Array = [];
        private var keyValues:Array = [];
        private var selected:int = 0;
        private var controller:Boolean = false;
        private var upper:Boolean = false;
        private var keyboardPanel:Sprite;
        private var dialog:Sprite;
        private var help:TextField;
        private var buttonBar:Sprite;
        private var acceptLabel:String;
        private var cancelLabel:String;
        private const ink:uint = 0x00FF00;
        private const panelWidth:Number = 500;

        public function ClipboardInput() {
            super();
            addEventListener(Event.ADDED_TO_STAGE, onAdded);
            if (stage) onAdded(null);
        }

        private function onAdded(event:Event):void {
            removeEventListener(Event.ADDED_TO_STAGE, onAdded);
            stage.scaleMode = "showAll";
            stage.align = "";
            stage.addEventListener(KeyboardEvent.KEY_DOWN, onKey);
            stage.addEventListener(KeyboardEvent.KEY_UP, onKeyUp);
        }

        private function textField(value:String, px:Number, py:Number, widthValue:Number, size:int):TextField {
            var field:TextField = new TextField();
            field.defaultTextFormat = new TextFormat("$MAIN_Font_Bold", size, ink, false, false, false, null, null, "center");
            field.x = px; field.y = py; field.width = widthValue; field.height = 70;
            field.multiline = false; field.wordWrap = false; field.selectable = false;
            field.mouseEnabled = false; field.text = value;
            dialog.addChild(field);
            return field;
        }

        // Independently drawn Fallout-style frame: fine horizontal rules and
        // short corner brackets, with a translucent green backing.
        private function frame(box:Sprite, widthValue:Number, heightValue:Number):void {
            box.graphics.clear();
            box.graphics.beginFill(0x063306, 0.72);
            box.graphics.drawRect(0, 0, widthValue, heightValue); box.graphics.endFill();
            box.graphics.lineStyle(1, ink);
            box.graphics.moveTo(0, 6); box.graphics.lineTo(0, 0);
            box.graphics.lineTo(widthValue, 0); box.graphics.lineTo(widthValue, 6);
            box.graphics.moveTo(0, heightValue - 6); box.graphics.lineTo(0, heightValue);
            box.graphics.lineTo(widthValue, heightValue); box.graphics.lineTo(widthValue, heightValue - 6);
        }

        private function layout():void {
            title.height = Math.max(27, title.textHeight + 6);
            entry.y = title.y + title.height + 12;
            error.y = entry.y + entry.height + 5;
            error.visible = error.text.length > 0;
            error.height = error.visible ? Math.max(22, error.textHeight + 6) : 0;
            var nextY:Number = error.visible ? error.y + error.height + 8 : entry.y + entry.height + 12;
            keyboardPanel.y = nextY;
            if (keyboardPanel.visible) nextY += Math.ceil((buttons.length - 2) / 10) * 33 + 10;
            help.y = nextY;
            help.height = Math.max(22, help.textHeight + 6);
            var heightValue:Number = help.y + help.height + 18;
            dialog.x = (1280 - panelWidth) / 2;
            dialog.y = (720 - heightValue) / 2;
            frame(dialog, panelWidth, heightValue);
        }

        public function Initialize(value:Object):Boolean {
            if (initialized || !stage || value.protocol != clipboardProtocol) return false;
            initialized = true;
            token = String(value.token);
            maximumChars = int(value.maxChars);
            inputType = int(value.inputType);
            errorLabel = String(value.errorLabel);
            dialog = new Sprite(); addChild(dialog);
            title = textField(String(value.header), 20, 24, 460, 19);
            title.multiline = true; title.wordWrap = true;
            entry = textField("", 20, 0, 460, 21);
            entry.type = "input"; entry.selectable = true; entry.mouseEnabled = true;
            // Decimal submissions are validated whole by native code. Do not
            // silently cut a pasted seventh decimal or strip its sign/exponent.
            entry.height = 34; entry.maxChars = inputType == 1 ? 0 : maximumChars;
            if (inputType == 0) entry.restrict = "0-9";
            entry.text = inputType == 1 ? String(value.initial) : truncateComplete(String(value.initial), maximumChars);
            error = textField("", 20, 0, 460, 16);
            error.multiline = true; error.wordWrap = true;
            buttonBar = new Sprite(); addChild(buttonBar);
            acceptLabel = String(value.acceptLabel).toUpperCase();
            cancelLabel = String(value.cancelLabel).toUpperCase();
            createButton(acceptLabel, "ACCEPT");
            createButton(cancelLabel, "CANCEL");
            layoutButtons();
            keyboardPanel = new Sprite(); dialog.addChild(keyboardPanel);
            keyboardPanel.x = 20;
            buildKeyboard();
            keyboardPanel.visible = false;
            help = textField(String(value.controllerHelp), 20, 0, 460, 14);
            help.defaultTextFormat = new TextFormat("$MAIN_Font", 14, ink, false, false, false, null, null, "center");
            help.setTextFormat(help.defaultTextFormat);
            help.multiline = true; help.wordWrap = true;
            layout();
            stage.focus = entry; entry.setSelection(0, entry.text.length);
            if (ClipboardReady == null || !ClipboardReady(token, clipboardProtocol)) {
                entry.type = "dynamic"; return false;
            }
            return true;
        }

        private function createButton(label:String, action:String):void {
            var box:Sprite = new Sprite();
            box.name = action; box.buttonMode = true; box.tabEnabled = false;
            var field:TextField = new TextField();
            field.defaultTextFormat = new TextFormat("$MAIN_Font_Bold", 19, ink, false, false, false, null, null, "center");
            field.height = 29; field.x = 8; field.y = 1; field.mouseEnabled = false; field.selectable = false;
            field.text = label; box.addChild(field); box.addEventListener(MouseEvent.CLICK, onClick);
            box.addEventListener(MouseEvent.ROLL_OVER, onHover);
            box.addEventListener(MouseEvent.ROLL_OUT, onLeave);
            buttonBar.addChild(box); buttons.push(box); keyValues.push(action);
        }

        private function layoutButtons():void {
            var left:Number = 4;
            for (var i:int = 0; i < 2; ++i) {
                var box:Sprite = Sprite(buttons[i]);
                var field:TextField = TextField(box.getChildAt(0));
                var hint:String = i == 0 ? (controller ? "Start" : "Enter") : (controller ? "B" : "Tab");
                field.text = hint + ") " + (i == 0 ? acceptLabel : cancelLabel);
                field.width = Math.ceil(field.textWidth) + 6;
                box.x = left; box.y = 2; left += field.width + 24;
                paintButton(box, false);
            }
            frame(buttonBar, left, 33);
            buttonBar.x = (1280 - left) / 2; buttonBar.y = 652;
        }

        private function paintButton(box:Sprite, highlight:Boolean):void {
            var field:TextField = TextField(box.getChildAt(0));
            box.graphics.clear();
            box.graphics.beginFill(highlight ? ink : 0x063306, highlight ? 1 : 0);
            box.graphics.drawRect(0, 0, field.width + (box.parent == buttonBar ? 16 : 0), 29);
            box.graphics.endFill();
            field.textColor = highlight ? 0x001000 : ink;
        }

        private function onHover(event:MouseEvent):void {
            paintButton(Sprite(event.currentTarget), true);
        }

        private function onLeave(event:MouseEvent):void {
            var box:Sprite = Sprite(event.currentTarget);
            paintButton(box, controller && box == buttons[selected]);
        }

        private function buildKeyboard():void {
            var chars:String = inputType == 0 ? "0123456789" :
                (inputType == 1 ? "0123456789." : "abcdefghijklmnopqrstuvwxyz0123456789 -_.'");
            for (var i:int = 0; i < chars.length; ++i) {
                var box:Sprite = new Sprite();
                box.x = (i % 10) * 46; box.y = int(i / 10) * 33;
                box.name = chars.charAt(i); box.buttonMode = true; box.tabEnabled = false;
                var field:TextField = new TextField();
                field.defaultTextFormat = new TextFormat("$MAIN_Font", 18, ink, false, false, false, null, null, "center");
                field.width = 41; field.height = 29; field.mouseEnabled = false; field.selectable = false;
                field.text = chars.charAt(i) == " " ? "_" : chars.charAt(i);
                box.addChild(field); box.addEventListener(MouseEvent.CLICK, onClick);
                box.addEventListener(MouseEvent.ROLL_OVER, onHover);
                box.addEventListener(MouseEvent.ROLL_OUT, onLeave);
                keyboardPanel.addChild(box); buttons.push(box); keyValues.push(chars.charAt(i));
                paintButton(box, false);
            }
        }

        private function onClick(event:MouseEvent):void {
            action(String(event.currentTarget.name));
        }

        private function action(value:String):void {
            if (accepted) return;
            if (value == "ACCEPT") { submit(false); return; }
            if (value == "CANCEL") { submit(true); return; }
            var character:String = upper ? value.toUpperCase() : value;
            var replaced:int = entry.selectionEndIndex - entry.selectionBeginIndex;
            if (entry.text.length - replaced + character.length <= maximumChars) {
                entry.replaceSelectedText(character); error.text = ""; layout();
            }
        }

        private function truncateComplete(value:String, length:int):String {
            var truncated:String = value.substr(0, length);
            if (truncated.length > 0) {
                var last:int = truncated.charCodeAt(truncated.length - 1);
                if (last >= 0xD800 && last <= 0xDBFF) truncated = truncated.substr(0, truncated.length - 1);
            }
            return truncated;
        }

        private function removeLastCharacter(value:String):String {
            var length:int = value.length;
            if (length == 0) return value;
            var last:int = value.charCodeAt(length - 1);
            if (last >= 0xDC00 && last <= 0xDFFF && length > 1) {
                var previous:int = value.charCodeAt(length - 2);
                if (previous >= 0xD800 && previous <= 0xDBFF) return value.substr(0, length - 2);
            }
            return value.substr(0, length - 1);
        }

        private function submit(cancelled:Boolean):void {
            if (!initialized || accepted || ClipboardSubmit == null) return;
            // Keep text and controls intact until the native service acknowledges
            // the correlated terminal result. A failed call can be retried.
            if (ClipboardSubmit(token, entry.text, cancelled)) {
                accepted = true; entry.type = "dynamic";
                mouseEnabled = false; mouseChildren = false;
            } else {
                error.text = errorLabel; layout(); stage.focus = entry;
            }
        }

        private function onKey(event:KeyboardEvent):void {
            if (event.keyCode == Keyboard.ENTER) {
                event.preventDefault(); event.stopImmediatePropagation();
                if (initialized && !accepted) enterDown = true;
                return;
            }
            if (!initialized || accepted) return;
            if (event.keyCode == Keyboard.ESCAPE || event.keyCode == Keyboard.TAB) { event.preventDefault(); submit(true); }
        }

        private function onKeyUp(event:KeyboardEvent):void {
            if (event.keyCode != Keyboard.ENTER) return;
            event.preventDefault(); event.stopImmediatePropagation();
            var wasDown:Boolean = enterDown;
            enterDown = false;
            // Keep the modal menu alive through release. An unpaired release
            // must not accept the prompt, and repeats must submit once.
            if (wasDown && initialized && !accepted) submit(false);
        }

        public function ProcessUserEvent(eventName:String, pressed:Boolean):Boolean {
            // IMenu::HandleEvent consumes the native ButtonEvent only when this
            // method returns true. Flash preventDefault alone does not stop a
            // mapped Accept/Activate from reaching the menu/world underneath.
            // Keep consuming during initialization and acknowledged closing too.
            // Keyboard/mouse editing and submission use their Flash events.
            return true;
        }

        // Controller editing: D-pad selects keys; A enters a key, B cancels,
        // X deletes, Y changes case, Start accepts. Keyboard remains available.
        public function ControllerAction(value:String):Boolean {
            if (!initialized || accepted) return false;
            if (!controller) { controller = true; keyboardPanel.visible = true; selected = 2; layoutButtons(); layout(); }
            if (value == "Cancel") submit(true);
            else if (value == "Accept") submit(false);
            else if (value == "Select") action(String(keyValues[selected]));
            else if (value == "Delete") {
                if (entry.selectionBeginIndex != entry.selectionEndIndex) entry.replaceSelectedText("");
                else if (entry.text.length > 0) entry.text = removeLastCharacter(entry.text);
                entry.setSelection(entry.text.length, entry.text.length);
            } else if (value == "Case") {
                upper = !upper;
                for (var j:int = 2; j < buttons.length; ++j) {
                    var field:TextField = TextField(Sprite(buttons[j]).getChildAt(0));
                    field.text = upper ? String(keyValues[j]).toUpperCase() : String(keyValues[j]);
                }
            } else if (value == "Left") selected = (selected + buttons.length - 1) % buttons.length;
            else if (value == "Right") selected = (selected + 1) % buttons.length;
            else if (value == "Up") selected = selected < 12 ? 0 : selected - 10;
            else if (value == "Down") selected = selected < 2 ? 2 : (selected + 10 >= buttons.length ? 1 : selected + 10);
            for (var i:int = 0; i < buttons.length; ++i) paintButton(Sprite(buttons[i]), i == selected);
            stage.focus = entry;
            return true;
        }
    }
}
