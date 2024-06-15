#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "pdp8.h"
#include "console.h"
#include "tty.h"

/* CPU state */
WORD AC;	/* Accumulator */
WORD L;		/* Link */
WORD MQ;	/* Multiplier/Quotient register */
WORD SC;	/* Step counter (5 bits) */
WORD PC;	/* Program counter */
WORD SR;	/* Switch register */
WORD IR;	/* Instruction register */
WORD MA;	/* Memory address register */
WORD MB;	/* Memory buffer register */

/* Memory extension registers */
WORD IF;	/* Instruction field */
WORD DF;	/* Data field */
WORD IB;	/* Instruction buffer */
WORD SF;	/* Save field */

/* Various flip-flops */
BIT RUN;		/* CPU is running */
BIT STOP;		/* CTRL-C was pressed */
BIT IEN;		/* Interrupt enable */
BIT ION_delay;	/* Delay ION by 1 instruction */
BIT CIF_delay;	/* Delay ION until next JMP/JMS */

/* Interrupt request: 64 bits, 1 bit per device */
unsigned long long IREQ;

WORD trace;	/* Trace execution? */

/* Configuration */
BIT HAVE_EAE;	/* Extended arithmetic element */
BIT HAVE_EMEM;	/* Extended memory (> 4K) */

/* Primary memory */
WORD *MP;
size_t memsize;	/* # of words */
int nfields;	/* # of fields */

static void input_output(void);
static void operate(void);
static void skip_group(void);
static void eadd(WORD lastPC);

void cpu_run(
	WORD addr,	/* Initial address */
	WORD count)	/* Number of instructions to run (0=until HLT) */
{
	WORD lastPC;
	int keyb_delay;

	PC = addr;
	RUN = 1;
	keyb_delay = 100; /* Check keyboard every 100 instructions */

	while (RUN) {
		if (ION_delay) {
			IEN = 1;	/* Handle interrupts after the next instruction */
			ION_delay = 0;
		}

		MA = PC;
		IR = MB = MP[MA];
		lastPC = PC;
		PC_INC();
		switch (IR >> 9) {	/* Opcode */
		case 0:	/* AND - Logical AND */
			eadd(lastPC);
			MB = MP[MA];
			AC &= MB;
			break;
		case 1:	/* TAD - Two's complement ADD */
			eadd(lastPC);
			MB = MP[MA];
			ALU_ADD(AC,MB);
			break;
		case 2:	/* ISZ - Increment and skip on zero */
			eadd(lastPC);
			//ALU_INC(MP[MA]);
			MP[MA] = (MP[MA] + 1) & WORD_MASK;
			if (!MP[MA]) PC_INC();
			break;
		case 3:	/* DCA - Deposit and clear accumulator */
			eadd(lastPC);
			MP[MA] = AC;
			AC = 0;
			break;
		case 4:	/* JMS - Jump to subroutine */
			eadd(lastPC);
			IF = IB;
			MA = IF | (MA & WORD_MASK);
			MP[MA] = PC & WORD_MASK;	/* Save return address (12 bits) */
			PC = MA + 1;				/* Code begins at next word */
			break;
		case 5:	/* JMP - Jump */
			eadd(lastPC);
			IF = IB;
			PC = IF | (MA & WORD_MASK);
			/* Detect hot loops of the form:
			  LOOP:	XXX (do something)
					SKIP if YYY
					JMP  LOOP (ie .-2)

				This is probably waiting for an external event, like a key
				press, so we do a keyboard read with 0.5 s timeout. If a key
				has been pressed it will trigger an interrupt. Otherwise we
				will have avoided a hot loop doing nothing by yielding the
				CPU to the OS.
			*/
			if (PC == (lastPC - 2) && ((MP[lastPC-1] & 07400) == 07400)) {
				tty_out_set_flag(4,1);
				tty_keyb_timed_wait1(3);	/* Read 1 char for 0.5 sec */
			}
			break;
		case 6:	/* IOT - Input/output transfer */
			input_output();
			break;
		case 7:	/* OPR - Operate */
			operate();
			break;
		}
		if (trace)
			con_trace(lastPC);
		if (STOP) {
			con_stop();
			RUN = 0;
			STOP = 0;
		}
		if (count && !--count)
			RUN = 0;
		if (!--keyb_delay) {
			tty_keyb_get_flag(3);
			tty_out_set_flag(4,1);
			keyb_delay = 1000;
		}
		if (IREQ && IEN && !ION_delay && !CIF_delay) {
			/* Service interrupt */
			/* JMS 0 in field 0 */
			MP[0] = (PC & WORD_MASK);
			PC = 1;
			IEN = 0;
			SF = (IF >> 9) | (DF >> 12);
			IF = DF = 0;
		}
	}
}

