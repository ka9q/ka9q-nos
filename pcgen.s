; Collection of assembler support routines for NOS
; Copyright 1991 Phil Karn, KA9Q

include	asmglobal.h
	LOCALS
	extrn	ctick:proc
	extrn	kbpoll:proc
	extrn	kbsave:proc
	extrn	ksignal:proc
	public	eoi

; Hardware vector for timer linkage
; We use the timer hardware channel here instead of the indirect BIOS
; channel (1ch) because the latter is sluggish when running under DoubleDos
TIMEVEC	EQU	08h

	.DATA
	public	Intstk,Stktop,Spsave,Sssave,Mtasker,Hashtab,Kbvec
	extrn	Isat:word
Spsave	dw	?		; Save location for SP during interrupts
Sssave	dw	?		; Save location for SS during interrupts
Intstk	dw	1024 dup(?)	; Interrupt working stack
Stktop	equ	$		; SP set here when entering interrupt
Mtasker	db	?		; Type of higher multitasker, if any
Hashtab	db	256 dup(?)	; Modulus lookup table for iphash()
Kbvec	dd	?		; Address of BIOS keyboard handler
	.CODE
dbase	dw	@Data
jtable	dw	l0,l1,l2,l3,l4,l5,l6,l7,l8,l9,l10,l11,l12,l13,l14,l15	
vector	dd	?		; place to stash chained vector
vectlo	equ	word ptr vector
vecthi	equ	word ptr vector+2

; Re-arm 8259 interrupt controller(s)
; Should be called just after taking an interrupt, instead of just
; before returning. This is because the 8259 inputs are edge triggered, and
; new interrupts arriving during an interrupt service routine might be missed.
eoi	proc
	cmp	Isat,1
	jnz	@@1		; Only one 8259, so skip this stuff
	mov	al,0bh		; read in-service register from
	out	0a0h,al		; secondary 8259
	nop			; settling delay
	nop
	nop
	in	al,0a0h		; get it
	or	al,al		; Any bits set?
	jz	@@1		; nope, not a secondary interrupt
	mov	al,20h		; Get EOI instruction
	out	0a0h,al		; Secondary 8259 (PC/AT only)
@@1:	mov	al,20h		; 8259 end-of-interrupt command
	out	20h,al		; Primary 8259
	ret
eoi	endp

; common routine for interrupt return
; Note that all hardware interrupt handlers are expected to return
; the original vector found when the device first attached. We branch
; to it just after we've cleaned up here -- this implements shared
; interrupts through vector chaining. If the original vector isn't
; available, the interrupt handler must return NULL to avoid a crash!
public	doret
	label	doret	far
	cmp	ax,0		; is a chained vector present?
	jne	@@1		; yes
	if	@Datasize NE 0
		cmp	dx,ax
		jne	@@1		; yes
	endif
	pop	es		; nope, return directly from interrupt
	POPALL
	mov	ss,Sssave
	mov	sp,Spsave	; restore original stack context
	pop	ds
	iret

; Code to handle vector chaining
@@1:	mov	cs:vectlo,ax	; stash vector for later branch
	if	@Datasize NE 0
		mov	cs:vecthi,dx
	endif
	pop	es
	POPALL
	mov	ss,Sssave
	mov	sp,Spsave	; restore original stack context
	pop	ds
	if	@Datasize NE 0
		jmp	cs:[vector]	; jump to the original interrupt handler
	else
		jmp	cs:[vectlo]
	endif

; istate - return current interrupt state
	public	istate
istate	proc
	pushf
	pop	ax
	and	ax,200h
	jnz	@@1
	ret
@@1:	mov	ax,1
	ret
istate	endp

; dirps - disable interrupts and return previous state: 0 = disabled,
;	1 = enabled
	public dirps
dirps	proc
	pushf			; save flags on stack
	pop	ax		; flags -> ax
	and	ax,200h		; 1<<9 is IF bit
	jz	@@1		; ints are already off; return 0
	mov	ax,1
	cli			; interrupts now off
@@1:	ret
dirps	endp

; restore - restore interrupt state: 0 = off, nonzero = on
	public	restore
restore	proc
	arg is:word
	test	is,0ffffh
	jz	@@1
	sti
	ret
@@1:	cli	; should already be off, but just in case...
	ret
