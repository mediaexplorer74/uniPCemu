org 100h
section .text

BITS 16
start:
	; Start of the program!
push ax ; Original save
push bx ; Original save
push cx ; Original save
push dx ;Original save
pushf ;Original save

mov dx,0x3cc
in al,dx
push ax ; Original Misc Output save
and al,0xfe ; Read original!
or al,1 ; Color mode!
mov dx,0x3c2
out dx,al ; Write Misc Output Register!
mov dx,0x3d4
in al,dx ; Backup the original index!
push ax ; Original index save!

; Start of testing code
call near ET4000_clearKEY ; Make sure the KEY starts out disabled, since we don't know it's initial status!
mov ax,0x3400 ; Index 34!
call near readCRTC ; Read the index!
mov [originalvaluereadwithoutkey],al ; Save (also the original value is stored)!
call near ET4000_setKEY ; Set the KEY!
call near readCRTC ; Read the index!
mov [originalvaluereadwithkey],al ; Save!
xor al,08h ; Toggle bit 3 (safe?)
mov [valuewrittenwithkeyenabled],al ; Save!
call near writeCRTC ; Write the index!
call near readCRTC ; Read it back!
mov [valueactuallywrittenwithkeyenabled],al ; Save!
call near ET4000_clearKEY ; Clear the KEY!
call near readCRTC ; Read back the value!
mov [valuereadwithkeydisabledandbitsflipped],al ; What is read back without the KEY!
call near ET4000_setKEY ; Set the KEY again
mov al,[originalvaluereadwithkey] ; Original value to restore!
call near writeCRTC ; Write the index!
call near ET4000_clearKEY ; Clear the KEY!
; End of testing code

; Finish up the ET4000!
pop ax ; Pop the original index
mov dx,0x3d4
out dx,al ; Restore the CRTC index!
pop ax ; Pop the original Misc Output Register
mov dx,0x3c2
out dx,al ; Restore the Misc Output Register
;Finish up the program!
; Display results
mov dl,[originalvaluereadwithoutkey] ; What to display!
call near printhex08
call near printCRLF
mov dl,[originalvaluereadwithkey] ; What to display!
call near printhex08
call near printCRLF
mov dl,[valuewrittenwithkeyenabled] ; What to display!
call near printhex08
call near printCRLF
mov dl,[valueactuallywrittenwithkeyenabled] ; What to display!
call near printhex08
call near printCRLF
mov dl,[valuereadwithkeydisabledandbitsflipped] ; What to display!
call near printhex08
call near printCRLF
popf
pop dx
pop cx
pop bx
pop ax
mov ax,0x4c00
int 21h

; readCRTC: IN: ah: index, OUT: al=value read, ah: index
readCRTC:
push dx
mov dx,0x3d4
xchg ah,al ; AL=Index, AH=whatever
out dx,al ; Write the index!
xchg ah,al ; Restore AX. AL=whatever, AH=Index
inc dx
in al,dx ; Read the result
pop dx
ret

; writeCRTC: IN: ah=index, al=value
writeCRTC:
push dx
mov dx,0x3d4
xchg ah,al ; AL=Index, AH=value
out dx,al ; Write the index!
xchg ah,al ; AH=Index, AL=value
inc dx
out dx,al ; Write the result
pop dx
ret

ET4000_setKEY: ; Procedure
push ax
push dx
mov dx,0x3BF
mov al,0x03
out dx,al
mov dl,0xD8
mov al,0xA0
out dx,al
pop dx
pop ax
ret

ET4000_clearKEY: ; Procedure
push ax
push dx
mov dx,0x3BF
mov al,0x01
out dx,al
mov dl,0xD8
mov al,0x29
out dx,al
pop dx
pop ax
ret

printhex08: ; Procedure!
push dx
and dx,0x00ff ; 8-bits only!
push dx
shr dx,4 ; High 4 bits first!
and dx,0xf ; 4 bits only!
call near printhex04
pop dx
and dx,0x000f ; Low 4 bits last!
call near printhex04
pop dx
ret

printhex04: ; Procedure!
push dx
push ax
and dx,0xf ; Limit possibilities to within range!
cmp dl,0xa ; <A
jc isnumber
sub dl,0xa
add dl,'A' ; A-F
jmp endprinthex04 ; Finish up!
isnumber: ; We're a number?
add dl,'0' ; 0-9
endprinthex04:
call printchar ; Print it!
pop ax
pop dx
ret

printchar: ; Procedure!
; DL=character code to write
push ax
push dx
mov ah,2 ; Write character to standard output!
; DL is already set!
int 21h
pop dx
pop ax
ret


printCRLF: ; Procedure
push dx
mov dx,13
call near printchar ;CR
mov dx,10
call near printchar ;LF
pop dx
ret
; uninitialized data
originalvaluereadwithoutkey db 0
originalvaluereadwithkey db 0
valuewrittenwithkeyenabled db 0
valueactuallywrittenwithkeyenabled db 0
valuereadwithkeydisabledandbitsflipped db 0