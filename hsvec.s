; HS ("high speed" 8530 card) interrupt hooks

include	asmglobal.h

	extrn	Stktop,Spsave,Sssave,hsint:proc,doret:proc,eoi:proc

	.CODE
dbase	dw	@Data		; save loc for ds (must be in code segment)

; hs0vec - high speed modem #0 interrupt handler
	public	hs0vec
	label	hs0vec	far
	cld
	push	ds		; save on user stack
	mov	ds,cs:dbase	; establish interrupt data segment

	mov	Sssave,ss	; stash user stack context
	mov	Spsave,sp

	mov	ss,cs:dbase
	lea	sp,Stktop

	PUSHALL
	push	es
	call	eoi

	mov	ax,0		; arg for service routine
	push	ax
	call	hsint
	inc	sp
	inc	sp
	jmp	doret

	end