/* Raise/lower interrupt request (for a device) */
void cpu_ireq(int dev, int updown)
{
	assert(dev <= 077);

	if (updown)
		IREQ |= (1 << dev);
	else
		IREQ &= ~(1 << dev);
}

/* Calculate effective memory address */
static void eadd(WORD lastPC)
{
	if (IR & PAGE_BIT)	/* Use current page of current field */
		MA = IF | (lastPC & PAGE_MASK) | (IR & OFF_MASK);
	else				/* Use page 0 of current field */
		MA = IF | (IR & OFF_MASK);
	
	/* Indirect addressing? */
	if (IR & INDIR_BIT) {
		/* Auto-increment? */
		if ((MA & 07770) == 00010)	/* Addresses 0010 to 0017 */
			MP[MA] = (MP[MA] + 1) & WORD_MASK;
		MA = DF | MP[MA];
	}
}

/* Check if next instruction is a JMP .-1 */
static int cpu_is_jmpm1(void)
{
	WORD inst;

	inst = 05000 | PAGE_BIT | ((PC-1) & PAGE_MASK);

	return MP[PC] == inst;
}

static void input_output(void)
{
	int dev = (IR >> 3) & 077;
	int fun = IR & 07;
	int flag;

	/*
	   Handle memory extension separately since it involves
	   several devices (20 to 27).
	*/
	if (HAVE_EMEM && ((IR & 07700) == 06200)) {	/* 62NX */
//		int field = (IR & 00070) << 9;
		int field = (IR & 00070) >> 3;
		switch (IR & 7) {
		case 1:	/* CDF = 62N1 */
#if	1
			if (field < nfields)
				DF = field << 12;
#else
		if ((field>>12) >= nfields) printf("Invalid DF=%d (nfields=%d) @ PC=%05o\r\n",field>>12,nfields,PC-1);
			DF = field;
#endif
			break;
		case 2:	/* CIF = 62N2 */
#if	1
			if (field < nfields) {
				IB = field << 12;
				CIF_delay = 1;
			}
#else
		if ((field>>12) >= nfields) printf("Invalid IF=%d (nfields=%d) @ PC=%05o\r\n",field>>12,nfields,PC-1);
			IB = field;
			CIF_delay = 1;
#endif
			break;
		case 3:	/* CDI = 62N3 = CDF | CIF */
#if	1
			if (field < nfields) {
				DF = IB = field << 12;
				CIF_delay = 1;
			}
#else
		if ((field>>12) >= nfields) printf("Invalid DF and IF=%d (nfields=%d) @ PC=%05o\r\n",field>>12,nfields,PC-1);
			DF = field;
			IB = field;
			CIF_delay = 1;
#endif
			break;
		case 4:
			switch ((IR & 070) >> 3) {
			case 1:	/* RDF = 6214 */
				AC = (AC & 07707) | (DF >> 9);
				break;
			case 2:	/* RIF = 6224 */
				AC = (AC & 07707) | (IF >> 9);
				break;
			case 3:	/* RIB = 6234 */
				AC = SF;
				break;
			case 4:	/* RMF = 6244 */
				IB = (SF & 00070) << 9;
				DF = (SF & 7) << 12;
				break;
			}
		}
		return;
	}

	switch(dev) {
	case 000:	/* CPU */
		if (fun == 1)		/* ION = 6001 */
			ION_delay = 1;	/* Delay 1 instruction */
		else if (fun == 2) {/* IOF = 6002 */
			IEN = 0;
			ION_delay = 0;
		}
		break;
	case 001:	/* High speed paper tape reader */
		break;
	case 002:	/* High speed paper tape punch */
		break;
	case 003:	/* Console keyboard (TTY) / low speed paper tape reader */
		switch(fun) {
		case 0: /* KCF = 6030 */
			/* Clear keyboard/reader flag, do not start reader */
			tty_keyb_set_flag(dev, 0);
			break;
		case 1: /* KSF = 6031 */
			/* Skip is keyboard/flag = 1 */
			/* If next instruction is JMP .-1, wait until a key is pressed */
			if (cpu_is_jmpm1()) flag = tty_keyb_wait1(dev);
			else flag = tty_keyb_get_flag(dev);
			if (flag)
				PC_INC();
			break;
		case 2: /* KCC = 6032 */
			/* Clear AC and keyboard/reader flag, set reader run */
			AC = 0;
			tty_keyb_set_flag(dev, 0);
			break;
		case 3: /* ??? = 6033 */
			break;
		case 4: /* KRS = 6034 */
			/* Read keyboard/reader buffer static */
			break;
		case 5: /* KIE = 6035 */
			/* Interrrupt enable */
			break;
		case 6: /* KRB = 6036 */
			/* Clear AC, read keyboard buffer, clear keyboard flags */
			AC = tty_keyb_inp1(dev);
			break;
		case 7: /* ??? = 6037 */
			break;
		}
		break;
	case 004:	/* Console output (TTY) / low speed paper tape punch */
		switch(fun) {
		case 0: /* SPF = 6040 */
			/* Set teleprinter/punch flag */
			tty_out_set_flag(dev, 1);
			break;
		case 1: /* TSF = 6041 */
			/* Skip if teleprinter/punch flag is 1 */
			//if (tty_out_get_flag(dev))
				PC_INC();
			break;
		case 2: /* TCF = 6042 */
			/* Clear teleprinter/punch flag */
			tty_out_set_flag(dev, 0);
			break;
		case 3: /* ??? = 6043 */
			break;
		case 4: /* TPC = 6044 */
			/* Output AC as 7-bit ASCII */
			tty_out1(dev, AC & 0x7F);
			break;
		case 5: /* SPI = 6045 */
			break;
		case 6: /* TLS = 6046 */
			/* Clear teleprinter/punch flag */
			/* Output AC as 7-bit ASCII */
			tty_out1(dev, AC & 0x7F);
			break;
		case 7: /* ??? = 6047 */
			break;
		}
		break;
	case 010:	/* Memory parity (MP8/I) and Automatic Restart (KP8/I) */
		if (fun == 1) { /* SMP = 6101 */
			/* Skip if memory parity error flag = 0, ie, always */
			PC_INC();
		}
		/* SPL = 6102 = Skip if power low (never) */
		/* CMP = 6104 = Clear memory parity flag (nop) */
		break;
	}
}