restore	endp


; multitasker types
NONE		equ	0
DOUBLEDOS	equ	1
DESQVIEW	equ	2
WINDOWS3	equ	3
OS2		equ	4

; Relinquish processor so other task can run
	public	giveup
giveup	proc
	pushf		;save caller's interrupt state
	sti		;re-enable interrupts
	cmp	mtasker, DOUBLEDOS
	jnz	@@1
	mov	al,2	; 110 ms
	mov	ah,0eeh
	int	21h
	POPFLAGS		; restore caller's interrupt state
	ret

@@1:	cmp	mtasker, DESQVIEW
	jnz	@@2
	mov	ax, 1000h
	int	15h
	POPFLAGS		; restore interrupts
	ret

@@2:	cmp	mtasker, WINDOWS3
	jnz	@@3
	mov	ax, 1680h
	int	2fh
	cmp	al, 80h	; call supported?
	jz	@@3	; nope
	POPFLAGS		; yes - restore interrupts
	ret

@@3:	cmp	mtasker, OS2
	jnz	@@4
	mov	ax, 1680h
	int	2fh
	POPFLAGS		; restore interrupts
	ret

@@4:	hlt		; wait for an interrupt

	POPFLAGS		; restore interrupts
	ret
giveup	endp

; check for a multitasker running
	public	chktasker
chktasker	proc
	mov	mtasker,NONE

	; Check for OS/2
	mov	ax,3000h	; Get MS-DOS Version Number call
	int	21h
	cmp	al,20		; Version 20 = OS/2 2.0
	jnz	@@5
	mov	mtasker, OS2
	ret

  	; Check for Microsoft Windows
@@5:	mov	ax,1600h

	; Check for Microsoft Windows
	mov	ax,1600h
	int	2fh
	cmp	al, 00h		; 0 means windows multitasking not running
	jz	@@4
	cmp	al, 80h		; ditto for 80h return
	jz	@@4
	mov	mtasker, WINDOWS3
	ret

	; Check for DoubleDos
@@4:	mov	ah,0e4h
	int	21h
	cmp	al,1
	jz	@@1
	cmp	al,2
	jnz	@@2
@@1:	mov	mtasker, DOUBLEDOS
	ret

	; Check for DESQVIEW
@@2:	mov	ax, 2b01h
	mov	cx, 4445h
	mov	dx, 5351h
	int	21h
	cmp	al, 0ffh
	jnz	@@3
	ret

@@3:	mov	mtasker, DESQVIEW
	ret
chktasker	endp

; getss - Read SS for debugging purposes
	public	getss
getss	proc
	mov	ax,ss
	ret
getss	endp

; clockbits - Read low order bits of timer 0 (the TOD clock)
; This works only for the 8254 chips used in ATs and 386s.
;
; The timer runs in mode 3 (square wave mode), counting down
; by twos, twice for each cycle. So it is necessary to read back the
; OUTPUT pin to see which half of the cycle we're in. I.e., the OUTPUT
; pin forms the most significant bit of the count. Unfortunately,
; the 8253 in the PC/XT lacks a command to read the OUTPUT pin...
;
; The PC's clock design is soooo brain damaged...

	public	clockbits
clockbits	proc
	mov	al,0c2h	; latch timer 0 count and status for reading
@@1:	pushf
	cli		; make chip references atomic
	out	43h,al	; send latch command
	in	al,40h	; get status of timer 0
	mov	bl,al	; save status
	in	al,40h	; get lsb of count
	mov	ah,al	; save lsb
	in	al,40h	; get msb of count
	POPFLAGS		; no more chip references
	test	bl,40h	; test NULL COUNT bit
	jnz	@@1	; count is invalid, try again
	and	bl,80h	; we're only interested in the OUT bit
	xchg	ah,al	; ax = count in correct order
	shr	ax,1	; count /= 2
	jz	@@3	; zero count requires carry propagation
@@2:	or	ah,bl	; combine with OUT bit as most sig bit of count
	ret
@@3:	xor	bl,80h	; propagate carry by toggling OUT bit when cnt == 0
	or	ah,bl	; combine with !OUT bit as most sig bit of count
	ret

clockbits	endp

