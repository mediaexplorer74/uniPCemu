Program ACLtest;
Uses Dos, Crt;
procedure setGraphicsMode(mode: byte);
var
  r: registers;
begin
  r.ah := 0; (* Set graphics mode *)
  r.al := mode; (* The mode to set *)
  intr($10, r);
end;
procedure setTextMode;
var
  r: registers;
begin
  r.ax := $0003;
  intr($10, r);
end;
function enableACL: word; (* returns the distance between two scanlines in bytes *)
var
  b, x, offsetreg: byte;
  result: word; (* the calculated result *)
begin
  x := port[$3D4];
  port[$3BF] := $03;
  port[$3D8] := $A0; (* enable extensions *)
  port[$3D4] := $36;
  b := port[$3D5];
  b := b or $28;
  port[$3D5] := b;
  port[$3D4] := $13; (* offset register *)
  offsetreg := port[$3D5]; (* retrieve it! *)
  port[$3D4] := x;
  port[$3BF] := $01;
  port[$3D8] := $00; (* disable extensions *)
  result := offsetreg; (* take the offset register... *)
  result := (result shl 3); (* times 2 for the MA offset, *)
  (* times 4 for the VRAM address difference, so times 8. *)
  enableACL := result; (* give the result *)
end;
procedure disableACL;
var
  b, x: byte;
begin
  x := port[$3D4];
  port[$3BF] := $03;
  port[$3D8] := $A0; (* enable extensions *)
  port[$3D4] := $36;
  b := port[$3D5];
  b := b and $D7;
  port[$3D5] := b;
  port[$3D4] := x;
  port[$3BF] := $01;
  port[$3D8] := $00; (* disable extensions *)
end;
procedure waitforkeypress;
var
  c: char;
begin
  c := readkey; (* Wait for input *)
  if (c=chr(0)) then
    c := readkey;
end;
procedure writeVRAM(o: word; val: byte);
begin
  mem[$A000:o] := val;
end;
function readVRAM(o: word) : byte;
begin
  readVRAM := mem[$A000:o];
end;
procedure writeACLreg(o: byte; val: byte);
begin
  mem[$BFF0:o] := val;
end;
function readACLreg(o: byte) : byte;
begin
  readACLreg := mem[$BFF0:o];
end;
procedure waitACL0;
begin
  while ((readACLreg($36) and $01)=$01) do
  begin
  end;
end;
procedure waitACL1;
begin
  while ((readACLreg($36) and $02)=$2) do
  begin
  end;
end;
procedure writeACLregunqueued(addr: byte; val: byte);
begin
  writeACLreg(addr, val);
end;
procedure writeACLregunqueued24(addr: byte; val0: byte; val1: byte; val2: byte);
begin
  writeACLregunqueued(addr,val0);
  writeACLregunqueued(addr+1,val1);
  writeACLregunqueued(addr+2,val2);
end;
procedure writeACLregqueued(addr:byte; val: byte);
begin
  waitACL1;
  waitACL0;
  writeACLreg(addr, val);
  waitACL0;
end;
procedure writeACLregqueued24(addr: byte; val0: byte; val1: byte; val2: byte);
begin
  writeACLregqueued(addr,val0);
  writeACLregqueued(addr+1,val1);
  writeACLregqueued(addr+2,val2);
end;
procedure writeACLregqueued16(addr: byte; val0: byte; val1: byte);
begin
  writeACLregqueued(addr,val0);
  writeACLregqueued(addr+1,val1);
end;
procedure writeACLmemqueued(addr:byte; val: byte);
begin
  waitACL1;
  waitACL0;
  mem[$B800:addr] := val;
  waitACL0;
end;
procedure setmap(a: byte);
var
  b: byte;
  c: byte;
begin
  b := a and $F;
  c := (a and $30);
  port[$3CD] := b or (b shl 4); (* low bits *)
  port[$3CB] := c or (c shr 4); (* high bits *)
