Program reboot;
uses dos;
var
  regs: registers;
  currentdisk: byte;
  disklist: array[0..3] of byte;
  currentdisk2: byte;
  oldmem: word;

procedure preparereboot; assembler;
asm
  mov ah,$0D
  int $21
  mov ax,$40
  mov ds,ax
  or byte ptr ds:[$17],$0C
  mov ax,$4F53
  int $15
  mov word ptr ds:[$72],$1234
end;

Begin
  oldmem := memw[$40:$72];
  disklist[0]:=$80;
  disklist[1]:=$81;
  disklist[2]:=$0;
  disklist[3]:=$1;
  if (ParamStr(1)='/?') then
  begin
    writeln;
    writeln('Reboot: performs a reboot.');
    writeln('Usage: reboot [/hard]');
    writeln('/hard: tries to perform a hard reboot, default is soft boot');
    writeln('/soft: tries to perform a soft reboot from floppy, then hdd');
    exit;
  end;
  if (ParamStr(1)='/hard') then
  begin
    fillchar(regs,sizeof(regs),0);
    preparereboot;
    memw[$40:$72] := 0; (* Hard reboot *)
    port[$64] := $FE; (* Try hard reset *)
    intr($19,regs); (* Second option: BIOS  *)
    memw[$40:$72] := oldmem; (* Restore *)
    writeln('Hard reboot failed.');
    exit;
  end;
  if (ParamStr(1)='/soft') then
  begin
    for currentdisk := 0 to sizeof(disklist)-1 do
    begin
      fillchar(regs,sizeof(regs),0);
      currentdisk2 := disklist[currentdisk]; (* The disk to boot *)
      regs.ah := 0;
      regs.dl := currentdisk2;
      intr($13,regs);
      if ((regs.flags and FCarry)=0) then (* Reset? *)
      begin
        regs.ah := 2;
        regs.al := 1;
        regs.ch := 0;
        regs.cl := 1;
        regs.dh := 0;
        regs.dl := currentdisk2;
        regs.es := $7C0;
        regs.bx := $0; (* Protect against overflow by using offset 0! *)
        intr($13,regs);
        if ((regs.flags and FCarry)=0) then (* Read sector? *)
          if (regs.al=1) then
            if ((memw[0:$7dfe]=$aa55) or (disklist[currentdisk]<$80)) then (* Valid to boot? *)
            begin
              preparereboot;
              mem[0:$7f00] := currentdisk2; (* Our disk to boot! *)
              memw[0:$7efc] := $7c00; (* Boot offset *)
              memw[0:$7efe] := 0; (* Boot segment *)
              asm
                mov bx,0
                mov es,bx (* use this for the drive letter *)

                mov ss,bx (* use this for the new stack *)
                mov sp,$7efc (* our return address plus 4 addresses must be 512 bytes past the boot sector *)

                mov bx,$7f00 (* ES:BX=Disk number to boot variable! *)
                mov dl,[es:bx] (* Load the disk number in DL! *)
                retf (*Start the boot loader stored at 0:7efc *)
              end;
            end;
      end;
    end;
    memw[$40:$72] := oldmem; (* Restore *)
    writeln('Soft reboot failed: no bootable disks have been found.');
    exit;
  end;

  preparereboot;
  memw[$40:$72] := 0; (* Hard reset instead *)
  inline($EA/$00/$00/$FF/$FF); (* Perform hard reset manually. *)

  memw[$40:$72] := oldmem; (* Restore *)
  writeln('Hard BIOS reset failed.');
End.