; Internet checksum subroutine
; Compute 1's-complement sum of data buffer
; Uses an unwound loop inspired by "Duff's Device" for performance
;
; Called from C as
; unsigned short
; lcsum(buf,cnt)
; unsigned short *buf;
; unsigned short cnt;
	public	lcsum
lcsum	proc
	arg	buf:ptr,cnt:word

	if	@Datasize NE 0
		uses	ds,si
		lds	si,buf	; ds:si = buf
	else
		uses	si
		mov	si,buf	; ds:si = buf (ds already set)
	endif

	mov	cx,cnt		; cx = cnt
	cld			; autoincrement si
	mov	ax,cx
	shr	cx,1		; cx /= 16, number of loop iterations
	shr	cx,1
	shr	cx,1
	shr	cx,1

	inc	cx		; make fencepost adjustment for 1st pass
	and	ax,15		; ax = number of words modulo 16
	shl	ax,1		; *=2 for word table index
	lea	bx,jtable	; bx -> branch table
	add	bx,ax		; index into jump table
	clc			; initialize carry = 0
	mov	dx,0		; clear accumulated sum
	jmp	word ptr cs:[bx]	; jump into loop

; Here the real work gets done. The numeric labels on the lodsw instructions
; are the targets for the indirect jump we just made.
;
; Each label corresponds to a possible remainder of (count / 16), while
; the number of times around the loop is determined by the quotient.
;
; The loop iteration count in cx has been incremented by one to adjust for
; the first pass.
; 
deloop:	lodsw
	adc	dx,ax
l15:	lodsw
	adc	dx,ax
l14:	lodsw
	adc	dx,ax
l13:	lodsw
	adc	dx,ax
l12:	lodsw
	adc	dx,ax
l11:	lodsw
	adc	dx,ax
l10:	lodsw
	adc	dx,ax
l9:	lodsw
	adc	dx,ax
l8:	lodsw
	adc	dx,ax
l7:	lodsw
	adc	dx,ax
l6:	lodsw
	adc	dx,ax
l5:	lodsw
	adc	dx,ax
l4:	lodsw
	adc	dx,ax
l3:	lodsw
	adc	dx,ax
l2:	lodsw
	adc	dx,ax
l1:	lodsw
	adc	dx,ax
l0:	loop	deloop		; :-)

	adc	dx,0		; get last carries
	adc	dx,0
	mov	ax,dx		; result into ax
	xchg	al,ah		; byte swap result (8088 is little-endian)
	ret
lcsum	endp

; Link timer handler into timer chain
; Arg == address of timer handler routine
; MUST be called exactly once before uchtimer is called!

toff	dw	?		; save location for old vector
tseg	dw	?		;  must be in code segment

	public	chtimer
chtimer	proc
	arg	vec:far ptr
	uses	ds

	mov	ah,35h		; get current vector
	mov	al,TIMEVEC
	int	21h		; puts vector in es:bx
	mov	cs:tseg,es	; stash
	mov	cs:toff,bx

	mov	ah,25h
	mov	al,TIMEVEC
	lds	dx,vec		; ds:si = vec

	int	21h		; set new vector
	ret
chtimer	endp

; unchain timer handler from timer chain
; MUST NOT be called before chtimer!
	public	uchtimer
uchtimer	proc
	uses	ds

	mov	ah,25h
	mov	al,TIMEVEC
	mov	dx,toff
	mov	ds,tseg
	int	21h		; restore old vector
	ret
uchtimer	endp

; Keyboard hardware interrupt handler.
; First pass the interrupt to the original handler in the BIOS,
; then do a ksignal() to wake up any task sleeping on it.
	public	kbint
	label	kbint	far
	cld
	push	ds
	mov	ds,cs:dbase	; establish interrupt data segment

	pushf			; Make it look like a hardware interrupt
	call dword ptr [Kbvec]	; Call BIOS handler

	mov	Sssave,ss	; stash user stack context
	mov	Spsave,sp
	mov	ss,cs:dbase	; switch to interrupt stack
	lea	sp,Stktop

	PUSHALL
	push	es
	; ksignal(&Kbvec,1);
	mov	ax,1
	push	ax
	push	ds
	mov	ax,offset DGROUP:Kbvec
	push	ax
	call	ksignal
	add	sp,6	; remove args

	pop	es
	POPALL
	mov	ss,Sssave
	mov	sp,Spsave	; restore original stack context
	pop	ds
	iret


