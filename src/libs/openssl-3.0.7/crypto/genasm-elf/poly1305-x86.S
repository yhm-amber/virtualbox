.text
.align	64
.globl	poly1305_init
.type	poly1305_init,@function
.align	16
poly1305_init:
.L_poly1305_init_begin:
	#ifdef __CET__

.byte	243,15,30,251
	#endif

	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%ebp
	xorl	%eax,%eax
	movl	%eax,(%edi)
	movl	%eax,4(%edi)
	movl	%eax,8(%edi)
	movl	%eax,12(%edi)
	movl	%eax,16(%edi)
	movl	%eax,20(%edi)
	cmpl	$0,%esi
	je	.L000nokey
	movl	(%esi),%eax
	movl	4(%esi),%ebx
	movl	8(%esi),%ecx
	movl	12(%esi),%edx
	andl	$268435455,%eax
	andl	$268435452,%ebx
	andl	$268435452,%ecx
	andl	$268435452,%edx
	movl	%eax,24(%edi)
	movl	%ebx,28(%edi)
	movl	%ecx,32(%edi)
	movl	%edx,36(%edi)
	movl	$0,%eax
.L000nokey:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	poly1305_init,.-.L_poly1305_init_begin
.globl	poly1305_blocks
.type	poly1305_blocks,@function
.align	16
poly1305_blocks:
.L_poly1305_blocks_begin:
	#ifdef __CET__

.byte	243,15,30,251
	#endif

	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%edi
	movl	24(%esp),%esi
	movl	28(%esp),%ecx
.L001enter_blocks:
	andl	$-15,%ecx
	jz	.L002nodata
	subl	$64,%esp
	movl	24(%edi),%eax
	movl	28(%edi),%ebx
	leal	(%esi,%ecx,1),%ebp
	movl	32(%edi),%ecx
	movl	36(%edi),%edx
	movl	%ebp,92(%esp)
	movl	%esi,%ebp
	movl	%eax,36(%esp)
	movl	%ebx,%eax
	shrl	$2,%eax
	movl	%ebx,40(%esp)
	addl	%ebx,%eax
	movl	%ecx,%ebx
	shrl	$2,%ebx
	movl	%ecx,44(%esp)
	addl	%ecx,%ebx
	movl	%edx,%ecx
	shrl	$2,%ecx
	movl	%edx,48(%esp)
	addl	%edx,%ecx
	movl	%eax,52(%esp)
	movl	%ebx,56(%esp)
	movl	%ecx,60(%esp)
	movl	(%edi),%eax
	movl	4(%edi),%ebx
	movl	8(%edi),%ecx
	movl	12(%edi),%esi
	movl	16(%edi),%edi
	jmp	.L003loop
