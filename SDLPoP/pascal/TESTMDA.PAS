Program TestMDA;
uses dos,crt;
function hexify(number: byte): char;
begin
  if (number>=10) then hexify := chr(number+ord('A')-10)
  else hexify := chr(number+ord('0')); (* Simple number! *)
end;
function int2hex(value: byte): string;
var
  result: string[3];
begin
  result := '00'; (* We're two nibbles! *)
  result[1] := hexify(value shr 4); (* high nibble  *)
  result[2] := hexify(value and $F); (* low nibble *)
  int2hex := result; (* Give the result! *)
end;

procedure switchmode(mode: byte);
var
  regs: registers;
begin
  regs.ax := mode; (* Our mode! *)
  intr($10,regs); (* Switch! *)
end;

procedure incxy(var x: integer; var y:integer);
begin
  x := x + 1;
  if (x>=80) then
  begin
    x := 0;
    y := y + 1;
  end;
end;

procedure setxy(x: integer; y: integer; c: char; a: byte);
begin
    mem[$B000:(160*y)+(x shl 1)] := ord(c);
    mem[$B000:(160*y)+(x shl 1)+1] := a;
end;

var
  w: word;
  c: char;
  s: string;
  x, y: integer;
begin
  switchmode(7); (* Our mode! *)
  clrscr;
  x := 0;
  y := 0;
  for w := 0 to 255 do
  begin
    s := int2hex(w); (* Show the value *)
    setxy(x,y,s[1],w);
    incxy(x,y);
    setxy(x,y,s[2],w);
    incxy(x,y);
    if ((w and $1F)=$1F) then (* End of row? *)
    begin
      y := y + 1;
      x := 0; (* Start of next row! *)
    end;
  end;

  if (readkey=chr(0)) then c := readkey; (* Wait for keypress!  *)
  switchmode(3); (* Return to normal mode! *)
end.