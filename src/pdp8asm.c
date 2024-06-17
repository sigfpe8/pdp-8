#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include "pdp8.h"

typedef struct {
	int  opcode;	/* Numeric op-code */
	char *name;		/* Instruction name or mnemonic */
	char *descr;	/* Short description */
} INSTR;

static const INSTR main_opcodes[] = {
	{ 00000,	"AND",	"Logical 'and'"				},
	{ 01000,	"TAD",	"2's complement 'add'"		},
	{ 02000,	"ISZ",	"Increment and skip on zero"},
	{ 03000,	"DCA",	"Deposit and clear AC"		},
	{ 04000,	"JMS",	"Jump to subroutine"		},
	{ 05000,	"JMP",	"Jump"						},
	{ 06000,	"IOT",	"I/O transfer"				},
	{ 07000,	"OPR",	"Operate"					},
	{ 00000,	0,		0							}
};

static const INSTR group1_opr[] = {
	{ 07000,	"NOP",	"No operation"				},
	{ 07001,	"IAC",	"Increment AC"				},
	{ 07004,	"RAL",	"Rotate AC and L left"		},
	{ 07006,	"RTL",	"Rotate 2 left"				},
	{ 07010,	"RAR",	"Rotate AC and L right"		},
	{ 07012,	"RTR",	"Rotate 2 right"			},
	{ 07020,	"CML",	"Complement L"				},
	{ 07040,	"CMA",	"Complement AC"				},
	{ 07041,	"CIA",	"Two's complement AC"		},
	{ 07100,	"CLL",	"Clear L"					},
	{ 07120,	"STL",	"Set L=1"					},
	{ 07200,	"CLA",	"Clear AC"					},
	{ 07204,	"GLK",	"Get L into AC[11]"			},
	{ 07240,	"STA",	"Set AC=-1"					},
	{ 00000,	0,		0							}
};

static const INSTR group2_opr[] = {
	{ 07402,	"HLT",	"Halt"						},
	{ 07404,	"OSR",	"'Or' AC with SR"			},
	{ 07410,	"SKP",	"Skip"						},
	{ 07420,	"SNL",	"Skip if L!=0"				},
	{ 07430,	"SZL",	"Skip if L==0"				},
	{ 07440,	"SZA",	"Skip if AC==0"				},
	{ 07450,	"SNA",	"Skip if AC!=0"				},
	{ 07500,	"SMA",	"Skip if AC<0"				},
	{ 07510,	"SPA",	"Skip if AC>=0"				},
	{ 07604,	"LAS",	"Load AC with SR"			},
	{ 00000,	0,		0							}
};

static const INSTR eae_opr[] = {
	{ 07405,	"MUY",	"Multiply"					},
	{ 07407,	"DVI",	"Divide"					},
	{ 07411,	"NMI",	"Normalize"					},
	{ 07413,	"SHL",	"Arithmetic shift left"		},
	{ 07415,	"ASR",	"Arithmetic shift right"	},
	{ 07417,	"LSR",	"Logical shift right"		},
	{ 07421,	"MQL",	"MQ=AC, AC=0"				},
	{ 07441,	"SCA",	"AC=SC"						},
	{ 07403,	"SCL",	"Load SC from memory"		},
	{ 07501,	"MQA",	"AC|=MQ"					},
	{ 07621,	"CAM",	"AC=0, MQ=0"				},
	{ 00000,	0,		0							}
};

/* Extended memory IOT's special handling */
static const INSTR emem_iot[] = {
	{ 06201,	"CDF",	"Change DF"					},
	{ 06202,	"CIF",	"Change IF"					},
	{ 06203,	"CDI",	"Change DF and IF"			},
	{ 06214,	"RDF",	"Read DF into AC"			},
	{ 06224,	"RIF",	"Read IF into AC"			},
	{ 06234,	"RIB",	"Read IB into AC"			},
	{ 06244,	"RMF",	"Restore memory fields"		},
	{ 00000,	0,		0							}
};

/* Device 00: Internal opcodes */
static const INSTR dev00_opcodes[] = {
	{ 06000,	"SKON",	"Skip if int ON and turn OFF"	},
	{ 06001,	"ION",	"Turn interrupt ON"				},
	{ 06002,	"IOF",	"Turn interrupt OFF"			},
	{ 06003,	"SRQ",	"Skip interrupt request"		},
	{ 06004,	"GTF",	"Get interrupt flags"			},
	{ 06005,	"RTF",	"Restore interrupt flags"		},
	{ 06006,	"SGT",	"Skip on Greater Than flag"		},
	{ 06007,	"CAF",	"Clear all flags"				},
	{ 00000,	0,		0								}
};

/* Device 01: High speed paper tape reader */
static const INSTR dev01_opcodes[] = {
	{ 06010,	0,		0								},
	{ 06011,	"RSF",	"Skip if reader flag = 1"		},
	{ 06012,	"RRB",	"Read char and clear flag"		},
	{ 06013,	0,		0								},
	{ 06014,	"RFC",	"Clear flag and start read"		},
	{ 06015,	0,		0								},
	{ 06016,	0,		0								},
	{ 06017,	0,		0								},
	{ 00000,	0,		0								}
};