static void operate(void)
{
	uint64_t temp;
	int count;

	if (!(IR & GROUP_BIT)) {	/* Group 1 */
		if (CLA(IR)) AC = 0;
		if (CLL(IR)) L = 0;
		if (CMA(IR)) AC = ~AC & WORD_MASK;
		if (CML(IR)) L = !L;
		if (IAC(IR)) ALU_INC(AC);
		if (RT(IR)) { /* Rotate twice */
			if (RAL(IR)) { AC = (AC << 1) | L; L = GET_LINK; AC &= WORD_MASK; }
			if (RAR(IR)) { AC = SET_LINK; L = AC & 1; AC >>= 1; }
		}
		if (RAL(IR)) { AC = (AC << 1) | L; L = GET_LINK; AC &= WORD_MASK; }
		if (RAR(IR)) { AC = SET_LINK; L = AC & 1; AC >>= 1; }
		if (BSW(IR) && !RAR(IR) && !RAL(IR)) { AC = ((AC & BYTE_MASK) << BYTE_BITS) | (AC >> BYTE_BITS); }
	} else if (!(IR & 1)) {		/* Group 2 */
		skip_group();
		if (CLA(IR)) AC = 0;
		if (OSR(IR)) AC = AC | SR;
		if (HLT(IR)) RUN = 0;
	} else {					/* Group 3 */
		/* EAE instructions as in the PDP-8/I */
		/* Sequence 1 */
		if (CLA(IR)) AC = 0;

		/* Sequence 2 */
		/* Decode on bits 5, 6, 7 */
		switch ((IR >> 4) & 07) { /* MQA | SCA | MQL */
		case 0:	/* NOP = 7401 */
			break;
		case 1:	/* MQL = 7421 */
			MQ = AC;
			break;
		case 2:	/* SCA = 7441 */
			AC |= SC;
			break;
		case 3:	/* NOP = 7461 */
			break;
		case 4:	/* MQA = 7501 */
			AC |= MQ;
			break;
		case 5:	/* SWP = 7521 */
			temp = AC;
			AC = MQ;
			MQ = temp;
			break;
		case 6:	/* NOP = 7541 */
			break;
		case 7:	/* NOP = 7561 */
			break;
		}

		/* Sequence 3 */
		/* Decode on bits 8, 9, 10 */
		switch ((IR >> 1) & 07) {
		case 0:		/* NOP = 7401 */
			break;
		case 1:		/* SCL = 7403 */
			SC = ~MP[PC] & SC_MASK;
			PC_INC();
			break;
		case 2:		/* MUY = 7405 */
			/* AC @ MQ = MP[PC] * MQ */
			temp = MP[PC] * MQ;
			AC = temp >> WORD_BITS;
			MQ = temp & WORD_MASK;
			PC_INC();
			break;
		case 3:		/* DVI = 7407 */
			temp = (AC << WORD_BITS) | MQ;
			MQ = temp / MP[PC];
			AC = temp & MP[PC];
			L = 0;
			PC_INC();
			break;
		case 4:		/* NMI = 7411 */
			break;
		case 5:		/* SHL = 7413 */
			count = MP[PC] + 1;
			temp = (AC << WORD_BITS) | MQ;
			temp <<= count;
			AC = (temp >> WORD_BITS) & WORD_MASK;
			MQ = temp & WORD_MASK;
			L = ((temp & LINK_MASK) >> LINK_SHFT);
			PC_INC();
			break;
		case 6:		/* ASR = 7415 */
			count = MP[PC] + 1;
			if (count > (WORD_BITS * 2)) count = WORD_BITS * 2;
			temp = (AC << WORD_BITS) | MQ;
			/* Extend sign */
			/* Hacker's Delight 2-5 */
			temp = ((temp + ACMQ_SIGN_BIT) & ACMQ_MASK) - ACMQ_SIGN_BIT;
			temp >>= count;
			AC = (temp >> WORD_BITS) & WORD_MASK;
			MQ = temp & WORD_MASK;
			PC_INC();
			break;
		case 7:		/* LSR = 7417 */
			count = MP[PC] + 1;
			temp = (AC << WORD_BITS) | MQ;
			temp >>= count;
			AC = (temp >> WORD_BITS) & WORD_MASK;
			MQ = temp & WORD_MASK;
			PC_INC();
			break;
		}
	}
}