end;
procedure performtests(lineoffset: word);
var
  fontA: array[0..7] of byte;
  i: integer;
  o: integer;
  destYoffset: word;
begin
  destYoffset := lineoffset - 1; (* The destination Y offset is diff minus one *)
  fontA[0] := $30;
  fontA[1] := $78;
  fontA[2] := $CC;
  fontA[3] := $CC;
  fontA[4] := $FC;
  fontA[5] := $CC;
  fontA[6] := $CC;
  fontA[7] := $00;
  (* first, initialize the accelerator registers and pattern memory areas *)
  setmap($3F); (* Just below 4MB *)
  for i := 0 to $100 do
    writeVRAM(i, $0f); (* Fill pattern map area at 3F0000 with our specified color *)
  setmap($00);
  (* Now, the accelerator registers to test with *)
  writeACLregunqueued24($0, $00, $00, $00);
  writeACLregunqueued24($4, $00, $00, $00); (* Writing the text at location 0 in VRAM (start of active display) *)
  writeACLregunqueued24($8, $00, $00, $00); (* Writing the text at location 0 in VRAM (start of active display) *)
  writeACLregunqueued($13, $77); (* Writing the text at location 0 in VRAM (start of active display) *)
  writeACLregunqueued($32, $01); (* Sync enable *)
  writeACLregqueued24($80, $00, $00, $3f); (* Pattern address *)
  writeACLregqueued24($84, $00, $00, $3f); (* Source address *)
  writeACLregqueued16($88, $00, $00); (* Pattern Y offset *)
  writeACLregqueued16($8A, $00, $00); (* Source Y offset *)
  writeACLregqueued16($8C, (destYoffset and $ff), ((destYoffset shr 8) and $ff)); (* Destination Y offset *)
  writeACLregqueued($8E, $01); (* Virtual bus size *)
  writeACLregqueued($8F, $00); (* X/Y direction *)
  writeACLregqueued($90, $02); (* X/Y pattern wrap *)
  writeACLregqueued($92, $02); (* X/Y source wrap *)
  writeACLregqueued16($94, $00, $00); (* X position *)
  writeACLregqueued16($96, $00, $00); (* Y position *)
  writeACLregqueued16($98, $07, $00); (* X count *)
  writeACLregqueued16($9A, $07, $00); (* Y count *)
  writeACLregqueued($9C, $02); (* Routing control: mix data *)
  writeACLregqueued($9D, $00); (* Reload control *)
  writeACLregqueued($9E, $AA); (* Background operation: NOP *)
  writeACLregqueued($9F, $CC); (* Foreground operation: Src *)
  writeACLregqueued24($A0, $00, $00, $00); (* Destination address *)
  (* Finally, perform the test operation *)
  for i := 0 to 7 do
  begin
    writeACLmemqueued(i shl 1, fontA[i]); (* Print the character to the display in an accelerated way *)
    writeACLmemqueued((i shl 1) + 1, 0); (* Print the character to the display in an accelerated way *)
  end;

  (* Restore registers *)
  writeACLregunqueued($13, $00); (* Writing the text at location 0 in VRAM (start of active display) *)  
end;
procedure performtest(mode: byte);
begin
  setGraphicsMode(mode); (* Perform tests below using graphics mode requested *)
  (* Start of the test *)
  performtests(enableACL); (* Perform the tests using the ACL *)
  waitforkeypress; (* Wait for a keypress *)
  disableACL; (* Finish the accelerator to clean up *)
  setTextMode; (* Return to text mode *)
  (* End of the test *)
end;
Begin
  performtest($2E); (* Perform tests below using 256-color graphics mode 2E(640x480) *)
  performtest($30); (* Perform tests below using 256-color graphics mode 30(800x600) *)
  performtest($38); (* Perform tests below using 256-color graphics mode 38(1024x768) *)
end.
