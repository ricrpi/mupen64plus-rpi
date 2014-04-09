
.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 2
	.eabi_attribute 30, 6
	.eabi_attribute 18, 4
	.eabi_attribute 28, 1
	.file	"dma_arm.s"
	
	.text
	.align
//----------------------------------- 

// int sl(unsigned int x)

	.global	sl
	.hidden sl
	.type	sl, %function

sl:	
	rev r0, r0				// swap endianness
	mov	pc, lr				// return


// int dma_copy(unsigned int* pTo,unsigned int* pFrom, unsigned int uiLength, unsigned int uiCount, unigned int uiSkip )
// {
//    for(j=0; j<count; j++) {
//        for(i=0; i<length; i++) {
//            dram[dramaddr^3] = spmem[memaddr^3];
//            memaddr++;
//            dramaddr++;
//        }
//        dramaddr+=skip;
//    }
//	 return
// }

	.global	dma_copy
	.hidden dma_copy
	.type	dma_copy, %function

dma_copy:		
	
										
	cmp		r0, #0			// pTo 		(dramaddr)
	beq		E
	cmp		r1, #0			// pFrom		(memaddr)
	beq		E
	cmp		r2, #0			// uiLength	(length)
	beq		E
	cmp		r3, #0			// uiCount	(count)
	beq		E

	push 	{r4-r6}			// save registers

	ldr 	r4, [sp, #12]	// uiSkip		(skip)
	cmp		r4, #0
	beq		E2

L1:	
	mov 	r5, r2			// i = length
L2:	
	ldr		r6, [r1],#4		// load value at pFrom and pFrom += 4
	//rev		r6, r6			// swap bytes 
	str		r6, [r0],#4		// save back to pTo and pTo += 4

	//add		r0, #4			// (dramaddr ++)
	//add		r1, #4			// (memaddr ++)

	subs 	r5, #1			// i--
	bgt		L2				// while i > 0 goto L2
	add		r1, r4			// dramaddr += skip
	subs 	r4, #1			// j--
	bgt 	L1				// while j > 0 goto L1

	pop {r4-r6}				// restore registers

	mov r0, #0
	mov	pc, lr				// return 0

E2:
	pop {r4-r6}				// restore registers

E:
	mov r0, #1
	mov	pc, lr				// return 1
		