/* Device 02: High speed paper tape punch */
static const INSTR dev02_opcodes[] = {
	{ 06020,	0,		0								},
	{ 06021,	"PSF",	"Skip if punch flag = 1"		},
	{ 06022,	"PCF",	"Clear punch flag and buffer"	},
	{ 06023,	0,		0								},
	{ 06024,	"PPC",	"Punch char"					},
	{ 06025,	0,		0								},
	{ 06026,	"PLS",	"Clear flag, punch char"		},
	{ 06027,	0,		0								},
	{ 00000,	0,		0								}
};

/* Device 03: TTY keyboard/low speed paper tape reader */
static const INSTR dev03_opcodes[] = {
	{ 06030,	"KCF",	"Clear flag"					},
	{ 06031,	"KSF",	"Skip if flag=1"				},
	{ 06032,	"KCC",	"Clear AC/flag, set Reader run" },
	{ 06033,	0,		0,								},
	{ 06034,	"KRS",	"Read buffer static"			},
	{ 06035,	"KIE",	"Keyboard interrupt enable"		},
	{ 06036,	"KRB",	"Clear AC/flags read buffer"	},
	{ 06037,	0,		0								},
	{ 00000,	0,		0								}
};

/* Device 04: TTY printer/low speed paper tape punch */
static const INSTR dev04_opcodes[] = {
	{ 06040,	"SPF",	"Set tty flag"					},
	{ 06041,	"TSF",	"Skip if tty flag = 1"			},
	{ 06042,	"TCF",	"Clear tty flag"				},
	{ 06043,	0,		0								},
	{ 06044,	"TPC",	"Print char"					},
	{ 06045,	"SPI",	"Skip if tty interrupt"			},
	{ 06046,	"TLS",	"Print char and clear flag"		},
	{ 06047,	0,		0								},
	{ 00000,	0,		0								}
};

/*
   Devices 05, 06 and 07:
   Osciloscope display (VC8/I)
   Precision CRT display (30N)
   Light pen (370)
*/
static const INSTR dev05_opcodes[] = {
	{ 06050,	0,		0								},
	{ 06051,	"DCX",	"Clear X coordinate buffer"		},
	{ 06052,	0,		0								},
	{ 06053,	"DXL",	"Load X coordinate buffer"		},
	{ 06054,	"DIX",	"Intensify point (X,Y)"			},
	{ 06055,	0,		0								},
	{ 06056,	0,		0								},
	{ 06057,	"DXS",	"Load X and intensify (X,Y)"	},
	{ 00000,	0,		0								}
};

static const INSTR dev06_opcodes[] = {
	{ 06060,	0,		0								},
	{ 06061,	"DCY",	"Clear Y coordinate buffer"		},
	{ 06062,	0,		0								},
	{ 06063,	"DYL",	"Load Y coordinate buffer"		},
	{ 06064,	"DIY",	"Intensify point (X,Y)"			},
	{ 06065,	0,		0								},
	{ 06066,	0,		0								},
	{ 06067,	"DYS",	"Load Y and intensify (X,Y)"	},
	{ 00000,	0,		0								}
};

static const INSTR dev07_opcodes[] = {
	{ 06070,	0,		0								},
	{ 06071,	"DSF",	"Skip if display flag = 1"		},
	{ 06072,	"DCF",	"Clear display flag"			},
	{ 06073,	0,		0								},
	{ 06074,	"DSB",	"Zero brightness"				},
	{ 06075,	"DSB",	"Set minimum brightness"		},
	{ 06076,	"DSB",	"Set medium brightness"			},
	{ 06077,	"DSB",	"Set maximum brightness"		},
	{ 00000,	0,		0								}
};

/* Device 10: Power low (KP8/I) and memory parity (MP8/I) */
static const INSTR dev10_opcodes[] = {
	{ 06100,	0,		0								},
	{ 06101,	"SMP",	"Skip if memory parity error=0"	},
	{ 06102,	"SPL",	"Skip if power low"				},
	{ 06103,	0,		0								},
	{ 06104,	"CMP",	"Clear memory parity error flag"},
	{ 06105,	0,		0								},
	{ 06106,	0,		0								},
	{ 06107,	0,		0								},
	{ 00000,	0,		0								}
};

/* Devices 2X: Memory extension (MC8/I) */

