; HAPN (8273 HDLC) interrupt hooks

include	asmglobal.h

	extrn	Stktop,Spsave,Sssave,haint:proc,doret:proc,eoi:proc

	.CODE
dbase	dw	@Data		; save loc for ds (must be in code segment)

; ha0vec - HAPN card #0 interrupt handler
	public	ha0vec
	label	ha0vec	far
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
	call	haint
	inc	sp
	inc	sp
	jmp	doret

	end
