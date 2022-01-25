Program TestFD80;
Uses Dos, Crt;
function seektrack(track: byte): boolean;
var
  regs: registers;
  buffer: array[1..512] of byte;
begin
  regs.ax := 0; (* Reset first! *)
  regs.dl := 1; (* B drive, assuming A=OS *)
  intr($13,regs); (* Reset disk controller *)
  regs.ah := 2; (* Read sector *)
  regs.al := 1; (* One sector *)
  regs.ch := track; (* What track *)
  regs.cl := 1; (* Sector number *)
  regs.dh := 0; (* Head *)
  regs.dl := 1; (* B drive, assuming A=OS *)
  regs.es := seg(buffer); (* Temp buffer *)
  regs.bx := ofs(buffer); (* Temp buffer *)
  intr($13,regs);
  if ((regs.flags and $01)<>0) then (* Failed? *)
    seektrack := false (* Failed! *)
  else (* Success? *)
    seektrack := true; (* Success *)
end;
var
  track: byte;
Begin
  if (paramcount<>0) then
  begin
    if (paramstr(1)='/?') then
    begin
      writeln('TestFD80 - A program that seeks from track 0 to up to track 80 to check for seek errors');
      writeln;
      halt;
    end;
  end;
  writeln('Starting track seek/read test.');
  writeln('This recalibrates and then seeks to all available tracks on a 80-track disk.');
  for track := 0 to 79 do
  begin
    write('Seeking to track ',track,'...');
    if (not seektrack(track)) then
    begin
      writeln('Failed.');
    end
    else
    begin
      writeln('Success.');
    end;
  end;
End.