; Modified from the PE1CHL version to work with NOS.
; This file cannot be used with the PE1CHL sources.
; 1/21/90
; Ken Mitchum, KY3B   km@speedy.cs.pitt.edu km@cadre.dsl.pitt.edu
; SCC interrupt handler for IBM-PC

include asmglobal.h
	extrn	Stktop,Spsave,Sssave,doret:proc,scctim:proc,eoi:proc
	extrn	porg:proc
	extrn	Sccvecloc,Sccpolltab,Sccmaxvec:byte

ifdef	LARGEDATA
	extrn	Sccchan:dword
else
	extrn	Sccchan:word
endif
	
	.CODE
dbase	dw	@Data		; save loc for ds (must be in code segment)

; sccvec is the interrupt handler for SCC interrupts using INTACK

	public sccvec
	label	sccvec	far

	cli			; this code is not re-entrant, so make sure it
				; is not interrupted. some multi-taskers
				; intercept interrupt handlers, so be careful!


	cld
	push	ds		; save on user stack

	mov	ds,cs:dbase

	mov	Sssave,ss	; stash user stack context
	mov	Spsave,sp

	mov	ss,cs:dbase		; set up interrupt stack
	lea	sp,Stktop

	PUSHALL
	push	es
	call	eoi
ifndef	LARGEDATA
	mov	es,ax		; small data assumes ES == DS
endif
	cld			; in case "movsb" or "movsw" is used


; Read SCC interrupt vector and check it

sccint: mov	dx,Sccvecloc
	out	dx,al			; Generate INTACK
	jmp	short d1		; Delay
d1:	jmp	short d2
d2:	jmp	short d3
d3:	in	al,dx			; Read the vector
	cmp	al,Sccmaxvec		; Check for a legal vector
	jnc	clrret			; It should not be >= the maximum
					; If it is, ignore the interrupt

; Extract channel number and status from vector. Determine handler address.

	mov	bl,al			; Copy vector (need it later for status)
	shr	bl,1			; Discard least significant bit
	jc	clrret			; It should not be a 1
	and	bx,7ch			; Isolate channel number (and make word)
	xor	bl,04h			; Toggle A/B channel bit
ifdef	LARGEDATA
	les	si,Sccchan[bx]	; Read address of channel structure
else
	shr	bl,1			; Discard another bit
	mov	si,Sccchan[bx]		; Read address of channel structure
endif
ifdef	LARGEDATA		; Test for NULL
	push	ax
	mov		ax,es
	test	ax,ax
	pop		ax
	jne	nn0
endif
	test	si,si			; Test for NULL
	je	clrret			; No channel struct, ignore it
nn0:
	and	ax,06h			; Isolate status info from vector
	add	ax,ax			; Make index in FAR PTR array
	mov	bx,ax			; It must be in BX

; Call the handler (defined in C), with Sccchan struct as a parameter

	push	es
	push	si			; Put channel struct as a parameter
ifdef	LARGEDATA
	call	dword ptr es:[bx+si]	; Call the handler
else
	call	dword ptr [bx+si]	; Call the handler
endif
	pop	si			; Get channel struct back
	pop	es

; Reset highest priority interrupt

ifdef	LARGEDATA
	mov	dx,es:16[si]		; Get control register address
else
	mov	dx,16[si]		; Get control register address
endif
	mov	al,38h			; "Reset Highest IUS" opcode
	out	dx,al			; to WR0
	jmp	short d4	; settling delay
d4:	jmp	short d5
d5:

; Determine if more interrupt requests are coming in from the SCCs

	jmp	sccint			; keep trying until no vector returned

; Clear the ISR bit in the PIC and return from interrupt

clrret:	
	mov	ax,0		; clear chain vector value (4/92 KA9Q)
	mov	dx,0
	jmp	doret			; execute code in pcint.asm

;	sccvec	endproc

; sccnovec is the interrupt handler for SCC interrupts using polling

	public sccnovec
	label sccnovec far
	
	cli			; this code is not re-entrant, so make sure it
				; is not interrupted. some multi-taskers
				; intercept interrupt handlers, so be careful!


	push	ds		; save on user stack
	mov	ds,cs:dbase

	mov	Sssave,ss	; stash user stack context
	mov	Spsave,sp

	mov	ss,cs:dbase		; set up interrupt stack
	lea	sp,Stktop

	PUSHALL
	push	es
	call	eoi
ifndef	LARGEDATA
	mov	es,ax		; small data assumes ES == DS
endif

	cld			; in case "movsb" or "movsw" is used


; Find the SCC generating the interrupt by polling all attached SCCs
; reading RR3A (the interrupt pending register)

sccintnv:
	lea	si,Sccpolltab		; Point to polling table
sccpoll:
	mov	dx,[si]			; Get chan A CTRL address
	inc	si
	inc	si
	test	dx,dx			; End of table without finding it
	je	clrret			; Then return from interrupt
	mov	al,3			; Select RR3A
	out	dx,al
	jmp	short d6	; Delay
d6:	jmp	short d7
d7:	jmp	short d8
d8:	in	al,dx
	test	al,al			; Test if a nonzero IP here
	jnz	sccip			; Yes, handle it
	inc	si			; No, next A CTRL
	inc	si
	jmp	sccpoll

; Read SCC interrupt vector from RR2B, it should always be correct
; Extract channel number and status from vector. Determine handler address.

sccip:	mov	dx,[si]			; Read B CTRL address
	mov	al,2			; Select RR2B
	out	dx,al
	jmp	short d9	; Delay
d9:	jmp	short d10
d10:	jmp	short d11
d11:	in	al,dx		; Read the vector
	mov	bl,al			; Copy vector (need it later for status)
	shr	bl,1			; Discard least significant bit
	and	bx,7ch			; Isolate channel number (and make word)
	xor	bl,04h			; Toggle A/B channel bit
ifdef	LARGEDATA
	les si,Sccchan[bx]
else
	shr	bl,1			; Discard another bit (Sccchan=words)
	mov	si,Sccchan[bx]		; Read address of channel structure
endif
ifdef	LARGEDATA		; Test for NULL
	push	ax
	mov		ax,es
	test	ax,ax
	pop		ax
	jne nn1
endif
	test	si,si			; Test for NULL
	je	clrret			; No channel struct, ignore it
nn1:
	and	ax,06h			; Isolate status info from vector
	add	ax,ax			; Make index in FAR PTR array
	mov	bx,ax			; It must be in BX

; Call the handler (defined in C), with Sccchan struct as a parameter

	push	es
	push	si			; Put channel struct as a parameter
ifdef	LARGEDATA
	call	dword ptr es:[bx+si]	; Call the handler
else
	call	dword ptr [bx+si]	; Call the handler
endif
	pop	si			; Remove parameter from stack
	pop	es

; Check for more interrupt pending bits

	jmp	sccintnv

;	sccnovec	endproc

	end
