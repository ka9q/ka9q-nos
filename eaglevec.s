; Eagle (8530 card) interrupt hooks

include	asmglobal.h

	extrn	Stktop,Spsave,Sssave,egint:proc,doret:proc,eoi:proc

	.CODE
dbase	dw	@Data		; save loc for ds (must be in code segment)

; eg0vec - Eagle card #0 interrupt handler
	public	eg0vec
	label	eg0vec far
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
	call	egint
	inc	sp
	inc	sp
	jmp	doret

	end
