Program Highres256;
uses dos,crt,UniPCemu;

var
  id: string;
  regs: registers;
  x,y: integer;
  color: byte;
  status: boolean;
  params: array[1..2] of byte;
  result: array[1..2] of byte;
  paramlength: word;
  resultlength: word;
  r: real;
  colors: array[0..639] of byte; (* all colours precalculated! *)

begin
  id := getDebuggerID; (* get the debugger id! *)
  regs.AH := 0;
  regs.AL := $2E;
  intr($10,regs); (* switch to 640x480x256 mode *)

  status := resetDebugger(id); (* Try and reset the debugger! *)
  if (status) then (* Debugger ready? *)
  begin
    params[1] := 1; (* enable *)
    paramlength := 1; (* 1 byte param *)
    resultlength := 1; (* One byte result *)
    status := debuggerSendCommand(id,$00,$03,@params,paramlength,@result,@resultlength); (* Start logging VGA accesses *)
  end;

  for x := 0 to 639 do
  begin
    r := (x/640.0)*256.0; (* Draw all 256 colors horizontally! *)
    color := trunc(r); (* The color to draw *)
    colors[x] := color; (* Save the precalculated horizontal color *)
  end;

  for y := 0 to 479 do (* process rows in ascending order! *)
  begin
    for x := 0 to 639 do (* process pixels from left to right *)
    begin
      regs.AH := $C;
      regs.AL := colors[x]; (* apply the precalculated color to the display *)
      regs.BH := 0;
      regs.CX := x;
      regs.DX := y;
      intr($10,regs); (* Put a pixel! *)
    end;
  end;

  if (status) then
  begin
    params[1] := 0; (* disable *)
    paramlength := 1; (* 1 byte param *)
    resultlength := 1; (* One byte result *)
    status := debuggerSendCommand(id,$00,$03,@params,paramlength,@result,@resultlength); (* Start logging VGA accesses *)
    paramlength := 0;
    resultlength := 0;
    status := debuggerSendCommand(id,$00,$00,@params,0,@result,@resultlength); (* Disable the x86EMU extensions *)
  end;

  regs.AH := 0;
  regs.AL := 2;
  intr($10,regs); (* switch back to text mode *)

  if (not status) then
  begin
    writeln('Error: couldnt clean up fully. Debugger might be left in the wrong state!');
  end
end.