; Poll keyboard through BIOS. Returns ascii char in low byte, scan code
; in high byte. If low byte == 0, character is "extended ascii"
	public	kbraw
kbraw	proc
	mov	ah,1	; poll BIOS for character
	int	16h
	jz	nochar	; no character available
	mov	ah,0
	int	16h	; get it for real: ah = scan code, al = ascii char (or 0)
	ret
nochar:	xor	ax,ax
	ret
kbraw	endp

; Clock tick interrupt handler. Note the use of "label" rather than "proc"
; here, necessitated by the fact that "proc" automatically generates BP-saving
; code that we don't want here.

	public	btick
	label	btick	far

	pushf
	cld
	push	ds
	cli
	mov	ds,cs:dbase	; establish interrupt data segment

	mov	Sssave,ss	; stash user stack context
	mov	Spsave,sp

	mov	ss,cs:dbase
	lea	sp,Stktop

	PUSHALL
	push	es

	call	ctick

 	pop	es
	POPALL
	mov	ss,Sssave
	mov	sp,Spsave	; restore original stack context
	pop	ds
	POPFLAGS
	jmp	dword ptr [toff]		; link to previous vector

; Convert 32-bit int in network order to host order (dh, dl, ah, al)
; Called from C as
; int32 get32(char *cp);

	public	get32
get32	proc
	arg	cp:ptr
	if	@Datasize NE 0
		uses	ds,si
		lds	si,cp	; ds:si = cp
	else
		uses	si
		mov	si,cp	; ds:si = cp (ds already set)
	endif

	cld
	lodsw
	mov	dh,al	; high word to dx, a-swapping as we go
	mov	dl,ah
	lodsw
	xchg	al,ah	; low word stays in ax, just swap
	ret
get32	endp

; Convert 16-bit int in network order to host order (ah, al)
; Called from C as
; int16 get16(char *cp);

	public	get16
get16	proc
	arg	cp:ptr
	if	@Datasize NE 0
		uses	ds,si
		lds	si,cp	; ds:si = cp
	else
		uses	si
		mov	si,cp	; ds:si = cp (ds already set)
	endif

	lodsw		; note: direction flag is don't-care
	xchg	al,ah	; word stays in ax, just swap
	ret
get16	endp

; Convert 32-bit int to network order, returning new pointer
; Called from C as
; char *put32(char *cp,int32 x);

	public	put32
put32	proc
	arg	cp:ptr,x:dword
	if	@Datasize NE 0
		uses	ds,di
		les	di,cp	; es:di = cp
		mov	ax,ss	; our parameter is on the stack, and ds might not
		mov	ds,ax	;   be pointing to ss.
	else
		uses	di
		mov	di,cp	; es:di = cp
		mov	ax,ds	; point es at data segment
		mov	es,ax
	endif

	cld
	mov	ax,word ptr (x+2)	; read high word of machine version
	xchg	ah,al			; swap bytes
	stosw				; output in network order
	mov	ax,word ptr x		; read low word of machine version
	xchg	ah,al			; swap bytes
	stosw				; put in network order

	mov	ax,di	; return incremented output pointer
	if	@Datasize NE 0
		mov	dx,es	; upper half of pointer
	endif
	ret
put32	endp

; Convert 16-bit int to network order, returning new pointer
; Called from C as
; char *put16(char *cp,int16 x);

	public	put16
put16	proc
	arg	cp:ptr,x:word
	uses	di
	if	@Datasize NE 0
		les	di,cp	;es:di = cp
	else
		mov	di,cp	; es:di = cp
		mov	ax,ds
		mov	es,ax
	endif
	cld
	mov	ax,x	; fetch source word in machine order
	xchg	ah,al	; swap bytes
	stosw		; save in network order
	mov	ax,di	; return new output pointer to user
	if	@Datasize NE 0
		mov	dx,es	; upper half of pointer
	endif
	ret
put16	endp

if	@CPU AND 2
; fast I/O buffer routines
; version for 80[1234]86 (uses ins, outs instructions)

; outbuf - put a buffer to an output port
	public	outbuf
