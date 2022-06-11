; Packet driver interrupt hooks

include	asmglobal.h
	extrn	pkint:proc

	.DATA
	extrn	Stktop

	.CODE
sssave	dw	?
spsave	dw	?
dbase	dw	@Data

if	@Cpu AND 8	; temp storage for POPAD operations
disave	dw	?
endif

; pkvec0 - Packet driver receive call handler #0
	public	pkvec0
	label	pkvec0	far
	pushf			; save his interrupt state
	cld
	cli			; no distractions
	mov	cs:sssave,ss	; stash user stack context
	mov	cs:spsave,sp

	mov	ss,cs:dbase	; set up interrupt stack
	lea	sp,Stktop
	if @Cpu AND 8
		PUSHALL		; Protect 32-bit regs
	endif
	push	ds		; save for caller
	mov	ds,cs:dbase
	push	ax
	push	cx
	mov	ax,0	; device #0
	push	ax
	call	pkint
	jmp	pkret

; pkvec1 - Packet driver receive call handler #1
	public	pkvec1
	label	pkvec1	far
	pushf			; save his interrupt state
	cld
	cli			; no distractions
	mov	cs:sssave,ss	; stash user stack context
	mov	cs:spsave,sp

	mov	ss,cs:dbase	; set up interrupt stack
	lea	sp,Stktop
	if @Cpu AND 8
		PUSHALL		; Protect 32-bit regs
	endif
	push	ds		; save for caller
	mov	ds,cs:dbase
	push	ax
	push	cx
	mov	ax,1	; device #1
	push	ax
	call	pkint
	jmp	pkret


; pkvec2 - Packet driver receive call handler #2
	public	pkvec2
	label	pkvec2	far
	pushf			; save his interrupt state
	cld
	cli			; no distractions
	mov	cs:sssave,ss	; stash user stack context
	mov	cs:spsave,sp

	mov	ss,cs:dbase	; set up interrupt stack
	lea	sp,Stktop
	if @Cpu AND 8
		PUSHALL		; Protect 32-bit regs
	endif
	push	ds		; save for caller
	mov	ds,cs:dbase
	push	ax
	push	cx
	mov	ax,2	; device #2
	push	ax
	call	pkint
	jmp	pkret


; common return for all packet drivers
	label	pkret	near
	mov	es,dx	; move returned pointer from DX:AX to ES:DI
	mov	di,ax
	pop	ax	; pop dev # arg	
	pop	cx
	pop	ax
	pop	ds		; restore for caller
	if	@Cpu AND 8	; POPAD available
		mov cs:disave,di
		POPALL		; restore all regs but DI 
		mov di,cs:disave
	endif

	mov	ss,cs:sssave
	mov	sp,cs:spsave	; restore original stack context
	POPFLAGS
	retf

	end
