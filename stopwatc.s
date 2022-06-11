; Stopwatch primitives - used in sw.c
; Copyright 1991 Phil Karn, KA9Q

include	asmglobal.h
	public stopval,swstart
	.CODE
dbase	dw	@Data

; start the interval timer
swstart	proc	far
	push	ax
; send the mode word to the 8254
	mov	al,0b8h	; select counter 2, write lsb,msb, mode 4, binary
	out	43h,al
; initialize the counter at 0
	xor	al,al
	out	42h,al	; lsb
	out	42h,al	; msb
; gate the counter on
	in	al,61h
	or	al,1
	out	61h,al
	pop	ax
	ret
swstart	endp

; stop the interval timer and return its value
stopval	proc	far
; gate the counter off
	in	al,61h
	and	al,0feh
	out	61h,al
; latch counter 2
	mov	al,080h
	out	43h,al
; get the value
	in	al,42h
	mov	ah,al
	in	al,42h
	xchg	ah,al
	ret
	endp

	end