static const INSTR *device_opcodes[64] = {
	/* 00 */	dev00_opcodes,
	/* 01 */	dev01_opcodes,
	/* 02 */	dev02_opcodes,
	/* 03 */	dev03_opcodes,
	/* 04 */	dev04_opcodes,
	/* 05 */	dev05_opcodes,
	/* 06 */	dev06_opcodes,
	/* 07 */	dev07_opcodes,

	/* 10 */	dev10_opcodes,
	/* 11 */	0,
	/* 12 */	0,
	/* 13 */	0,
	/* 14 */	0,
	/* 15 */	0,
	/* 16 */	0,
	/* 17 */	0,

	/* 20 */	0,
	/* 21 */	0,
	/* 22 */	0,
	/* 23 */	0,
	/* 24 */	0,
	/* 25 */	0,
	/* 26 */	0,
	/* 27 */	0,

	/* 30 */	0,
	/* 31 */	0,
	/* 32 */	0,
	/* 33 */	0,
	/* 34 */	0,
	/* 35 */	0,
	/* 36 */	0,
	/* 37 */	0,

	/* 40 */	0,
	/* 41 */	0,
	/* 42 */	0,
	/* 43 */	0,
	/* 44 */	0,
	/* 45 */	0,
	/* 46 */	0,
	/* 47 */	0,

	/* 50 */	0,
	/* 51 */	0,
	/* 52 */	0,
	/* 53 */	0,
	/* 54 */	0,
	/* 55 */	0,
	/* 56 */	0,
	/* 57 */	0,

	/* 60 */	0,
	/* 61 */	0,
	/* 62 */	0,
	/* 63 */	0,
	/* 64 */	0,
	/* 65 */	0,
	/* 66 */	0,
	/* 67 */	0,

	/* 70 */	0,
	/* 71 */	0,
	/* 72 */	0,
	/* 73 */	0,
	/* 74 */	0,
	/* 75 */	0,
	/* 76 */	0,
	/* 77 */	0
};

/* Pseudo-instructions (directives) */
#define	PSEUDO_CONTINUE		1
#define	PSEUDO_DECIMAL		2
#define	PSEUDO_DEFINE		3
#define	PSEUDO_DUBL			4
#define	PSEUDO_EXPUNGE		5
#define	PSEUDO_FIELD		6
#define	PSEUDO_FIXTAB		7
#define	PSEUDO_FLTG			8
#define	PSEUDO_OCTAL		9
#define	PSEUDO_PAGE			10
#define	PSEUDO_PAUSE		11
#define	PSEUDO_TEXT			12

/* Set to 1 when instruction symbols are inserted */
static int symbol_init;

#define	SYMLEN	6	/* Max 6 chars */

typedef struct symbol {
	struct symbol *next;
	WORD value;
	char type;
	char len;
	char name[SYMLEN];
} SYMBOL;

//static void 	symb_display(int type);
static SYMBOL	*symb_get(int len, char *name);
static int		symb_hash(int len, char *pnt);
static void		symb_init(void);
static void		symb_insert(int len, char *name, WORD value, int type);

/* Symbol types */
#define	SYMB_OPCODE		1
#define	SYMB_PSEUDO		2
#define	SYMB_USER		3
#define	SYMB_MACRO		4

#define	HASH_SIZE	32	/* Power of 2 */
#define	HASH_MASK	(HASH_SIZE-1)

SYMBOL *hash_table[HASH_SIZE];

FILE *lex_inp;
FILE *lex_err;
char Line[128];
SYMBOL *Tksym;
char *Tkbeg;	/* Token's 1st character */
char *Tkend;	/* Character after token */
int Tklen;		/* Length of symbol token */
int Token;		/* '0'=number, 'S'=symbol, punctuation=itself */
int Tkvalue;	/* Token value if number */
int Radix = 8;

#define	MAXLITS	128

typedef struct literal {
	int		page;
	int		nlits;
	WORD	table[MAXLITS];
} PAGETAB;

int clc;			/* Current location counter */
int pass;			/* Assembler pass: 1 or 2 */

PAGETAB *curpage;	/* Ptr to current page table */
PAGETAB page0;		/* Page 0 */
PAGETAB pagen;		/* Current page */

void	asm_clear_pages(void);
int		asm_elem(void);
void 	asm_emit_lits(FILE *out, PAGETAB *pt);
int		asm_expr(void);
int		asm_literal(PAGETAB *pt);
void	asm_pseudo(void);
void 	asm_set_page(FILE *out);

/* Initialize lexer */
void lex_init(void)
{
	Tkend = Line;
}

/* Advance to next token */
int lex_next(void)
{
	Tkbeg = Tkend;
	while (isspace(*Tkbeg)) ++Tkbeg;
	if (!*Tkbeg || *Tkbeg == '/') {
		Tkend = Tkbeg;
		Token = 0;
		return 0; /* End of line token */
	}

	/* Alpha */
	if (isalpha(*Tkbeg)) {
		Token = 'S';
		Tkend = Tkbeg + 1;
		while (isalnum(*Tkend)) ++Tkend;
		Tklen = Tkend - Tkbeg > SYMLEN ? SYMLEN : Tkend - Tkbeg;
		Tksym = symb_get(Tklen, Tkbeg);
		return 'S';	/* Symbol token */	
	}

	/* Number */
	if (isdigit(*Tkbeg)) {
		Token = '0';
		Tkvalue = (int)strtol(Tkbeg,&Tkend,Radix);
		if (Tkend == Tkbeg) printf("Invalid number in radix %d\n",Radix);
		return '0';	/* Number token */
	}

	/* Punctuation */
	if (*Tkbeg == '"') { /* Single character text facility */
		Token = '0';
		if ((Tkvalue = *(Tkbeg + 1))) {
			Tkvalue |= 0200;	/* Mark bit */
			Tkend = Tkbeg + 2;
		} else Tkend = Tkbeg + 1;
		return '0';
	}
	Token = *Tkbeg;
	Tkend = Tkbeg + 1;
	return Token;	/* The token is the character itself */
}

