unit UniPCemu;
interface
type
  pointingb = ^Byte;
  pointingw = ^Word;
function getDebuggerID: string;
function resetDebugger(id: string): boolean;
function debuggerSendCommand(id: string; group: byte;
functionnr: byte;
parameters: pointingb;
parameterlength: word;
result: pointingb;
resultlength: pointingw): boolean;
implementation
function getDebuggerID: string;
var
  id: string[255];
  s: string[2];
begin
  id := ''; (* Init ID *)
  if (port[$E9]<>$E9) then (* No debugger? *)
  begin
    getDebuggerID := ''; (* No debugger *)
    exit;
  end;
  s[0] := chr(1); (* 1 long *)
  s[1] := chr(port[$EA]); (* Read ID byte *)
  while (ord(s[1])<>$FF) do s[1] := chr(port[$EA]); (* Read ID byte *) (* Not finished yet? *)

  s[1] := chr(port[$EA]); (* Read ID byte *)
  while (ord(s[1])<>$FF) do (* Not finished yet? *)
  begin
    id := id + s; (* Add character *)
    s[1] := chr(port[$EA]); (* Read ID byte *)
  end;

  if (id='') then (* No debugger *)
  begin
    getDebuggerID := ''; (* No debugger *)
    exit;
  end;
  getDebuggerID := id; (* The ID! *)
end;

function resetDebugger(id: string): boolean;
var
  i: integer;
begin
  port[$E9] := 13;
  port[$E9] := 10; (* Newline to debugger *)

  for i := 1 to length(id) do
  begin
    port[$E9] := ord(id[i]); (* Write the enable debugger string *)
  end;

  port[$E9] := 13;
  port[$E9] := 10; (* Newline to debugger *)

  if (port[$EA]<>0) then (* Failed to start debugger? *)
  begin
    resetdebugger := false; (* error *)
    exit;
  end;

  resetDebugger := true; (* Ready! *)
end;

function debuggerSendCommand(id: string; group: byte;
functionnr: byte;
parameters: pointingb;
parameterlength: word;
result: pointingb;
resultlength: pointingw): boolean;
var
  aresult: byte;
  counter: word;
  dummy: boolean;
  aresultlength: word; (* the length of the result *)
label
  skipparameters,
  skipresultphase;
begin
  port[$EA] := group; (* Group byte *)
  aresult := port[$EA]; (* result *)
  if (aresult<>1) then (* invalid group? *)
  begin
    debuggerSendCommand := false; (* error *)
    exit;
  end;
  port[$EA] := functionnr; (* Function byte *)
  aresult := port[$EA]; (* result *)
  if (aresult<>1) then (* invalid group? *)
  begin
    debuggerSendCommand := false; (* error *)
    exit;
  end;
  if ((parameters=Nil) or (parameterlength=0)) then goto skipparameters; (* no parameters? skip them! *)
  for counter := 1 to parameterlength do (* Process parameters! *)
  begin
    port[$EA] := parameters^; (* write a parameter byte *)
    inc(parameters); (* next parameter *)
  end;
  aresult := port[$EA]; (* read the status *)
  case (aresult) of
  3: begin (* parameters acn, result phase *)
       goto skipparameters; (* result phase *)
    end;
  2: begin (* parameters acn, reset to command mode *)
       debuggerSendCommand := true; (* success *)
       exit;
    end;
  1: begin (* too many/few parameters *)
       debuggerSendCommand := false; (* error *)
       exit;
    end;
  0: begin (* error, reset to command mode *)
       debuggerSendCommand := false; (* error *)
       exit;
    end;
  else (* unknown case? *)
    begin
      dummy := resetDebugger(id); (* silent reset *)
      debuggerSendCommand := false; (* error *)
      exit;
    end;
  end;
  skipparameters: (* Check for result phase *)
  if ((result=nil) or (resultlength^=0)) then (* no result buffer/phase? *)
  begin
    debuggerSendCommand := true; (* success *)
    exit;
  end;
  aresultlength := port[$EA]; (* low byte *)
  aresultlength := aresultlength or (port[$EA] shl 8); (* high byte *)
  if (aresultlength>0) then (* result data? *)
  begin
    if (aresultlength>resultlength^) then (* too long result to contain? *)
    begin
      goto skipresultphase; (* skip: buffer too small *)
    end;
    for aresultlength := aresultlength downto 1 do (* process result *)
    begin
      result^ := port[$EA]; (* write result byte *)
      inc(result); (* next location *)
    end;
  end;
  skipresultphase: (* verification phase *)
  port[$EA] := 0; (* trigger verification phase by writing to the port *)
  aresult := port[$EA]; (* read the result status *)
  case aresult of
  4: begin (* OK *)
      debuggerSendCommand := true; (* success *)
    end;
  0: begin (* error or too much/not enough read *)
      debuggerSendCommand := false; (* error *)
    end;
  else begin
      dummy := resetDebugger(id); (* reset us *)
      debuggerSendCommand := false; (* error *)
    end;
  end;
end;

end.