static void skip_group(void)
{
	int skip;

	if (!RSS(IR)) {	/* Normal skip sense */
		skip = 0;
		if (SNL(IR) && L) skip = 1;
		if (SZA(IR) && !AC) skip = 1;
		if (SMA(IR) && (AC & SIGN_BIT)) skip = 1;
	} else {		/* Reverse skip sense */
		skip = 1;
		if (SZL(IR) && L) skip = 0;
		if (SNA(IR) && !AC) skip = 0;
		if (SPA(IR) && (AC & SIGN_BIT)) skip = 0;
	}

	if (skip) PC_INC();
}

void cpu_init(size_t kwords)
{
	size_t i;

	memsize = kwords * 1024;
	nfields = kwords / 4;
	MP = (WORD *)malloc(memsize * sizeof(WORD));
	if (kwords > 4) HAVE_EMEM = 1;

	/* Fill memory with halt instructions */
	for (i = 0; i < memsize; ++i)
		MP[i] = HALT;

	/* Initialize registers */
	PC = 0;
	AC = 0;
	L = 0;
	MQ = 0;
	SR = 0;
	DF = 0;
	IF = 0;
	IB = 0;
	IEN = 0;
	IREQ = 0;
	trace = 0;
}

void cpu_stop(void)
{
	STOP = 1;
}