int asm_literal(PAGETAB *pt)
{
	int value = asm_expr();

	for (int i = 1; i <= pt->nlits; ++i) {
		if (pt->table[MAXLITS - i] == (WORD)value)
			return (pt->page << PAGE_SHFT) | (MAXLITS - i);	// Reuse existing literal
	}
	// Insert new literal
	if (++pt->nlits > MAXLITS) printf("Reached max # of literals in page %d\n", pt->page);
	pt->table[MAXLITS - pt->nlits] = (WORD)value;

	return (pt->page << PAGE_SHFT) | (MAXLITS - pt->nlits);
}

/*
	We only keep track of page 0 and the current page.
	If we later get back to a previous page, its old
	literals will be lost.
*/
void asm_set_page(FILE *out)
{
	int new_page = clc >> PAGE_SHFT;

	if (new_page != curpage->page) {
		/* Emit literals from previous page */
		asm_emit_lits(out, curpage);
		if (new_page) {	/* Initialize page literals */
			curpage = &pagen;
			curpage->page = new_page;
			curpage->nlits = 0;
		} else			/* Preserve page 0 literals */
			curpage = &page0;
	}
}

void asm_clear_pages(void)
{
	page0.page = 0;
	page0.nlits = 0;

	clc = 0200;
	curpage = &pagen;
	pagen.page = clc >> PAGE_SHFT;
	pagen.nlits = 0;
}

void asm_emit_lits(FILE *out, PAGETAB *pt)
{
	WORD addr;
	WORD *table;
	int nlits;
	
	nlits = pt->nlits;
	table = pt->table;
	addr = (pt->page << PAGE_SHFT) | (MAXLITS - nlits);

	if (out) {
		for (int i = nlits; i >= 1; --i, ++addr)
			fprintf(out, "%04o %04o\n", addr, table[MAXLITS - i]);
	} else {
		for (int i = nlits; i >= 1; --i, ++addr)
			MP[addr] = table[MAXLITS - i];
	}
}

/* Token already points to the current element */
int asm_elem(void)
{
	int value;

	switch (Token) {
	case '(':	/* Current page literal */
		lex_next();
		value = asm_literal(curpage);
		/* Closing parenthesis is optional */
		if (Token == ')') lex_next();
		break;
	case '[':	/* Page 0 literal */
		lex_next();
		value = asm_literal(&page0);
		/* Closing bracket is optional */
		if (Token == ']') lex_next();
		break;
	case '.':	/* Current location */
		value = clc;
		break;
	case '-':	/* Unary minus */
		lex_next();
		value = (-asm_elem()) & WORD_MASK;
		break;
	case '0':	/* Number */
		value = Tkvalue;
		break;
	case 'S':	/* Symbol */
		if (Tksym)
			value = Tksym->value;
		else {
			/* Undefined symbol */
			/* Treat as 0 in pass 1 */
			value = 0;
			if (pass == 2)
				printf("Undefined symbol in expression: %.*s\n",Tklen,Tkbeg);
		}
		break;
	default:
		value = 0;
		break;
	}

	return value;
}

/* Token already points to the first element */
int asm_expr(void)
{
	int value, value2;
	int addr;
	int opr;

	/* <expr> = <elem> [<opr> <elem>]* */
	/* <opr> = + | - | ! | & */

	/* Stop expression at: 0 | ) | ] | ; */

	/* First element in expression */
	value = asm_elem();

	/* If first element is an opcode, check for arguments */
	if (Token == 'S' && Tksym && Tksym->type == SYMB_OPCODE) {
		/* Memory Reference Instruction? */
		if ((value >> 9) < 6) { /* Yes, e.g. TAD <addr> */
			opr = lex_next();
			if (opr == 'S' && (Tkend - Tkbeg) == 1 && *Tkbeg == 'I') {
				value |= INDIR_BIT;
				opr = lex_next();
			}
			addr = asm_expr();
			if (addr > OFF_MASK) value |= PAGE_BIT;
			value |= (addr & OFF_MASK);
			opr = 0;
		} else { /* Microinstructions to be OR'ed */
			opr = lex_next();
			while (opr && opr != ';') {
				value |= asm_elem(); /* E.g. CLA CLL CMA */
				opr = lex_next();
			}
		}
	} else
		opr = lex_next();

	while (opr) {
		if (opr == ')' || opr == ']' || opr == ';')
			break;
		lex_next();
		value2 = asm_elem();

		switch(opr) {
		case '+':
			value = (value + value2) & WORD_MASK;
			break;
		case '-':
			value = (value - value2) & WORD_MASK;
			break;
		case '!':
			value = (value | value2) & WORD_MASK;
			break;
		case '&':
			value = (value & value2) & WORD_MASK;
			break;
		default:
			printf("Invalid operator: %C (%d)\n", opr, opr);
			return 0;
		}

		opr = lex_next();
	}

	return value;
}