outbuf	proc
	arg	port:word,buf:ptr,cnt:word
	if	@Datasize NE 0
		uses	ds,si
		lds	si,buf	; ds:si = buf
	else
		uses	si
		mov	si,buf	;ds:si = buf (ds already set)
	endif
	mov	dx,port
	mov	cx,cnt
	cld
	rep outsb		; works only on PC/AT (80286)
	mov	dx,ds
	mov	ax,si		; return pointer just past end of buffer
	ret
outbuf	endp

; inbuf - get a buffer from an input port
	public	inbuf
inbuf	proc
	arg	port:word,buf:ptr,cnt:word
	uses	di
	if	@Datasize NE 0
		les	di,buf		; es:di = buf
	else
		mov	di,buf		; es:di = buf
		mov	ax,ds
		mov	es,ax
	endif
	mov	dx,port
	mov	cx,cnt
	cld
	rep insb		; works only on PC/AT (80286)
	mov	dx,es
	mov	ax,di		; return pointer just past end of buffer
	ret
inbuf	endp

else

; fast buffer I/O routines
; version for 8086/8

; outbuf - put a buffer to an output port
	public	outbuf
outbuf	proc
	arg	port:word,buf:ptr,cnt:word
	if	@Datasize NE 0
		uses	ds,si
		lds	si,buf	; ds:si = buf
	else
		uses	si
		mov	si,buf	; ds:si = buf (ds already set)
	endif

	mov	dx,port
	mov	cx,cnt
	cld

; If buffer doesn't begin on a word boundary, send the first byte
	test	si,1	; (buf & 1) ?
	jz	@@even ; no
	lodsb		; al = *si++;
	out	dx,al	; out(dx,al);
	dec	cx	; cx--;
	mov	cnt,cx	; save for later test
@@even:
	shr	cx,1	; cx = cnt >> 1; (convert to word count)
; Do the bulk of the buffer, a word at a time
	jcxz	@@nobuf	; if(cx != 0){
@@deloop:
	lodsw		; do { ax = *si++; (si is word pointer)
	out	dx,al	; out(dx,lowbyte(ax));
	mov	al,ah
	out	dx,al	; out(dx,hibyte(ax));
	loop	@@deloop	; } while(--cx != 0); }
; now check for odd trailing byte
@@nobuf:
	mov	cx,cnt
	test	cx,1
	jz	@@cnteven
	lodsb		; al = *si++;
	out	dx,al
@@cnteven:
	mov	dx,ds
	mov	ax,si		; return pointer just past end of buffer
	ret
outbuf	endp

; inbuf - get a buffer from an input port
	public	inbuf
inbuf	proc
	arg port:word,buf:ptr,cnt:word
	uses	di
	if	@Datasize NE 0
		les	di,buf	; es:di = buf
	else
		mov	di,buf	; es:di = buf
		mov	ax,ds
		mov	es,ax
	endif
	mov	dx,port
	mov	cx,cnt
	cld

; If buffer doesn't begin on a word boundary, get the first byte
	test	di,1	; if(buf & 1){
	jz	@@bufeven ;
	in	al,dx	; al = in(dx);
	stosb		; *di++ = al
	dec	cx	; cx--;
	mov	cnt,cx	; cnt = cx; } save for later test
@@bufeven:
	shr	cx,1	; cx = cnt >> 1; (convert to word count)
