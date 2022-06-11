; :ts=8
include	asmglobal.h
	extrn	Stktop,Spsave,Sssave,piint:proc,doret:proc,eoi:proc

DMAEN	equ	4

	.DATA

	public	acc_delay
acc_delay dw	0


	.CODE
dbase	dw	@Data		; save loc for ds (must be in code segment)

	public	mloop
mloop	proc
	mov cx,65535
Loop7:
	nop
	loop Loop7
	ret
mloop	endp

	public	wrtscc
wrtscc	proc
	arg	cbase:word,ctl:word,reg:word,val:word

	pushf
	cli

	mov dx,cbase	; get address of dma enable port
	add dx,DMAEN
	mov al,0	; Write a 0 to disable DMA while we touch the scc
	out dx,al

	mov cx,acc_delay
Loop1:	nop
	loop Loop1

	mov dx,ctl	; Get address of scc control reg
	mov ax,reg	; Select register
	out dx,al

	mov cx,acc_delay
Loop2:	nop
	loop Loop2

	mov ax,val	; Output value
	out dx,al

	mov cx,acc_delay
Loop3:	nop
	loop Loop3

	mov dx,cbase	; get address of dma enable port
	add dx,DMAEN
	mov al,1	; Enable DMA
	out dx,al

	POPFLAGS
	ret
wrtscc	endp

	public	rdscc
rdscc	proc
	arg	cbase:word,ctl:word,reg:byte
	pushf
	cli

	mov dx,cbase	; Get address of dma enable port
	add dx,DMAEN;
	mov al,0	; Disable DMA while we touch the scc
	out dx,al;

	mov cx,acc_delay
Loop4:	nop
	loop Loop4

	mov dx,ctl	; Get address of SCC control reg
	mov al,reg	; Select register
	out dx,al;

	mov cx,acc_delay
Loop5:	nop
	loop Loop5

	in al,dx	; read register
	xor ah,ah
	push ax		; save return value

	mov cx,acc_delay
Loop6:	nop
	loop Loop6

	mov dx,cbase	; get address of dma enable port
	add dx,DMAEN
	mov al,1	; Enable DMA
	out dx,al

	pop	ax	; recover return value
	POPFLAGS
	ret
rdscc	endp

; pi0vec - Pi card #0 interrupt handler
	public	pi0vec
	label	pi0vec far
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
	call	piint
	inc	sp
	inc	sp
	jmp	doret

; pi1vec - Pi card #1 interrupt handler
	public	pi1vec
	label	pi1vec far
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

	mov	ax,1		; arg for service routine
	push	ax
	call	piint
	inc	sp
	inc	sp
	jmp	doret

; pi2vec - Pi card #2 interrupt handler
	public	pi2vec
	label	pi2vec far
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

	mov	ax,2		; arg for service routine
	push	ax
	call	piint
	inc	sp
	inc	sp
	jmp	doret

	end
