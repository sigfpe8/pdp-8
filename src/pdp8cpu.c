#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "pdp8.h"
#include "console.h"
#include "log.h"
#include "papertape.h"
#include "tty.h"

// CPU state
WORD AC;	// Accumulator
WORD L;		// Link
WORD MQ;	// Multiplier/Quotient register
WORD SC;	// Step counter (5 bits)
WORD PC;	// Program counter
WORD SR;	// Switch register
WORD IR;	// Instruction register
WORD MA;	// Memory address register
WORD MB;	// Memory buffer register

// Memory extension registers
WORD IF;	// Instruction field
WORD DF;	// Data field
WORD IB;	// Instruction buffer
WORD SF;	// Save field

// Various flip-flops
BIT RUN;		// CPU is running
BIT STOP;		// CTRL-C was pressed
BIT IEN;		// Interrupt enable
BIT ION_delay;	// Delay ION by 1 instruction
BIT CIF_delay;	// Delay ION until next JMP/JMS

// Auxiliary registers 
WORD THISPC;	// Current PC before it's incremented
WORD BP_NUM;	// Active breakpoint number

// Interrupt request: 64 bits, 1 bit per device
unsigned long long IREQ;

WORD trace;	// Trace execution?

/* Configuration */
BIT HAVE_EAE;		// Extended arithmetic element
BIT HAVE_EMEM;		// Extended memory (> 4K)
BIT HAVE_IOMEC_PPT;	// IOmec paper tape reader/punch 

/* Primary memory */
WORD *MP;
size_t memwords;/* # of words */
int nfields;	/* # of fields */

int keyb_delay;
#define	KEYB_DELAY	1000	// Check keyboard after so many instructions

static void input_output(void);
static void operate(void);
static void skip_group(void);
static void eadd(void);

