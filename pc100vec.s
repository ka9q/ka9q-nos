; PC-100 (8530 card) interrupt hooks

include	asmglobal.h
	extrn	Stktop,Spsave,Sssave,pcint:proc,doret:proc,eoi:proc

	.CODE
dbase	dw	@Data		; save loc for ds (must be in code segment)

; pc0vec - PC-100 card #0 interrupt handler
	public	pc0vec

	label	pc0vec	far
	cld
	push	ds		; save on user stack
	mov	ds,cs:dbase

	mov	Sssave,ss	; stash user stack context
	mov	Spsave,sp

	mov	ss,cs:dbase
	lea	sp,Stktop

	PUSHALL
	push	es
	call	eoi

	mov	ax,0		; arg for service routine
	push	ax
	call	pcint
	inc	sp
	inc	sp
	jmp	doret

	end
