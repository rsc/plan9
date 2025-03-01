TEXT asmbackdoor(SB), $0
	MOVL	ureg+0(FP), BP
	MOVL	16(BP), BX
	MOVL	20(BP), DX
	MOVL	24(BP), CX
	MOVL	0(BP), AX
	ANDL	$1, AX
	MOVL	28(BP), AX
	JNZ	out

in:
	INL
	JMP	done

out:
	OUTL

done:
	MOVL	ureg+0(FP), BP
	MOVL	BX, 16(BP)
	MOVL	DX, 20(BP)
	MOVL	CX, 24(BP)
	MOVL	AX, 28(BP)

	RET


	
