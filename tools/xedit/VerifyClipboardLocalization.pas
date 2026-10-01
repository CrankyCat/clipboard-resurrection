{
  SPDX-License-Identifier: GPL-3.0-or-later
  Read-only xEdit 4.1.5 script. No plugin setters, saves, or removals.
  Copy into a dedicated run directory before invoking -script:<absolute path>.
  JsonDataObjects writes resolved Unicode directly as UTF-8 without a BOM.
}
unit VerifyClipboardLocalization;

var
  Report: TJsonObject;
  Occurrences: TStringList;
  Records, Fields, NonEmptyFields, Errors: Integer;

procedure ReadElement(e: IInterface; formID, recordType, fieldType: string);
var
  i, occurrence: Integer;
  sig, occurrenceKey: string;
  entry: TJsonObject;
begin
  if ElementType(e) = etMainRecord then begin
    formID := IntToHex(FixedFormID(e), 8);
    recordType := Signature(e);
    fieldType := '';
    Inc(Records);
  end else begin
    sig := Signature(e);
    if sig <> '' then
      fieldType := sig;
  end;

  if Check(e) <> '' then begin
    Inc(Errors);
    entry := Report.A['check_errors'].AddObject;
    entry.S['form_id'] := formID;
    entry.S['path'] := FullPath(e);
    entry.S['error'] := Check(e);
    AddMessage('CLIPBOARD_L10N_CHECK_ERROR|' + formID);
  end;

  if DefType(e) = dtLString then begin
    occurrenceKey := formID + ':' + fieldType;
    occurrence := 0;
    for i := 0 to Occurrences.Count - 1 do
      if Occurrences[i] = occurrenceKey then
        Inc(occurrence);
    Occurrences.Add(occurrenceKey);
    Inc(Fields);
    if GetEditValue(e) <> '' then
      Inc(NonEmptyFields);
    entry := Report.A['fields'].AddObject;
    entry.S['form_id'] := formID;
    entry.S['record_type'] := recordType;
    entry.S['field'] := fieldType;
    entry.I['occurrence'] := occurrence;
    { A JVI local declared string narrows Unicode to the Windows ANSI code page. }
    { Pass the native Unicode return directly to the JSON adapter. }
    entry.S['text'] := GetEditValue(e);
    entry.S['path'] := FullPath(e);
    Exit;
  end;

  for i := 0 to ElementCount(e) - 1 do
    ReadElement(ElementByIndex(e, i), formID, recordType, fieldType);
end;

function Initialize: Integer;
var
  target: IInterface;
begin
  Result := 0;
  Records := 0;
  Fields := 0;
  NonEmptyFields := 0;
  Errors := 0;
  Report := TJsonObject.Create;
  Occurrences := TStringList.Create;
  Report.S['format'] := 'clipboard-xedit-readback-v2';
  Report.A['fields'].Clear;
  Report.A['check_errors'].Clear;
  target := FileByName('Clipboard.esp');
  if not Assigned(target) then begin
    AddMessage('CLIPBOARD_L10N_FATAL|Clipboard.esp is not loaded');
    Result := 1;
    Exit;
  end;
  AddMessage('CLIPBOARD_L10N_BEGIN|Clipboard.esp');
  ReadElement(target, '', '', '');
end;

function Finalize: Integer;
var
  summary, outputPath: string;
begin
  Result := 0;
  summary := 'summary|' + IntToStr(Records) + '|' + IntToStr(Fields) + '|' +
    IntToStr(NonEmptyFields) + '|' + IntToStr(Errors);
  Report.O['summary'].I['records'] := Records;
  Report.O['summary'].I['fields'] := Fields;
  Report.O['summary'].I['nonempty_fields'] := NonEmptyFields;
  Report.O['summary'].I['check_errors'] := Errors;
  Report.B['complete'] := True;
  outputPath := ScriptsPath + '\readback.json';
  Report.SaveToFile(outputPath, False, TEncoding.UTF8, True);
  AddMessage('CLIPBOARD_L10N_' + summary);
  AddMessage('CLIPBOARD_L10N_COMPLETE|' + outputPath);
  Occurrences.Free;
  Report.Free;
end;

end.