void asm_pseudo(void)
{
	switch (Tksym->value) {
	case PSEUDO_CONTINUE:
		break;
	case PSEUDO_DECIMAL:
		Radix = 10;
		break;
	case PSEUDO_DEFINE:
		break;
	case PSEUDO_DUBL:
		break;
	case PSEUDO_EXPUNGE:
		break;
	case PSEUDO_FIELD:
		break;
	case PSEUDO_FIXTAB:
		break;
	case PSEUDO_FLTG:
		break;
	case PSEUDO_OCTAL:
		Radix = 8;
		break;
	case PSEUDO_PAGE:
		break;
	case PSEUDO_PAUSE:
		break;
	case PSEUDO_TEXT:
		break;
	}

	lex_next();
}

/* Process one logical line */
void asm_line(FILE *out, FILE *err)
{
	int gencode;	/* Generate code? */
	int code;

	gencode = 1;

	switch (Token) {
	case '*':	/* Set origin */
		lex_next();
		clc = asm_expr();
		asm_set_page(out);
		gencode = 0;
		break;
	case '.':	/* Expression involving clc */
		code = asm_expr();
		break;
	case 'S':	/* Symbol */
		if (*Tkend == '=') {	/* Definition (symb=expr) */
			if (pass == 1) {
				char *symb = Tkbeg;
				int value;
				++Tkend;	/* Skip = */
				lex_next();
				value = asm_expr();
				symb_insert(Tklen, symb, (WORD)value, SYMB_USER);
			} else Token = 0;
			gencode = 0;
			break;
		}
		if (*Tkend == ',') {	/* Label */
			if (pass == 1)
				symb_insert(Tklen, Tkbeg, (WORD)clc, SYMB_USER);
			++Tkend;		/* Skip , */
			lex_next();
		} else {	/* Pseudo-instruction? */
			if (Tksym && Tksym->type == SYMB_PSEUDO) {
				asm_pseudo();
				gencode = 0;
				break;
			}
		}
		code = asm_expr();
		break;
	case '0':	/* Number */
		code = asm_expr();
		break;
	default:
		gencode = 0;
		fprintf(err, "Invalid expression %s\n", Tkbeg);
		break;
	}

	if (gencode) {
		if (pass == 2) {
			if (out) fprintf(out, "%04o %04o\n", clc, code);
			else MP[clc] = (WORD)code;
		}
		++clc;
	}
}

/*
	Macro Assembler
*/
void macro_asm(FILE *inp, FILE *out, FILE *err)
{
	// int nline = 0;

	clc = 0200;	/* Default origin */
	asm_clear_pages();
	//printf("Pass %d\n", pass);

	while(fgets(Line, sizeof(Line), inp) != NULL) {
		// ++nline;
		lex_init();

		/* Skip empty and comment lines */
		if (!lex_next()) continue;

		/* Process all logical lines in a physical line */
		do {
			asm_line(out,err);
			if (Token == ';') lex_next();
		} while (Token);
	}

	if (pass == 2) {
		asm_emit_lits(out,curpage);
		if (curpage->page != 0)
			asm_emit_lits(out,&page0);
	}
}

void inline_asm(WORD addr)
{
	asm_clear_pages();

	printf("\n");

	/* Stop at the end of memory or when the user types an empty line */
	while (addr < MAXMEM) {
		printf("%04o: %04o    ", addr, MP[addr]);
		if (fgets(Line, sizeof(Line), stdin) == NULL) 
			break;

		lex_init();

		/* Stop on empty and comment lines */
		if (!lex_next()) break;

		/*
		   We require that any symbols used in an expression be already
		   defined, so we only need to run pass 2 of the assembler.
		*/
		clc = addr;
		asm_set_page(0);
		pass = 2;
		asm_line(0,stderr);

		//printf("%04o: %04o\n", addr, MP[addr]);

		addr = clc;
	}
}

int load_asm(FILE *inp, UNUSED FILE *out, UNUSED FILE *err)
{
	pass = 1;
//	macro_asm(inp,stdout,stderr);
	macro_asm(inp,0,stderr);
	rewind(inp);
	pass = 2;
//	macro_asm(inp,stdout,stderr);
	macro_asm(inp,0,stderr);

	return 0;
}