; Do the bulk of the buffer, a word at a time
	jcxz	@@nobuf	; if(cx != 0){
@@deloop:
	in	al,dx	; do { al = in(dx);
	mov	ah,al
	in	al,dx	; ah = in(dx);
	xchg	al,ah
	stosw		; *si++ = ax; (di is word pointer)
	loop	@@deloop	; } while(--cx != 0);
; now check for odd trailing byte
@@nobuf:
	mov	cx,cnt
	test	cx,1
	jz	@@cnteven
	in	al,dx
	stosb		; *di++ = al
@@cnteven:
	mov	dx,es
	mov	ax,di		; return pointer just past end of buffer
	ret
inbuf	endp

endif

	public	longdiv

; long unsigned integer division - divide an arbitrary length dividend by
; a 16-bit divisor. Replaces the dividend with the quotient and returns the
; remainder. Called from C as
;
; unsigned short
; longdiv(unsigned short divisor,int cnt,unsigned short *dividend);
;
;Register usage:
; di - divisor
; si - pointer into dividend array
; cx - loop counter, initialized to the number of 16-bit words in the dividend
; ax - low word of current dividend before each divide, current quotient after
; dx - remainder from previous divide carried over, becomes high word of
;      dividend for next divide

longdiv	proc
	arg	divisor:word,cnt:word,dividend:ptr
	if	@Datasize NE 0
		uses	ds,si,di
		lds	si,dividend
	else
		uses	si,di
		mov	si,dividend	;si -> dividend array
	endif

	cmp	divisor,0		; divisor == 0?
	jne	@2			; no, ok
	xor	ax,ax			; yes, avoid divide-by-zero trap
	jmp	short @1

@2:	mov	dx,0			; init remainder = 0
	mov	cx,cnt			; init cnt
	mov	di,divisor		; cache divisor in register

@@deloop:
	mov	ax,word ptr [si]	; fetch current word of dividend
	cmp	ax,0			; dividend == 0 ?
	jne	@7			; nope, must do division
	cmp	dx,0			; remainder also == 0?
	je	@4			; yes, skip division, continue

@7:	div	di			; do division
	mov	word ptr [si],ax	; save quotient

@4:	inc	si			; next word of dividend
	inc	si
	loop 	@@deloop

	mov	ax,dx			; return last remainder
@1:	ret

longdiv	endp

; long unsigned integer multiplication - multiply an arbitrary length
; multiplicand by a 16-bit multiplier, leaving the product in place of
; the multipler, returning the carry. Called from C as
;
; unsigned short
; longmul(unsigned short multiplier,int cnt,unsigned short *multiplier);
;
; Register usage:
; di = multiplier
; si = pointer to current word of multiplicand
; bx = carry from previous round
; cx = count of words in multiplicand
; dx,ax = scratch for multiply

	public longmul
longmul	proc	far
	arg	multiplier:word,n:word,multiplicand:ptr
	if	@Datasize NE 0
		uses	ds,si,di
		lds	si,multiplicand
	else
		uses	si,di
		mov	si,multiplicand	; si -> multiplicand array
	endif

	mov	di,multiplier		; cache multiplier in register
	xor	bx,bx			; init carry = 0
	mov	cx,n			; fetch n
	mov	ax,cx
	shl	ax,1			; *2 = word offset
	add	si,ax			; multiplicand += n

@@deloop:
	dec	si
	dec	si			; work from right to left
	mov	ax,word ptr [si]	; fetch current multiplicand
	or	ax,ax			; skip multiply if zero
	jz	@@nomult
	mul	di			; dx:ax <- ax * di
@@nomult:
	add	ax,bx			; add carry from previous multiply
	mov	word ptr [si],ax	; save low order word of product
	mov	bx,0			; clear previous carry, leaving CF alone
	adc	bx,dx			; save new carry
	xor	dx,dx			; clear in case we skip the next mult
	loop	@@deloop

	mov	ax,bx			; return final carry
	ret
longmul	endp

ifdef	notdef
; divide 32 bits by 16 bits, returning both quotient and remainder
; This allows C programs that need both to avoid having to do two divisions
;
; Called from C as
;	long divrem(dividend,divisor)
;	long dividend;
;	short divisor;
;
;	The quotient is returned in the low 16 bits of the result,
;	and the remainder is returned in the high 16 bits.

	public	divrem
divrem	proc
	arg	dividend:dword,divisor:word
	mov	ax,word ptr dividend
	mov	dx,word ptr (dividend+2)
	div	divisor
	ret
divrem	endp
endif	

; General purpose hash function for IP addresses
; Uses lookup table Hashtab[] initialized in iproute.c
; Called from C as
; char hash_ip(int32 ipaddr);

	public hash_ip
hash_ip	proc
	arg	ipaddr:dword
	lea	bx,Hashtab
	mov	ax,word ptr ipaddr
	xor	ax,word ptr (ipaddr+2)
	xor	al,ah
	xlat
	xor	ah,ah
	ret
hash_ip	endp

; Compute int(log2(x))
; Called from C as
; int ilog2(int16 x);

	public	ilog2
ilog2	proc
	arg	x:word
	mov	cx,16
	mov	ax,x
@@2:	rcl	ax,1 
	jc	@@1
	loop	@@2
@@1:	dec	cx
	mov	ax,cx
	ret
ilog2	endp
	end
