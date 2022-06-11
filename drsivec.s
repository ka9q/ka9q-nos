include asmglobal.h
; DRSI 8530 card interrupt hooks

	extrn	Stktop,Spsave,Sssave,drint:proc,doret:proc,eoi:proc

	.CODE
dbase	dw	@Data		; save loc for ds (must be in code segment)

; dr0vec - DRSI card #0 interrupt handler
	public	dr0vec
	label	dr0vec far
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
	call	drint
	inc	sp
	inc	sp
	jmp	doret

	end