/*
	Paper tape
	----------
	  In a paper tape a 12-bit word is punched as two 8-bit bytes.

	    word<abcdefghijkl> -> byte<00abcdef> byte<00ghijkl>

	  Depending on the logical format, any of the first two bits
	  may be set 1 to mark a specific boundary or function.

	  Bytes equal to 0x80 are used to fill leaders and trailers
	  and are ignored when read.

	  Byte 0x9A (0x80 | 0x1A = Ctrl-Z) is used to indicate EOF.

	  Byte 0xFF (all holes punched) is a rubout character and
	  should be ignored when read.

	RIM
	---
	  Is a sequence of pairs <addr> <value> ... Each pair corresponds
	  to a memory location.

	  In paper tapes and files this results in sequences of 4 bytes.
	  The first byte of the sequence is or-ed with 0x40 to indicate
	  a new memory location.

	BIN
	---
	  Is a sequence of blocks <addr> <value> <value> ... The first
	  byte of <addr> is or-ed with 0x40 to indicate a change in
	  location. The values in a block occupy consecute memory
	  locations.

	See "PDP-8 Family Paper Tape System User's Guide"
	File binldr_wu.pdf
*/
int load_bin(FILE *inp, FILE *out, UNUSED FILE *err)
{
	int byte1, byte2;
	int addr;
	int code;
	int first = MAXMEM;
	int last = 0;
	int nlocs;

	do {
		byte1 = fgetc(inp);
	} while (byte1 == 0x80);

	addr = 0;
	nlocs = 0;

	while (byte1 != 0x80 && byte1 != 0x9a && byte1 != EOF) {
		byte2 = fgetc(inp);
		if (byte1 & 0x40) {	/* New address */
			addr = ((byte1 & 0x3F) << 6) | byte2;
			if (addr < first) first = addr;
			// printf("*%05o\n", addr);
		} else {			/* New data */
			code = (byte1 << 6) | byte2;
			if (addr > last) last = addr;
			// printf("%05o  %04o\n", addr, code);
			MP[addr] = code;
			++addr;
			++nlocs;
		}
		byte1 = fgetc(inp);
	}

	while (byte1 == 0x80) byte1 = fgetc(inp);

	fprintf(out,"Read %d locations\n", nlocs);
	fprintf(out, "Addresses: %04o-%04o\n", first, last);

	return 0;
}

int load_rim(FILE *inp, UNUSED FILE *out, UNUSED FILE *err)
{
	int byte1, byte2;
	int addr;
	int code;

	do {
		byte1 = fgetc(inp);
	} while (byte1 == 0x80);

	while (byte1 != 0x80 && byte1 != 0x9a && byte1 != EOF) {
		byte2 = fgetc(inp);
		addr = ((byte1 & 0x3F) << 6) | byte2;

		byte1 = fgetc(inp);
		byte2 = fgetc(inp);
		code = (byte1 << 6) | byte2;

		printf("%04o  %04o\n", addr, code);
		MP[addr] = code;
		byte1 = fgetc(inp);
	}

	while (byte1 == 0x80) byte1 = fgetc(inp);

	return 0;
}

int load_txt(FILE *inp, FILE *out, FILE *err)
{
	char line[128];
	int addr, data;
	int first = MAXMEM;
	int last = 0;
	int nlines = 0;

	/* Text file format:  <addr>  <instr>  [<comment>] */
	while(fgets(line, sizeof(line), inp) != NULL) {
		++nlines;
		int len = strlen(line);
		if (len && line[len - 1] == '\n') { --len; line[len] = 0; }
		/* Ignore empty and comment lines */
		if (!*line || *line == '/') continue;
		/* Read only two octal numbers; ignore the rest of the line */
		if (sscanf(line, "%o %o", &addr, &data) != 2) {
			fprintf(err,"Error at line %d: '%s'\n",nlines,line);
			return 0;
		}
		if (addr >= MAXMEM) {
			fprintf(err,"Error at line %d: '%s'\n",nlines,line);
			fprintf(err,"Address field too big: %o\n", addr);
			return 0;
		}
		if (data >= MAXMEM) {
			fprintf(err,"Error at line %d: '%s'\n",nlines,line);
			fprintf(err,"Data field too big: %o\n", data);
			return 0;
		}
		if (addr < first) first = addr;
		if (addr > last) last = addr;
		MP[addr] = data;
	}

	fprintf(out, "Read %d locations\n", nlines);
	fprintf(out, "Addresses: %04o-%04o\n", first, last);

	return 0;
}

static int symb_hash(int len, char *pnt)
{
	int hash = 0;

	while (len--)
		hash += *pnt++;

	return hash & HASH_MASK;
}

static void symb_insert(int len, char *name, WORD value, int type)
{
	int hash = symb_hash(len, name);
	SYMBOL *psym, **plast;

	assert(len <= SYMLEN);

	plast = &hash_table[hash];

	while (*plast) {
		psym = *plast;
		/* Symbol already in table? */
		if (psym->len == len && !strncmp(psym->name, name, len)) {
			if (psym->type == type)
				psym->value = value;	/* Update symbol value */
			return;
		}
		plast = &psym->next;
	}

	psym = (SYMBOL *)malloc(sizeof(SYMBOL));
	psym->next = 0;
	psym->value = value;
	psym->type = type;
	psym->len = len;
	memcpy(psym->name, name, len);
	*plast = psym;
}

static SYMBOL *symb_get(int len, char *name)
{
	int hash = symb_hash(len, name);
	SYMBOL *psym;

	/* Only initialize the symbol table if needed */
	if (!symbol_init) symb_init();

	psym = hash_table[hash];

	while (psym) {
		/* Found symbol? */
		if (psym->len == len && !strncmp(psym->name, name, len))
			return psym;
		psym = psym->next;
	}

	/* Symbol not found */
	return 0;
}

