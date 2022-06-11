; global include file for all .s files to specify model and define macros
	.MODEL	USE16 LARGE,C
	%MACS
	.LALL

	if	@DataSize NE 0
	LARGEDATA	EQU	1
	endif

; Pop CPU flags. On the 286 this uses an alternate sequence to avoid
; a bug on early versions of the 286
POPFLAGS	macro
	if	@Cpu AND 8
		popf		; popf okay on the 386/486

	elseif	@Cpu AND 4	; use alternate sequence on the 286
		JMP	$+3	;Jump over IRET
		IRET		;Pops CS, IP, FLAGS
		PUSH	CS	;Removed by IRET
		CALL	$-2	;Pushes IP, removed by IRET
	else
		popf		; popf okay on the 8086/88/186
	endif	
	endm

; Push all general purpose registers. If 386/486, push 32-bit regs
; to support C code compiled with the -3 option
PUSHALL	macro
	if	@Cpu AND 8	; PUSHAD available to protect 32-bit regs
		pushad
	elseif	@Cpu AND 4	; PUSHA available
		pusha
	else
		push	ax		; save user regs on interrupt stack
		push	bx
		push	cx
		push	dx
		push	bp
		push	si
		push	di
	endif
	endm

; Pop all general purpose registers
POPALL	macro
	if	@Cpu AND 8	; PUSHAD available to protect 32-bit regs
		popad
		nop		; Avoid bug on some early 386's
	elseif	@Cpu AND 4	; PUSHA available
		popa
	else
		pop	di
		pop	si
		pop	bp
		pop	dx
		pop	cx
		pop	bx
		pop	ax
	endif
	endm
