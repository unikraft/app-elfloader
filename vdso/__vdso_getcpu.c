struct getcpu_cache {
	unsigned long blob[128 / sizeof(long)];
};

#define GDT_ENTRY_CPUNODE		15
#define __CPUNODE_SEG			(GDT_ENTRY_CPUNODE*8 + 3)
#define X86_FEATURE_RDPID		(16*32+22) /* RDPID instruction */

/* Bit size and mask of CPU number stored in the per CPU data (and TSC_AUX) */
#define VDSO_CPUNODE_BITS		12
#define VDSO_CPUNODE_MASK		0xfff

#define asm_inline asm __inline

#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

#define b_replacement(num)	"664"#num
#define e_replacement(num)	"665"#num

#define alt_end_marker		"663"
#define alt_slen		"662b-661b"
#define alt_total_slen		alt_end_marker"b-661b"
#define alt_rlen(num)		e_replacement(num)"f-"b_replacement(num)"f"

#define OLDINSTR(oldinstr, num)						\
	"# ALT: oldnstr\n"						\
	"661:\n\t" oldinstr "\n662:\n"					\
	"# ALT: padding\n"						\
	".skip -(((" alt_rlen(num) ")-(" alt_slen ")) > 0) * "		\
		"((" alt_rlen(num) ")-(" alt_slen ")),0x90\n"		\
	alt_end_marker ":\n"

#define ALTINSTR_ENTRY(ft_flags, num)					      \
	" .long 661b - .\n"				/* label           */ \
	" .long " b_replacement(num)"f - .\n"		/* new instruction */ \
	" .4byte " __stringify(ft_flags) "\n"		/* feature + flags */ \
	" .byte " alt_total_slen "\n"			/* source len      */ \
	" .byte " alt_rlen(num) "\n"			/* replacement len */

#define ALTINSTR_REPLACEMENT(newinstr, num)		/* replacement */	\
	"# ALT: replacement " #num "\n"						\
	b_replacement(num)":\n\t" newinstr "\n" e_replacement(num) ":\n"

/* alternative assembly primitive: */
#define ALTERNATIVE(oldinstr, newinstr, ft_flags)			\
	OLDINSTR(oldinstr, 1)						\
	".pushsection .altinstructions,\"a\"\n"				\
	ALTINSTR_ENTRY(ft_flags, 1)					\
	".popsection\n"							\
	".pushsection .altinstr_replacement, \"ax\"\n"			\
	ALTINSTR_REPLACEMENT(newinstr, 1)				\
	".popsection\n"

/* Like alternative_input, but with a single output argument */
#define alternative_io(oldinstr, newinstr, ft_flags, output, input...)	\
	asm_inline volatile (ALTERNATIVE(oldinstr, newinstr, ft_flags)	\
		: output : "i" (0), ## input)

inline void vdso_read_cpunode(unsigned *cpu, unsigned *node)
{
	unsigned int p;

	/*
	 * Load CPU and node number from the GDT.  LSL is faster than RDTSCP
	 * and works on all CPUs.  This is volatile so that it orders
	 * correctly with respect to barrier() and to keep GCC from cleverly
	 * hoisting it out of the calling function.
	 *
	 * If RDPID is available, use it.
	 */
	alternative_io ("lsl %[seg],%[p]",
			".byte 0xf3,0x0f,0xc7,0xf8", /* RDPID %eax/rax */
			X86_FEATURE_RDPID,
			[p] "=a" (p), [seg] "r" (__CPUNODE_SEG));

	if (cpu)
		*cpu = (p & VDSO_CPUNODE_MASK);
	if (node)
		*node = (p >> VDSO_CPUNODE_BITS);
}

long __vdso_getcpu(unsigned *cpu, unsigned *node, struct getcpu_cache *unused)
{
	vdso_read_cpunode(cpu, node);
	return 0;
}

long getcpu(unsigned *cpu, unsigned *node, struct getcpu_cache *tcache)
	__attribute__((weak, alias("__vdso_getcpu")));