#if		0
/* Display all currently defined symbols of a given type */
#define	SYMCOLS		8
static void symb_display(int type)
{
	int n = 0;

	switch (type) {
	case SYMB_OPCODE:
		printf("\nOpCodes\n");
		break;
	case SYMB_PSEUDO:
		printf("\nDirectives\n");
		break;
	case SYMB_USER:
		printf("\nUser symbols\n");
		break;
	case SYMB_MACRO:
		printf("\nMacro names\n");
		break;
	}

	for (int i = 0; i < HASH_SIZE; ++i) {
		SYMBOL *psym = hash_table[i];
		while (psym) {
			if (psym->type == type) {
				printf("%*.*s: %04o  ",psym->len,psym->len,psym->name,psym->value);
				if (++n == SYMCOLS) {
					printf("\n");
					n = 0;
				}
			}
			psym = psym->next;
		}
	}

	printf("\n\n");
}
#endif

static void symb_add_group(const INSTR *pi)
{
	while (pi->opcode || pi->name) {
		if (pi->name)
			symb_insert(strlen(pi->name),pi->name,pi->opcode,SYMB_OPCODE);
		++pi;
	}
}

static void symb_init(void)
{
	/* Memory reference instructions */
	symb_add_group(main_opcodes);

	/* Microprogrammed instructions (operate) */
	symb_add_group(group1_opr);
	symb_add_group(group2_opr);
	symb_add_group(eae_opr);

	/* Special case: extended memory */
	symb_add_group(emem_iot);

	/* I/O instructions */
	for (int dev = 00; dev <= 077; ++dev)
		if (device_opcodes[dev])
			symb_add_group(device_opcodes[dev]);

	/* Pseudo-instructions */
	symb_insert(6,"CONTINUE",PSEUDO_CONTINUE,SYMB_PSEUDO);
	symb_insert(6,"DECIMAL",PSEUDO_DECIMAL,SYMB_PSEUDO);
	symb_insert(6,"DEFINE",PSEUDO_DEFINE,SYMB_PSEUDO);
	symb_insert(4,"DUBL",PSEUDO_DUBL,SYMB_PSEUDO);
	symb_insert(6,"EXPUNGE",PSEUDO_EXPUNGE,SYMB_PSEUDO);
	symb_insert(5,"FIELD",PSEUDO_FIELD,SYMB_PSEUDO);
	symb_insert(6,"FIXTAB",PSEUDO_FIXTAB,SYMB_PSEUDO);
	symb_insert(4,"FLTG",PSEUDO_FLTG,SYMB_PSEUDO);
	symb_insert(5,"OCTAL",PSEUDO_OCTAL,SYMB_PSEUDO);
	symb_insert(4,"PAGE",PSEUDO_PAGE,SYMB_PSEUDO);
	symb_insert(5,"PAUSE",PSEUDO_PAUSE,SYMB_PSEUDO);
	symb_insert(4,"TEXT",PSEUDO_TEXT,SYMB_PSEUDO);

	symbol_init = 1;
}