void cpu_run(
	WORD addr,	/* Initial address */
	WORD count)	/* Number of instructions to run (0=until HLT) */
{

	PC = addr;
	RUN = 1;
	keyb_delay = KEYB_DELAY;

	while (RUN) {
		if (ION_delay) {
			IEN = 1;	/* Handle interrupts after the next instruction */
			ION_delay = 0;
		}

		MA = PC;
		IR = MB = MP[MA];
		THISPC = PC;
		PC_INC();
		switch (IR >> 9) {	/* Opcode */
		case 0:	/* AND - Logical AND */
			eadd();
			MB = MP[MA];
			AC &= MB;
			break;
		case 1:	/* TAD - Two's complement ADD */
			eadd();
			MB = MP[MA];
			ALU_ADD(AC,MB);
			break;
		case 2:	/* ISZ - Increment and skip on zero */
			eadd();
			//ALU_INC(MP[MA]);
			MP[MA] = (MP[MA] + 1) & WORD_MASK;
			if (!MP[MA]) PC_INC();
			break;
		case 3:	/* DCA - Deposit and clear accumulator */
			eadd();
			MP[MA] = AC;
			AC = 0;
			break;
		case 4:	/* JMS - Jump to subroutine */
			eadd();
			IF = IB;
			MA = IF | (MA & WORD_MASK);
			MP[MA] = PC & WORD_MASK;	/* Save return address (12 bits) */
			PC = MA + 1;				/* Code begins at next word */
			break;
		case 5:	/* JMP - Jump */
			eadd();
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
			if (PC == (THISPC - 2) && ((MP[THISPC-1] & 07400) == 07400)) {
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
		if (BP_NUM) {	// Are we leaving a breakpoint?
			MP[THISPC] = HALT; // Yes, restore the HALT
			BP_NUM = 0;
		}
		if (trace)
			con_trace(THISPC, IR);
		if (STOP) {
			con_stop();
			RUN = 0;
			STOP = 0;
		}
		if (count && !--count)
			RUN = 0;
		if (keyb_delay && !--keyb_delay) {
			tty_keyb_get_flag(3);
			tty_out_set_flag(4,1);
			keyb_delay = KEYB_DELAY;
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
static void eadd(void)
{
	if (IR & PAGE_BIT)	/* Use current page of current field */
		MA = IF | (THISPC & PAGE_MASK) | (IR & OFF_MASK);
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
	if ((IR & 07700) == 06200) {	// 62NX
		if (!HAVE_EMEM) return;

		int field = (IR & 00070) >> 3;

		switch (fun) {
		case 1:	// CDF = 62N1
			if (field < nfields)
				DF = field << 12;
			break;
		case 2:	// CIF = 62N2
			if (field < nfields) {
				IB = field << 12;
				CIF_delay = 1;
			}
			break;
		case 3:	// CDI = 62N3 = CDF | CIF
			if (field < nfields) {
				DF = IB = field << 12;
				CIF_delay = 1;
			}
			break;
		case 4:
			switch ((IR & 070) >> 3) {
			case 1:	// RDF = 6214
				AC = (AC & 07707) | (DF >> 9);
				break;
			case 2:	// RIF = 6224
				AC = (AC & 07707) | (IF >> 9);
				break;
			case 3:	// RIB = 6234
				AC = (AC & 7600) | SF;
				break;
			case 4:	// RMF = 6244
				IB = (SF & 00070) << 9;
				DF = (SF & 7) << 12;
				break;
			default:
				log_invalid();
				break;
			}
			break;
		default:
			log_invalid();
			break;
		}
		return;
	}

	switch(dev) {
	case 000:	/* CPU */
		switch (fun) {
			case 0:	// SKON = 6000 skip if interrupt is ON and turn OFF
				if (IEN) PC_INC();
				IEN = 0;
				ION_delay = 0;
				break;
			case 1: // ION  = 6001 turn interrupt ON
				ION_delay = 1;	// Delay 1 instruction
				break;			
			case 2: // IOF  = 6002 turn interrupt OFF
				IEN = 0;
				ION_delay = 0;
				break;
			case 3: // SRQ  = 6003 skip if interrupt request
				if (IREQ) PC_INC();
				break;
			case 4: // GTF  = 6004 get flags
				//   0   1   2   3   4   5   6   7   8   9  10  11
				// +---+---+---+---+---+---+---+---+---+---+---+---+
				// | L |GT |INT|NIT|ION|SUF|SF0|SF1|SF2|SF3|SF4|SF5|
				// +---+---+---+---+---+---+---+---+---+---+---+---+
				AC = (L << 11) | (IEN << 7) | (SF & 077);
				break;
			case 5: // RTF  = 6005 restore flags
				L = AC >> 11;
				SF = AC & 077;
				if (AC & 0200) ION_delay = 1;	// ION
				else {							// IOF
					IEN = 0;
					ION_delay = 0;
				}
				break;
			case 6: // SGT  = 6006 skip on Greater Than flag
				break;
			case 7: // CAF  = 6007 clear all flags
				break;
		}
		break;
	case 001:	// High speed paper tape reader
		switch(fun) {
		case 0: // RPE = 6010
			// Reader Punch Enable
			if (HAVE_IOMEC_PPT)
				ppt_reader_punch_ien(1);
			else
				log_invalid();
			break;
		case 1: // RSF = 6011
			// Reader Skip if Flag
			if (ppt_reader_get_flag())
				PC_INC();
			break;
		case 2: // RRB = 6012
			// Read Reader Buffer, clear the reader flag
			AC |= ppt_reader_get_buffer();
			break;
		case 3: // RRB RSF= 6013 (IOmec only)
			log_invalid();
			break;
		case 4: // RFC = 6014
			// Reader Fetch Character
			// Clear and start reading next character from tape
			ppt_reader_clear_flag();
			break;
		case 5: // RFC RSF = 6015 (IOmec only)
			// IOmec: Skip if End-Of-Tape flag
			if (HAVE_IOMEC_PPT) {
				if (ppt_reader_get_eot())
					PC_INC();
			} else
				log_invalid();
			break;
		case 6: // RRB RFC = 6016
			// Read Reader Buffer and start reading next character from tape
			AC |= ppt_reader_get_buffer();
			ppt_reader_clear_flag();
			break;
		case 7: // RFC RRB RSF = 6017 (IOmec only)
			// IOmec: clear end-of-tape flag
			if (HAVE_IOMEC_PPT)
				ppt_reader_clear_eot();
			else
				log_invalid();
		}
		break;
	case 002:	// High speed paper tape punch
		switch(fun) {
		case 0: // PCE = 6020
			// Punch Clear Enable
			if (HAVE_IOMEC_PPT)
				ppt_reader_punch_ien(0);
			else
				log_invalid();
			break;
		case 1: // PSF = 6021
			// Punch Skip if Flag
			if (ppt_punch_get_flag())
				PC_INC();
			break;
		case 2: // PCF = 6022
			// Punch Clear Flag
			ppt_punch_clear_flag();
			break;
		case 3: // 6023
			log_invalid();
			break;
		case 4: // PPC = 6024
			// Punch Put Character
			ppt_punch_putchar(AC & 0xFF);
			break;
		case 5: // 6025
			log_invalid();
			break;
		case 6: // PLS = 6026
			// Punch Load Sequence
			ppt_punch_clear_flag();
			ppt_punch_putchar(AC & 0xFF);
			break;
		case 7: // 6027
			log_invalid();
		}
		break;
	case 003:	// Console keyboard (TTY) / low speed paper tape reader
		switch(fun) {
		case 0: // KCF = 6030
			// Clear keyboard/reader flag, do not start reader
			tty_keyb_set_flag(dev, 0);
			break;
		case 1: // KSF = 6031
			// Skip is keyboard/flag = 1
			// If next instruction is JMP .-1, wait until a key is pressed
			if (cpu_is_jmpm1()) flag = tty_keyb_wait1(dev);
			else flag = tty_keyb_get_flag(dev);
			if (flag)
				PC_INC();
			break;
		case 2: // KCC = 6032
			// Clear AC and keyboard/reader flag, set reader run
			AC = 0;
			tty_keyb_set_flag(dev, 0);
			break;
		case 3: // ??? = 6033
		case 4: // KRS = 6034
			// Read keyboard/reader buffer static
			log_invalid();
			break;
		case 5: // KIE = 6035
			// Interrrupt enable
			log_invalid();
			break;
		case 6: // KRB = 6036
			// Clear AC, read keyboard buffer, clear keyboard flags
			AC = tty_keyb_inp1(dev);
			keyb_delay = KEYB_DELAY;
			break;
		case 7: // ??? = 6037
			log_invalid();
			break;
		}
		break;
	case 004:	// Console output (TTY) / low speed paper tape punch
		switch(fun) {
		case 0: // SPF = 6040
			// Set teleprinter/punch flag
			tty_out_set_flag(dev, 1);
			break;
		case 1: // TSF = 6041
			// Skip if teleprinter/punch flag is 1
			//if (tty_out_get_flag(dev))
				PC_INC();
			break;
		case 2: // TCF = 6042
			// Clear teleprinter/punch flag
			tty_out_set_flag(dev, 0);
			break;
		case 3: // ??? = 6043
			log_invalid();
			break;
		case 4: // TPC = 6044
			// Output AC as 7-bit ASCII
			tty_out1(dev, AC & 0x7F);
			break;
		case 5: // SPI = 6045
			log_invalid();
			break;
		case 6: // TLS = 6046
			// Clear teleprinter/punch flag
			// Output AC as 7-bit ASCII
			tty_out1(dev, AC & 0x7F);
			break;
		case 7: // ??? = 6047
			log_invalid();
			break;
		}
		break;
	case 010:	// Memory parity (MP8/I) and Automatic Restart (KP8/I)
		if (fun == 1) { // SMP = 6101
			// Skip if memory parity error flag = 0, ie, always
			PC_INC();
		} else {
			log_invalid();
			// SPL = 6102 = Skip if power low (never)
			// CMP = 6104 = Clear memory parity flag (nop)
		}
		break;
	default:
		log_invalid();
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
			log_invalid();
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
		case 7:	/* NOP = 7561 */
			log_invalid();
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
			log_invalid();
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

	memwords = kwords * 1024;
	nfields = kwords / 4;
	MP = (WORD *)malloc(memwords * sizeof(WORD));
	if (kwords > 4) HAVE_EMEM = 1;

	/* Fill memory with halt instructions */
	for (i = 0; i < memwords; ++i)
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

//#define	DEBUG_XMEM
#ifdef	DEBUG_XMEM
/*
/PROGRAM OPERATIONS NI MEMORY FIELD 2
/INSTRUCTION FIELD = 2; DATA FIELD = 2
/CALL A SUBROUTINE IN MEMORY FIELD 1
/ INDICATE CALLING FIELD LOCATION BY THE CONTENTS OF THE DATA FIELD

	FIELD	2

	CIF 10			/ CHANGE TO INSTRUCTION FIELD 1 = 6212
	JMS I SUBRP		/ SUBRP = ENTRY ADDRESS
	CDF 20			/ RESTORE DATA FIELD
	HLT
SUBRP,	SUBR		/ POINTER
					/ CALLED SUBROUTINE IN FIELD 1

20200:  6212   CIF 10
20201:  4604   JMS I       .+3
20202:  6221   CDF 20
20203:  7402   HLT 
20204:  0100   SUBR

	FIELD	1

SUBR,	0			/ REETURN ADDRESS STORED HERE
	CLA
	RDF				/ READ DATA FIELD INTO AC
	TAD RETURN		/ AC = 6202 + DATA FIELD BITS
	DCA	EXIT		/ STORE INSTRUCTION SUBROUTINE
					/ NOW CHANGE DATA FIELD IF DESIRED

EXIT,	0			/ A CIF INSTRUCTION
	JMP I	SUBR
RETURN,	CIF			/ USED TO CALCULATE EXIT INSTRUCTION

10100:  0000   AND 0000
10101:  7200   CLA 
10102:  6214   RDF
10103:  1107   TAD .+4
10104:  3105   DCA .+1
10105:  0000   AND 0000
10106:  5500   JMP I       .-6
10107:  6202   CIF 00
*/

	if (HAVE_EMEM) {
		MP[020200]=06212;
		MP[020201]=04604;
		MP[020202]=06221;
		MP[020203]=07402;
		MP[020204]=00100;

		MP[010100]=00000;
		MP[010101]=07200;
		MP[010102]=06214;
		MP[010103]=01107;
		MP[010104]=03105;
		MP[010105]=00000;
		MP[010106]=05500;
		MP[010107]=06202;

		DF = IF = 2 << 12;
		PC = 020200;
	}
#endif
}

void cpu_stop(void)
{
	STOP = 1;
}

void cpu_deinit(void)
{
	log_close();
}
