Program COPYDSK;
Uses Dos, Crt;
var
  F: File of Byte;
function readtrack(track: byte): boolean;
var
  regs: registers;
  buffer: array[0..511] of byte;
  b,c,d: integer;
begin
  for c := 1 to 18 do
  begin
    for d := 0 to 1 do
    begin
      regs.ax := 0; (* Reset first! *)
      regs.dl := 0; (* A drive, assuming A=OS *)
      intr($13,regs); (* Reset disk controller *)
      regs.ah := 2; (* Read sector *)
      regs.al := 1; (* One sector *)
      regs.ch := track; (* What track *)
      regs.cl := c; (* Sector number *)
      regs.dh := d; (* Head *)
      regs.dl := 0; (* Drive *)
      regs.es := seg(buffer); (* Temp buffer *)
      regs.bx := ofs(buffer); (* Temp buffer *)
      intr($13,regs);
      if ((regs.flags and $01)<>0) then (* Failed? *)
      begin
        readtrack := false (* Failed! *)
      end
      else (* Success? *)
      begin
        readtrack := true; (* Success *)
        for b := 0 to 511 do
        begin
          write(f,buffer[b]); (* Write to the file *)
        end;
      end;
    end;
  end;
end;
var
  track: byte;
  var
    filename: string;
Begin
  if (paramcount<>0) then
  begin
    if (paramstr(1)='/?') then
    begin
      writeln('COPYDSK [output] - A program that reads from track 0 to up to track 80 to a file (default OUTPUT.IMG)');
      writeln;
      halt;
    end;
    filename := paramstr(1); (* Output filename *)
  end
  else
    filename := 'OUTPUT.IMG'; (* Default filename *)
  assign(f,filename);
  (*$i-*)
  rewrite(f);
  (*$i+*)
  if (IOresult<>0) then
  begin
    writeln('Couldnt open output file: ',filename);
    halt;
  end;
  writeln('Starting track read.');
  writeln('This recalibrates and then reads to all available tracks on a 80-track disk.');
  for track := 0 to 79 do
  begin
    write('Reading track ',track,'...');
    if (not readtrack(track)) then
    begin
      writeln('Failed.');
      close(f);
      halt;
    end
    else
    begin
      writeln('Success.');
    end;
  end;
  close(f);
End.