/*
	Disassemble 1 instruction
		Input:  addr, inst
		Output: label, name, args, ascii
*/
void cpu_disasm(DINSTR *pd)
{
	const INSTR *pi;
	WORD addr;
	WORD inst = pd->inst;
	int opcode = pd->inst >> 9;

	pd->name[0] = 0;
	pd->ascii[4] = 0;

	// Label (address)
	sprintf(pd->label,"%04o",pd->addr);	/* In the future this could be a symbol */
	// ASCII characters
	if ((inst < 0400) && (inst & 0200)) {
		/* Possibly an ASCII constant */
		int code = inst - 0200; /* Remove mark bit */
		pd->ascii[0] = '\'';
		if (code == 127) { pd->ascii[1] = 'R'; pd->ascii[2] = 'O'; }	/* Rubout */
		else if (code < 32) { /* Control */
			pd->ascii[1] = '\\';
			pd->ascii[3] = '\'';
			switch (code) {
			case '\t': pd->ascii[2] = 't'; break;
			case '\f': pd->ascii[2] = 'f'; break;
			case '\n': pd->ascii[2] = 'n'; break;
			case '\r': pd->ascii[2] = 'r'; break;
			default:   pd->ascii[1] = '^'; pd->ascii[2] = code + 64; break;
			}
		} else { pd->ascii[1] = code; pd->ascii[2] = '\''; pd->ascii[3] = ' '; } /* Printable ASCII */
	} else {
		pd->ascii[0] = '"'; pd->ascii[3] = '"';
		int byte1 = (inst >> 6) & 077;
		int byte2 = inst & 077;
		if (byte1 <= 032) pd->ascii[1] = byte1 + '@';
		else if (byte1 <= 037) pd->ascii[1] = byte1 + '[';
		else pd->ascii[1] = byte1;

		if (byte2 <= 032) pd->ascii[2] = byte2 + '@';
		else if (byte2 <= 037) pd->ascii[2] = byte2 + '[';
		else pd->ascii[2] = byte2;
	}
	// Instruction name and arguments
	if (opcode < 6) {
		strcpy(pd->name,main_opcodes[opcode].name);
		if (inst & PAGE_BIT)	/* Current page */
			addr = (pd->addr & PAGE_MASK) | (inst & OFF_MASK);
		else					/* Page 0 */
			addr = inst & OFF_MASK;
		sprintf(pd->args,"%s%04o", (inst & INDIR_BIT ? "I " : ""), addr);
	} else if (opcode == 6) {
		int dev = (inst >> 3) & 077;
		int fun = inst & 07;
		pd->args[0] = 0;
		pi = device_opcodes[dev];
		/* Handle extended memory first */
		if ((inst & 07700) == 06200) {
			switch (fun) {
			case 1:	/* CDF N0 = 62N1 */
			case 2:	/* CIF N0 = 62N2 */
			case 3:	/* CDI N0 = 62N3 */
				strcpy(pd->name,emem_iot[fun-1].name);
				sprintf(pd->args,"%02o", inst & 070);
				break;
			case 4:	/* RDF, RIF, RIB, RMF */
				strcpy(pd->name,emem_iot[((inst >> 3) & 7) + 2].name);
				break;
			}
		} else if (pi && pi[fun].name) {
			strcpy(pd->name, pi[fun].name);
		}

		if (!pd->name[0]) {	/* Generic IOT */	
			strcpy(pd->name,"IOT");
			sprintf(pd->args,"D=%02o F=%o", dev, fun);
		}
	} else {
		pd->name[0] = 0;
		pd->args[0] = 0;
		if (!(inst & GROUP_BIT)) {	/* Group 1 */
			if (NOP(inst)) strcat(pd->name,"NOP ");
			if (CLA(inst)) strcat(pd->name,"CLA ");
			if (CLL(inst)) strcat(pd->name,"CLL ");
			if (CMA(inst)) strcat(pd->name,"CMA ");
			if (CML(inst)) strcat(pd->name,"CML ");
			if (IAC(inst)) strcat(pd->name,"IAC ");
			if (RT(inst)) {
				if (RAR(inst)) strcat(pd->name,"RTR ");
				if (RAL(inst)) strcat(pd->name,"RTL ");
				if (!RAR(inst) && !RAL(inst))
					strcat(pd->name,"BSW");
			} else {
				if (RAR(inst)) strcat(pd->name,"RAR ");
				if (RAL(inst)) strcat(pd->name,"RAL ");
			}
		} else if (!(inst & 1)) {	/* Group 2 */
			if (!RSS(inst)) {	/* Normal skip sense */
				if (SMA(inst)) strcat(pd->name,"SMA ");
				if (SZA(inst)) strcat(pd->name,"SZA ");
				if (SNL(inst)) strcat(pd->name,"SNL ");
			} else {		/* Reverse skip sense */
				if (!SKP(inst)) strcat(pd->name,"SKP ");
				if (SPA(inst)) strcat(pd->name,"SPA ");
				if (SNA(inst)) strcat(pd->name,"SNA ");
				if (SZL(inst)) strcat(pd->name,"SZL ");
			}
			if (CLA(inst)) strcat(pd->name,"CLA ");
			if (OSR(inst)) strcat(pd->name,"OSR ");
			if (HLT(inst)) strcat(pd->name,"HLT ");
		} else {					/* Group 3 */
			/* Sequence 1 */
			if (CLA(inst)) strcat(pd->name,"CLA ");
			/* Sequence 2 */
			switch ((inst >> 4) & 07) { /* MQA | SCA | MQL */
			case 1:	/* MQL = 7421 */
				strcat(pd->name,"MQL ");
				break;
			case 2:	/* SCA = 7441 */
				strcat(pd->name,"SCA ");
				break;
			case 3:	/* NOP = 7461 */
				break;
			case 4:	/* MQA = 7501 */
				strcat(pd->name,"MQA ");
				break;
			case 5:	/* SWP = 7521 */
				strcat(pd->name,"SWP ");
				break;
			}
			/* Sequence 3 */
			switch ((inst >> 1) & 07) {
			case 0:	/* NOP = 7401 */
				strcat(pd->name,"NOP");
				break;
			case 1:		/* SCL = 7403 */
				strcat(pd->name,"SCL");
				break;
			case 2:		/* MUY = 7405 */
				strcat(pd->name,"MUY");
				break;
			case 3:		/* DVI = 7407 */
				strcat(pd->name,"DVI");
				break;
			case 4:		/* NMI = 7411 */
				strcat(pd->name,"NMI");
				break;
			case 5:		/* SHL = 7413 */
				strcat(pd->name,"SHL");
				break;
			case 6:		/* ASR = 7415 */
				strcat(pd->name,"ASR");
				break;
			case 7:		/* LSR = 7417 */
				strcat(pd->name,"LSR");
				break;
			}
		}
	}
}