.align	32
.L003loop:
	addl	(%ebp),%eax
	adcl	4(%ebp),%ebx
	adcl	8(%ebp),%ecx
	adcl	12(%ebp),%esi
	leal	16(%ebp),%ebp
	adcl	96(%esp),%edi
	movl	%eax,(%esp)
	movl	%esi,12(%esp)
	mull	36(%esp)
	movl	%edi,16(%esp)
	movl	%eax,%edi
	movl	%ebx,%eax
	movl	%edx,%esi
	mull	60(%esp)
	addl	%eax,%edi
	movl	%ecx,%eax
	adcl	%edx,%esi
	mull	56(%esp)
	addl	%eax,%edi
	movl	12(%esp),%eax
	adcl	%edx,%esi
	mull	52(%esp)
	addl	%eax,%edi
	movl	(%esp),%eax
	adcl	%edx,%esi
	mull	40(%esp)
	movl	%edi,20(%esp)
	xorl	%edi,%edi
	addl	%eax,%esi
	movl	%ebx,%eax
	adcl	%edx,%edi
	mull	36(%esp)
	addl	%eax,%esi
	movl	%ecx,%eax
	adcl	%edx,%edi
	mull	60(%esp)
	addl	%eax,%esi
	movl	12(%esp),%eax
	adcl	%edx,%edi
	mull	56(%esp)
	addl	%eax,%esi
	movl	16(%esp),%eax
	adcl	%edx,%edi
	imull	52(%esp),%eax
	addl	%eax,%esi
	movl	(%esp),%eax
	adcl	$0,%edi
	mull	44(%esp)
	movl	%esi,24(%esp)
	xorl	%esi,%esi
	addl	%eax,%edi
	movl	%ebx,%eax
	adcl	%edx,%esi
	mull	40(%esp)
	addl	%eax,%edi
	movl	%ecx,%eax
	adcl	%edx,%esi
	mull	36(%esp)
	addl	%eax,%edi
	movl	12(%esp),%eax
	adcl	%edx,%esi
	mull	60(%esp)
	addl	%eax,%edi
	movl	16(%esp),%eax
	adcl	%edx,%esi
	imull	56(%esp),%eax
	addl	%eax,%edi
	movl	(%esp),%eax
	adcl	$0,%esi
	mull	48(%esp)
	movl	%edi,28(%esp)
	xorl	%edi,%edi
	addl	%eax,%esi
	movl	%ebx,%eax
	adcl	%edx,%edi
	mull	44(%esp)
	addl	%eax,%esi
	movl	%ecx,%eax
	adcl	%edx,%edi
	mull	40(%esp)
	addl	%eax,%esi
	movl	12(%esp),%eax
	adcl	%edx,%edi
	mull	36(%esp)
	addl	%eax,%esi
	movl	16(%esp),%ecx
	adcl	%edx,%edi
	movl	%ecx,%edx
	imull	60(%esp),%ecx
	addl	%ecx,%esi
	movl	20(%esp),%eax
	adcl	$0,%edi
	imull	36(%esp),%edx
	addl	%edi,%edx
	movl	24(%esp),%ebx
	movl	28(%esp),%ecx
	movl	%edx,%edi
	shrl	$2,%edx
	andl	$3,%edi
	leal	(%edx,%edx,4),%edx
	addl	%edx,%eax
	adcl	$0,%ebx
	adcl	$0,%ecx
	adcl	$0,%esi
	adcl	$0,%edi
	cmpl	92(%esp),%ebp
	jne	.L003loop
	movl	84(%esp),%edx
	addl	$64,%esp
	movl	%eax,(%edx)
	movl	%ebx,4(%edx)
	movl	%ecx,8(%edx)
	movl	%esi,12(%edx)
	movl	%edi,16(%edx)
.L002nodata:
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	poly1305_blocks,.-.L_poly1305_blocks_begin
.globl	poly1305_emit
.type	poly1305_emit,@function
.align	16
poly1305_emit:
.L_poly1305_emit_begin:
	#ifdef __CET__

.byte	243,15,30,251
	#endif

	pushl	%ebp
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	20(%esp),%ebp
.L004enter_emit:
	movl	24(%esp),%edi
	movl	(%ebp),%eax
	movl	4(%ebp),%ebx
	movl	8(%ebp),%ecx
	movl	12(%ebp),%edx
	movl	16(%ebp),%esi
	addl	$5,%eax
	adcl	$0,%ebx
	adcl	$0,%ecx
	adcl	$0,%edx
	adcl	$0,%esi
	shrl	$2,%esi
	negl	%esi
	andl	%esi,%eax
	andl	%esi,%ebx
	andl	%esi,%ecx
	andl	%esi,%edx
	movl	%eax,(%edi)
	movl	%ebx,4(%edi)
	movl	%ecx,8(%edi)
	movl	%edx,12(%edi)
	notl	%esi
	movl	(%ebp),%eax
	movl	4(%ebp),%ebx
	movl	8(%ebp),%ecx
	movl	12(%ebp),%edx
	movl	28(%esp),%ebp
	andl	%esi,%eax
	andl	%esi,%ebx
	andl	%esi,%ecx
	andl	%esi,%edx
	orl	(%edi),%eax
	orl	4(%edi),%ebx
	orl	8(%edi),%ecx
	orl	12(%edi),%edx
	addl	(%ebp),%eax
	adcl	4(%ebp),%ebx
	adcl	8(%ebp),%ecx
	adcl	12(%ebp),%edx
	movl	%eax,(%edi)
	movl	%ebx,4(%edi)
	movl	%ecx,8(%edi)
	movl	%edx,12(%edi)
	popl	%edi
	popl	%esi
	popl	%ebx
	popl	%ebp
	ret
.size	poly1305_emit,.-.L_poly1305_emit_begin
.byte	80,111,108,121,49,51,48,53,32,102,111,114,32,120,56,54
.byte	44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32
.byte	60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111
.byte	114,103,62,0
.align	4

	.section ".note.gnu.property", "a"
	.p2align 2
	.long 1f - 0f
	.long 4f - 1f
	.long 5
0:
	.asciz "GNU"
1:
	.p2align 2
	.long 0xc0000002
	.long 3f - 2f
2:
	.long 3
3:
	.p2align 2
4:
