#ifndef	_pdp8_h
#define _pdp8_h

/* Simulator version 0.2 */
#define	MAJVER		0
#define	MINVER		2

typedef unsigned int uint;
typedef unsigned short WORD;
typedef unsigned char BIT;

#define	WORD_BITS		12
#define	WORD_MASK		((1 << WORD_BITS) - 1)
#define	BYTE_BITS		6
#define	BYTE_MASK		((1 << BYTE_BITS) - 1)
#define	SC_BITS			5
#define	SC_MASK			((1 << SC_BITS) - 1)

#define	LINK_MASK		(1 << WORD_BITS)
#define	LINK_SHFT		WORD_BITS

#define	AC_MASK			WORD_MASK
#define	GET_LINK		((AC & LINK_MASK) >> LINK_SHFT)
#define	SET_LINK		((AC | (L << LINK_SHFT)))
#define	ALU_ADD(r,v)	{ r += (v); L ^= (r >> LINK_SHFT); r &= WORD_MASK; }
#define	PC_INC()		PC = (PC & FIELD_MASK) | ((PC + 1) & WORD_MASK);
#define	ALU_INC(r)		ALU_ADD(r,1)


/* Instructions

        3    1   1       7
	+------+---+---+--------------+
	|opcode|ind|pag| page offset  |
	+------+---+---+--------------+
	 0      3   4   5           11

	ind = 0 direct addressing
	      1 indirect addressing
	pag = 0 use page 0
	      1 use current page
*/

/* Bits in PDP-8 notation (big-endian) */
#define	CHKBIT(i,n)	((i) & (1 << (11 - (n))))
#define	CLA(i)		CHKBIT(i,4)		/* Clear accumulator */
#define	CLL(i)		CHKBIT(i,5)		/* Clear link */
#define	CMA(i)		CHKBIT(i,6)		/* Complement accumulator */
#define	CML(i)		CHKBIT(i,7)		/* Complement link */
#define	RAR(i)		CHKBIT(i,8)		/* Rotate accumulator right */
#define	RAL(i)		CHKBIT(i,9)		/* Rotate accumulator left */
#define	RT(i)		CHKBIT(i,10)	/* Rotate twice */
#define	BSW(i)		CHKBIT(i,10)	/* Byte swap */
#define	IAC(i)		CHKBIT(i,11)	/* Increment accumulator */

#define	SMA(i)		CHKBIT(i,5)		/* Skip on minus accumulator */
#define	SPA(i)		CHKBIT(i,5)		/* Skip on positive accumulator */
#define	SZA(i)		CHKBIT(i,6)		/* Skip on zero accumulator */
#define	SNA(i)		CHKBIT(i,6)		/* Skip on non-zero accumulator */
#define	SNL(i)		CHKBIT(i,7)		/* Skip on non-zero link */
#define	SZL(i)		CHKBIT(i,7)		/* Skip on zero link */
#define	IS(i)		CHKBIT(i,8)		/* Invert skip sense */
#define	RSS(i)		CHKBIT(i,8)		/* Reverse skip sense */
#define	OSR(i)		CHKBIT(i,9)		/* OR with switch register */
#define	HLT(i)		CHKBIT(i,10)	/* Halt */
#define	SKP(i)		((i) & 00160)	/* Skip always */
#define	NOP(i)		((i) == 07000)	/* No operation */

#define	MQA(i)		CHKBIT(i,5)		/* Multiplier/Quotient into accumulator */
#define	MQL(i)		CHKBIT(i,7)		/* Multiplier/Quotient load */

#define	HALT		07402	/* Halt */

#define	INDIR_BIT	00400
#define	PAGE_BIT	00200
#define	GROUP_BIT	00400
#define	SIGN_BIT	04000
/* When AC @ MQ are treated as single integer */
#define	ACMQ_SIGN_BIT	040000000
#define	ACMQ_MASK		077777777

#define	OFF_MASK	00177
#define PAGE_MASK	07600
#define	FIELD_MASK	070000
#define	FIELD_SHFT	WORD_BITS
#define	PAGE_SHFT	7

/* CPU state */
extern WORD AC;		/* Accumulator */
extern WORD L;		/* Link */
extern WORD MQ;		/* Multiplier/Quotient register */
extern WORD SC;		/* Step counter (5 bits) */
extern WORD PC;		/* Program counter */
extern WORD SR;		/* Switch register */
extern WORD IR;		/* Instruction register (12 bits) */
extern WORD MA;		/* Memory address register */
extern WORD MB;		/* Memory buffer register */

/* Memory extension registers */
extern WORD IF;		/* Instruction field (I0000) */
extern WORD DF;		/* Data field (D0000) */
extern WORD IB;		/* Instruction buffer (I0000) */
extern WORD SF;		/* Save field (00ID) */

/* Various flip-flops */
extern BIT RUN;		/* CPU is running */
extern BIT STOP;	/* CTRL-C was pressed */
extern BIT IEN;		/* Interrupt enable */
extern BIT ION_delay;/* Delay ION by 1 instruction */
extern BIT CIF_delay;/* Delay ION until next JMP/JMS */

extern unsigned long long IREQ;	/* Interrupt request */

extern WORD trace;	// Trace execution?
extern WORD BP_NUM;	// Active breakpoint number
extern WORD THISPC;	// Current PC before it's incremented

/* Configuration */
extern BIT HAVE_EAE;		// Extended arithmetic element
extern BIT HAVE_EMEM;		// Extended memory (> 4K)
extern BIT HAVE_IOMEC_PPT;	// IOmec paper tape reader/punch 


/* Primary memory */
#define	MAXMEM	4096	/* 4K words */
extern size_t memwords;
extern WORD *MP;

/* Used by the disassembler to represent an instruction */
typedef struct {
	char label[16];
	char name[64];
	char args[16];
	char ascii[5];	/* "XY" or 'A' or '\n' */
	WORD addr;
	WORD inst;
} DINSTR;

/* Implemented by pdp8cpu.c */
extern void	cpu_init(size_t kwords);
extern void cpu_deinit(void);
extern void	cpu_run(WORD addr, WORD count);
extern void cpu_ireq(int dev, int updown);
extern void log_close(void);
extern void log_open(void);

/* Implemented by pdp8asm.c */
extern void	cpu_disasm(DINSTR *pi);
extern int	load_asm(FILE *inp, FILE *out, FILE *err);
extern int	load_bin(FILE *inp, FILE *out, FILE *err);
extern int	load_rim(FILE *inp, FILE *out, FILE *err);
extern int	load_txt(FILE *inp, FILE *out, FILE *err);

#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif

#endif	/* _pdp8_h */
