org 100h
section .text

BITS 16
start:
	; Start of the program!
push ax ; Original save
push bx ; Original save
mov ax,10f1h
int 10h
cmp ax,10h
jnz normaldac
push dx ;Original save
mov dx,bx ; What number to print to stdout! The result code!
and dx,0xff ; Only the lower 8 bits are used!
call near printhex08
mov dx,13
call near printchar ;CR
mov dx,10
call near printchar ;LF
pop dx
normaldac:
pop bx
pop ax
mov ax,0x4c00
int 21h

printhex08: ; Procedure!
push dx
and dx,0xff ; 8-bits only!
push dx
shr dx,4 ; High 4 bits first!
and dx,0xf ; 4 bits only!
call near printhex04
pop dx
and dx,0xf ; Low 4 bits last!
call near printhex04
pop dx
ret

printhex04: ; Procedure!
push dx
push ax
and dx,0xf ; Limit possibilities to within range!
cmp dl,0xa ; <A
jc isnumber
add dl,'A' ; A-F
call printchar ; Print it!
jmp endprinthex04 ; Finish up!
isnumber: ; We're a number?
add dl,'0' ; 0-9
call printchar ; Print it!
endprinthex04:
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

section .data
  ; program data
 
section .bss
  ; uninitialized data