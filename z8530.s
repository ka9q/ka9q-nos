; Zilog 8530 general-purpose control subroutines
; Copyright 1991 Phil Karn, KA9Q
include	asmglobal.h
	LOCALS

	.CODE
; Write a 8530 register. Called from C as
; write_scc(int ctl,unsigned char reg,unsigned char val);
	public	write_scc
write_scc	proc
	arg	ctl:word,reg:byte,val:byte
	pushf
	mov	dx,ctl
	mov	al,reg
	cmp	al,0
	jz	@@doit		; no need to set register 0
	cli
	out	dx,al
; Allow enough delay for 16 MHz 80386 and a 4.9152 MHz 8530
; The delay uses a NOP in a loop because of the pipeline in the 80386.
; The loop instruction causes the 80386 to flush the pipeline and reload.
; The 8530 requires a delay of 6 PCLK + 200 ns - a loop of 8 should be
; adequate to a 16 Mhz 80386.
	mov	cx,8
@@w1:	nop
	loop	@@w1
@@doit:	mov	al,val
	out	dx,al
	POPFLAGS
	ret
write_scc	endp

; Read a 8530 register. Called from C as
; unsigned char read_scc(int ctl,unsigned char reg);
	public	read_scc
read_scc	proc
	arg	ctl:word,reg:byte
	pushf
	mov	dx,ctl
	mov	al,reg
	cmp	al,0
	jz	@@doit	; no need to set reg if R0
	cli
	out	dx,al
; allow enough delay for the 8530 to settle (see comments above)
	mov	cx,8
@@r1:	nop
	loop	@@r1
@@doit:	in	al,dx
	mov	ah,0
	POPFLAGS
	ret
read_scc	endp

; Read packets from the 8530 receiver.
; Returns when either a good frame is received, or when carrier drops.
; If a good frame is received, the length is returned; otherwise -1.
	public	rx8530
rx8530	proc
	arg	ctl:word,data:word,buf:ptr,bufsize:word	
	uses	di

@@restart:
	if	@Datasize NE 0
		les	di,buf
	else
		mov	di,buf	; cp = buf
		mov	ax,ds
		mov	es,ax
	endif

	cld
	mov	cx,0		; cnt = 0
@@rxloop:
	mov	dx,ctl	; read status
	in	al,dx		; into al
	test	al,08h		; DCD still present?
	jz	@@dcdoff	; nope, quit
	test	al,080h		; Abort?
	jnz	@@restart	; yes, start again
	test	al,01h		; character available?
	jz	@@nochar	; nope, go on
	mov	dx,data
	in	al,dx		; get it
	stosb			; and stash: *cp++ = char
	inc	cx		; cnt++
	cmp	cx,bufsize	; cx == bufsize?
	jnz	@@rxloop

	mov	dx,ctl	; buffer overflowed; abort receiver
	mov	al,3		; select register 3
	out	dx,al
	mov	al,0d9h		; ENT_HM|RxENABLE|RxCRC_ENAB|Rx8
	nop
	nop
	nop
	nop
	nop
	out	dx,al
	jmp	@@restart
@@nochar:
	mov	al,1		; Select error register (R1)
	mov	dx,ctl
	out	dx,al
	nop
	nop
	nop
	nop
	nop
	nop
	in	al,dx		; read error reg (R1)
	test	al,080h		; end of frame?
	jz	@@rxloop	; nope, keep looking

	test	al,040h		; End of frame. CRC error?
	jnz	@@restart	; yup; start again
	mov	ax,cx		; good frame; return with count
	ret

@@dcdoff:
	mov	ax,0ffffh	; DCD dropped, return -1
	ret

rx8530	endp
	end

