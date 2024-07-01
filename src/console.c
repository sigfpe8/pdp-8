#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "pdp8.h"
#include "console.h"
#include "tty.h"

extern void inline_asm(WORD addr);

/* Virtual Console */
#define MAXARGS	10

static int  assign(int argc, char *argv[]);
static int  bp_check(WORD addr);
static int  bp_clear(int argc, char *argv[]);
static int  bp_list(int argc, char *argv[]);
static int  bp_set(int argc, char *argv[]);
static void con_trace_next(WORD addr, WORD code);
static int  cont(int argc, char *argv[]);
static int  deposit(int argc, char *argv[]);
static int  examine(int argc, char *argv[]);
static int  help(int argc, char *argv[]);
static int  load(int argc, char *argv[]);
static int  make_argv(char *line, char **argv);
static int  octal_args(int argc, char *argv[], WORD args[], int minargs, int maxargs);
//static void print_argv(int argc, char *argv[]);
static int  quit(int argc, char *argv[]);
static int  run(int argc, char *argv[]);
static int  set_acc(int argc, char *argv[]);
static int  set_link(int argc, char *argv[]);
static int  set_log(int argc, char *argv[]);
static void set_signals(void);
static int  set_swt(int argc, char *argv[]);
static int  set_trace(int argc, char *argv[]);
static int  show_regs(int argc, char *argv[]);
static int  single_step(int argc, char *argv[]);
static void sig_handler(int sig);

typedef struct {
	char *name;						/* Command name	*/
	char *args;						/* Arguments and flags */
	char *help;						/* Help text */
	int (*handler)(int,char **);	/* Command handler */
} Command;

Command cmdtable[] = {
	{ "assign",	"<dev> <file>",			"Assign file to device",assign		},
	{ "bc",		"<bp #>",				"Clear breakpoint",		bp_clear	},
	{ "bl",		"",						"List breakpoints",		bp_list		},
	{ "bp",		"<addr>",				"Set breakpoint",		bp_set		},
	{ "continue","",					"Continue",				cont		},
	{ "deposit","<addr>",				"Deposit memory",		deposit		},
	{ "examine","<addr> [<count>]",		"Examine memory", 		examine,	},
	{ "help",	"",						"Display help",			help,		},
	{ "load",	"<file>",				"Load file",			load,		},
	{ "log",    "0|1",                  "Start/stop logging",	set_log,	},
	{ "quit",	"",						"Quit simulator",		quit,		},
	{ "run",	"<addr>",				"Run program",			run,		},
	{ "sacc",	"<value>",				"Set ACC=value",		set_acc,	},
	{ "shregs",	"",						"Show registers",		show_regs,	},
	{ "si",		"",						"Single step",			single_step	},
	{ "slink",	"0|1",					"Set L=0|1",			set_link,	},
	{ "sswt",	"<value>",				"Set SR=value",			set_swt,	},
	{ "trace",	"0|1 [<file>]",			"Start/stop tracing",	set_trace,	},
	{ "?",		"",						"Display help",			help,		},
	{	0,		0,						0,						0,			}
};

static Command *find_command(char *name);

static FILE *tracef;	/* Trace file pointer */
static size_t traceb;	/* # of bytes used by trace file */

#define	MAXBREAKPOINTS	10

typedef struct {
	WORD	addr;		// Address of breakpoint
	WORD	inst;		// Original instruction
} BreakPoint;

BreakPoint bptable[MAXBREAKPOINTS];
int nBreakPoints;

void console(void)
{
	char line[128];
	char *argv[MAXARGS+2]; /* Leave room for command + NULL */
	int argc;
	Command	*pcm;

	set_signals();
	tty_init();

	printf("\nVirtual console\n");

	while (1) {
		printf("\nPC=%05o> ",PC);
		if (fgets(line, sizeof(line), stdin) == NULL) {
			if (STOP) {	/* Ignore CTRL-C in the main loop */
				STOP = 0;
				continue;
			}
			return;	/* EOF */
		}

		if ((argc = make_argv(line, argv)) < 1)
			continue;

		//print_argv(argc, argv);

		pcm = find_command(argv[0]);

		if (pcm == (Command *)-1) {
			printf("Ambiguous command\n");
			continue;
		}

		if (!pcm) {
			printf("Unknown command\n");
			continue;
		}

		if ((*pcm->handler)(argc, argv))
			break;		/* Quit command */
	}
}

/* Show last instruction that was executed + current state */
void con_trace(WORD addr, WORD code)
{
	DINSTR inst;

	inst.addr = addr;
	inst.inst = code;
	cpu_disasm(&inst);

#if	0
	printf("PC=%04o [%04o] %s%s%s\t L=%d  AC=%04o  MQ=%04o\r\n",
		lastPC, IR, inst.name, (strlen(inst.name) > 8 ? "" : "\t"), inst.args, L, AC, MQ);
#else
	traceb += fprintf(tracef, "PC=%05o [%04o] ", addr, code);
	if (inst.args[0])
		traceb += fprintf(tracef, "%-8s %-8s", inst.name, inst.args);
	else
		traceb += fprintf(tracef, "%-16s ", inst.name);
	
	traceb += fprintf(tracef, "L=%d  AC=%04o  IF=%d  DF=%d  IB=%d  MA=%05o IEN=%d  IREQ=%08llx\r\n",
		L, AC,
		IF>>12, DF>>12, IB>>12,
		MA, IEN, IREQ);
	if (traceb / (1024*1024) > 10) {
		if (tracef != stdout) fclose(tracef);
		tracef = stdout;
		traceb = 0;
	}
#endif
}

/* Show *next* instruction to be executed + current state */
static void con_trace_next(WORD addr, WORD code)
{
	DINSTR inst;

	inst.addr = addr;
	inst.inst = code;
	cpu_disasm(&inst);

	printf("\r\nPC=%05o [%04o] %s\t%s\n", addr, code, inst.name, inst.args);
	printf("         L=%d  AC=%04o  MQ=%04o  IF=%d  DF=%d  IB=%d  IEN=%d  IREQ=%08llx\r\n",
		L, AC, MQ, IF>>12, DF>>12, IB>>12, IEN, IREQ);
}

void con_stop(void)
{
	if (STOP)
		printf("\n\nINTERRUPT @ %05o  L=%d  AC=%04o\n",PC-1,L,AC);
}

static Command *find_command(char *name)
{
	char *ptab, *pname;
	Command *pcm, *pcmFound;
	int nMatches;

	nMatches = 0;
	pcmFound = NULL;

	for (pcm = cmdtable; pcm->name; ++pcm) {
		pname = name;
		ptab  = pcm->name;

		while (tolower(*pname) == *ptab++)
			if (*pname == '\0')
				return pcm;		/* Exact match */
			else
				++pname;

		if (*pname == '\0')	{ /* Name was a prefix */
			++nMatches;
			pcmFound = pcm;
		}
	}

	if (nMatches > 1)
		return (Command *)-1;

	return pcmFound;
}

/*
   Parse a command line into an array of arguments.

   This function patches the source line with 0's at
   the end of each argument. If more sofisticated parsing
   is required it may be necessary to copy each argument
   to a separate buffer.
*/
static int make_argv(char *line, char **argv)
{
	char *pch = line;
	char **ppch = argv;
	int argc = 0;

	while (*pch) {
		if (argc > MAXARGS) {
			printf("Too many arguments: max = %d\n", MAXARGS);
			return 0;
		}

		while (isspace(*pch)) ++pch;/* Skip leading blanks */

		if (!*pch)					/* Got to the end? */
			break;

		if (*pch == '"') {			/* Quoted argument */
			++pch;
			*ppch++ = pch;
			while (*pch != '"' &&  *pch) ++pch;
			if (!*pch) {
				printf("Missing end quote in argument\n");
				return 0;
			} else
				*pch++ = 0;
		} else {					/* Ordinary argument */
			*ppch++ = pch;
			while (!isspace(*pch) && *pch) ++pch;
			if (*pch) *pch++ = 0;
		}

		++argc;
	}

	*ppch = 0;
	return argc;
}

#if	0
static void print_argv(int argc, char *argv[])
{
	printf("argc=%d\n", argc);

	for (int i = 0; i < argc; ++i) {
		printf("argv[%d]=%s\n", i, argv[i]);
	}
}
#endif

/*
   Convert octal arguments from string
   Validate number of arguments
*/
static int octal_args(int argc, char *argv[], WORD args[], int minargs, int maxargs)
{
	int nargs;

	nargs = argc - 1;	/* Real # of args */
	args[0] = nargs;

	if (minargs == maxargs && minargs != nargs) {
		Command *pcm = find_command(argv[0]);
		if (!minargs)
			printf("'%s' takes no arguments\n", pcm->name);
		else
			printf("'%s' takes %d argument%s: %s\n",
				pcm->name, minargs, minargs == 1 ? "" : "s", pcm->args);
		return -1;
	}
	if (nargs < minargs) {
		Command *pcm = find_command(argv[0]);
		printf("'%s' takes at least %d argument%s: %s\n",
			pcm->name, minargs, minargs == 1 ? "" : "s", pcm->args);
		return -1;
	}
	if (nargs > maxargs) {
		Command *pcm = find_command(argv[0]);
		printf("'%s' takes at most %d argument%s: %s",
			pcm->name, maxargs, maxargs == 1 ? "" : "s", pcm->args);
		return -1;
	}

	for (int i = 1; i <= nargs; ++i) {
		char *pch;
		char *end;
		pch = argv[i];
		WORD arg = (WORD)strtol(pch, &end, 8);
		if (end == pch) {
			printf("Argument must be an octal number: %s\n", pch);
			return -1;
		}
		if (arg >= memwords) {
			printf("Octal number is too big: 0%o (must be <= 0%lo)\n", arg, (memwords-1));
			return -1;
		}
		args[i] = arg;
	}

	return nargs;
}

static void set_signals(void)
{
	struct sigaction sa;

	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = sig_handler;

	if (sigaction(SIGINT, &sa, NULL) == -1)
		perror("sigaction");
}

static void sig_handler(int sig)
{
	if (sig == SIGINT) STOP = 1;
}

// bc <bp #>
static int bp_clear(int argc, char *argv[])
{
	WORD args[MAXARGS+1];
	int bn;
	BreakPoint *bp;

	if (octal_args(argc, argv, args, 1, 1) < 0)
		return 0;

	bn = args[1];

	if (bn < 1 || bn > MAXBREAKPOINTS) {
		printf("Valid breakpoint numbers are between 1 and %o\n", MAXBREAKPOINTS);
		return 0;
	}
	bp = &bptable[bn-1];

	if (!bp->addr) {
		printf("Breakpoint %o does not exist\n", bn);
		return 0;
	}

	// Restore original instruction
	MP[bp->addr] = bp->inst;
	// Mark slot as free
	bp->addr = 0;
	--nBreakPoints;
	if (bn == BP_NUM)
		BP_NUM = 0;

	printf("Breakpoint %o cleared\n", bn);
	return 0;
}

// bl
static int bp_list(UNUSED int argc, UNUSED char *argv[])
{
	int bn;
	BreakPoint *bp;

	if (!nBreakPoints) {
		printf("There are no breakpoints\n");
		return 0;
	}

	printf("\n");
	printf(" #   Addr  Inst\n");
	printf("--  -----  ----\n");
	for (bn = 1, bp = bptable; bn <= MAXBREAKPOINTS; ++bn, ++bp) {
		if (bp->addr)
			printf("%2o  %05o  %04o\n", bn, bp->addr, bp->inst);
	}

	return 0;
}

// bp <addr>
static int bp_set(int argc, char *argv[])
{
	WORD args[MAXARGS+1];
	int bn;
	BreakPoint *bp;

	if (octal_args(argc, argv, args, 1, 1) < 0)
		return 0;

	if (nBreakPoints == MAXBREAKPOINTS) {
		printf("Maximum of %d breakpoints allowed\n", MAXBREAKPOINTS);
		return 0;
	}

	WORD addr = args[1];

	if (!addr) {
		printf("Cannot set breakpoint at address 0\n");
		return 0;
	}

	// Make sure a breakpoint does not already exist at this address
	if (bp_check(addr)) {
		printf("Breakpoint already exists at %05o\n", addr);
		return 0;
	}

	// Find free breakpoint slot
	for (bn = 1, bp = bptable; bn <= MAXBREAKPOINTS; ++bn, ++bp) {
		// Zero addr means slot is free
		if (!bp->addr)
			break;	// found
	}

	assert(bn <= MAXBREAKPOINTS);

	bp->addr = addr;
	bp->inst = MP[addr];
	MP[addr] = HALT;
	++nBreakPoints;

	printf("Breakpoint %o set at %05o\n", bn, addr);
	return 0;
}

// If there's a breakpoint at addr, return its number
// Otherwise return 0
static int bp_check(WORD addr)
{
	int bn;
	int nb;
	BreakPoint *bp;

	bn = 1;
	bp = bptable;
	nb = nBreakPoints;	// Only check this many breakpoints

	while (nb && bn <= MAXBREAKPOINTS) {
		if (bp->addr) {	// Valid slot?
			if (bp->addr == addr)
				return bn;	// Found a breakpoint at addr
			--nb;
		}
		++bn;
		++bp;
	}

	return 0;	// No breakpoint found
}

/* continue */
static int cont(UNUSED int argc, UNUSED char *argv[])
{
	cpu_run(PC, 0);

	if (!RUN && (IR == HALT)) {
		if ((BP_NUM = bp_check(PC-1))) {
			--PC;
			MP[PC] = bptable[BP_NUM-1].inst;	// Restore original instruction
			printf("\nBreakpoint %o @ %05o\n", BP_NUM, PC);
			con_trace_next(PC,MP[PC]);
		} else
			printf("\n\nHALT @ %05o  L=%d  AC=%04o\n",PC-1,L,AC);
	}

	tty_exit();

	return 0;
}

/* deposit <addr> */
static int deposit(int argc, char *argv[])
{
	WORD args[MAXARGS+1];

	if (octal_args(argc, argv, args, 1, 1) < 0)
		return 0;

	inline_asm(args[1]);

	return 0;
}

/* examine <addr> [<count> [<output-file>]] */
static int examine(int argc, char *argv[])
{
	WORD addr;
	size_t count;
	DINSTR inst;
	FILE *out;
	int bn;
	WORD args[MAXARGS+1];

	if (argc == 4) {
		out = fopen(argv[3],"w");
		if (!out) {
			fprintf(stderr,"Could not open '%s' for output\n",argv[3]);
			return 0;
		}
		--argc;
	} else out = stdout;

	if (octal_args(argc, argv, args, 1, 2) < 0)
		return 0;

	if (args[0] > 1) count = args[2];
	else count = 1;
	if (args[0] > 0) addr = args[1];
	else addr = 0;

	if (addr + count > memwords) count = memwords - addr;
	if (!count) count = 1;

	fprintf(out,"\n");

	while (count--) {
		inst.addr = addr;
		if ((bn = bp_check(addr)))		// Is there a breakpoint here?
			inst.inst = bptable[bn-1].inst;	// Yes, show original instruction
		else
			inst.inst = MP[addr];			// No, show current instruction
		cpu_disasm(&inst);
		fprintf(out, "%05o:%s%s%04o  %s  %s%s%s\n",
			addr,
			(addr == PC ? ">" : " "),
			(bn ? "*" : " "),
			inst.inst, inst.ascii, inst.name,
			(strlen(inst.name) > 8 ? "" : "\t"),
			inst.args);
		++addr;
	}

	if (out != stdout) fclose(out);

	return 0;
}

static int help(int argc, char *argv[])
{
	Command *pcm;
#define	NAMECOLS	12	/* Columns used by name */
#define	ARGSCOLS	25	/* Columns used by arguments */

	WORD args[MAXARGS+1];
	int i;

	if (octal_args(argc, argv, args, 0, 0) < 0)
		return 0;

	printf("\nPDP-8 virtual console commands:\n\n");

	i = printf("  Command") - 2; 
	while (i++ < NAMECOLS) putchar(' ');
	i = printf("Arguments");
	while (i++ < ARGSCOLS) putchar(' ');
	printf("Purpose\n");

	i = printf("  ----------") - 2; 
	while (i++ < NAMECOLS) putchar(' ');
	i = printf("----------------------");
	while (i++ < ARGSCOLS) putchar(' ');
	printf("----------------------\n");

	for (pcm = cmdtable; pcm->name; ++pcm) {
		printf("  %s", pcm->name);
		i = strlen(pcm->name);
		while (i++ < NAMECOLS) putchar(' ');
		
		printf("%s", pcm->args);
		i = strlen(pcm->args);
		while (i++ < ARGSCOLS) putchar(' ');

		printf("%s\n", pcm->help);
	}

	return 0;
}

#define	FILEFMT_ASM		1	// macro-8 assembler source
#define	FILEFMT_BIN		2
#define	FILEFMT_RIM		3
#define	FILEFMT_TXT		4	// Text version of RIM: <addr> <code>

#define FILELEN_MAX		256	// Max filename length

static int load(int argc, char *argv[])
{
	FILE *inp;
	FILE *out = 0;	// Default: don't disassemble
	int disasm = 0;
	size_t len;
	char *sep;
	char *file_inp;
	char file_out[FILELEN_MAX+5];	// basename(<file_inp>).lst
	int ffmt;

	if (argc == 2)			// load <filename>
		file_inp = argv[1];
	else if (argc == 3) {	// load -d <filename>  // (debug/disassemble)
		if (strcmp(argv[1], "-d")) {
			printf("Invalid option: %s\n", argv[1]);
			return 0;
		}
		disasm = 1;
		file_inp = argv[2];
	} else {
		printf("load [-d] <filename>\n");
		return 0;
	}

	if ((sep = strrchr(file_inp, '.')) && strlen(sep+1) < 5) {
		if (!strcmp(sep+1,"asm8"))
			ffmt = FILEFMT_ASM;
		else if (!strcmp(sep+1,"bin"))
			ffmt = FILEFMT_BIN;
		else if (!strcmp(sep+1,"rim"))
			ffmt = FILEFMT_RIM;
		else if (!strcmp(sep+1,"txt"))
			ffmt = FILEFMT_TXT;
		else {
			printf("Unknown file extension\n");
			return 0;
		}
		if (disasm) {
			if ((len = sep - file_inp) > FILELEN_MAX) {
				printf("Filename is too long\n");
				return 0;
			}
			memcpy(file_out, file_inp, len);
		}
	} else if ((sep = strrchr(file_inp, '-'))) {
		if (!strcmp(sep+1,"pb"))
			ffmt = FILEFMT_BIN;
		else if (!strcmp(sep+1,"pm"))
			ffmt = FILEFMT_RIM;
		else {
			printf("Unknown file type\n");
			return 0;
		}
		if (disasm) {
			if ((len = strlen(file_inp)) > FILELEN_MAX) {
				printf("Filename is too long\n");
				return 0;
			}
			memcpy(file_out, file_inp, len);
		}
	}

	if (!(inp = fopen(file_inp,"r"))) {
		printf("Could not open '%s' for input\n", file_inp);
		return 0;
	}

	if (disasm) {
		strcpy(file_out + len, ".lst");
		if (!(out = fopen(file_out,"w"))) {
			printf("Could not open '%s' for output\n", file_out);
			fclose(inp);
			return 0;
		}
		printf("Disassembling to '%s'\n", file_out);
	}

	switch (ffmt) {
	case FILEFMT_ASM:
		load_asm(inp,out,stderr);
		break;
	case FILEFMT_BIN:
		load_bin(inp,out,stderr);
		break;
	case FILEFMT_RIM:
		load_rim(inp,out,stderr);
		break;
	case FILEFMT_TXT:
		load_txt(inp,out,stderr);
		break;
	}

	if (out && out != stdout) fclose(out);

	fclose(inp);

	return 0;
}

// log 0|1
static int set_log(int argc, char *argv[])
{
	WORD args[MAXARGS+1];
	int log = 0;

	if (octal_args(argc, argv, args, 0, 1) < 0)
		return 0;

	if (argc == 2) {	// log 0|1
		if (args[1]) {
			log_open();
			log = 1;
		}
		else
			log_close();
	}

	printf("Logging is %s\n", log ? "ON" : "OFF");

	return 0;
}

static int quit(int argc, char *argv[])
{
	WORD args[MAXARGS+1];

	if (octal_args(argc, argv, args, 0, 0) < 0)
		return 0;

	return 1;	/* Force quit */
}

/* run <addr> */
static int run(int argc, char *argv[])
{
	WORD args[MAXARGS+1];

	if (octal_args(argc, argv, args, 1, 1) < 0)
		return 0;

	cpu_run(args[1], 0);

	if (!RUN && (IR == HALT)) {
		if ((BP_NUM = bp_check(PC-1))) {
			--PC;
			MP[PC] = bptable[BP_NUM-1].inst;	// Restore original instruction
			printf("\nBreakpoint %o @ %05o\n", BP_NUM, PC);
			con_trace_next(PC,MP[PC]);
		} else
			printf("\n\nHALT @ %05o  L=%d  AC=%04o\n",PC-1,L,AC);
	}

	tty_exit();

	return 0;
}

/* sacc <value> */
static int set_acc(int argc, char *argv[])
{
	WORD args[MAXARGS+1];

	if (octal_args(argc, argv, args, 1, 1) < 0)
		return 0;

	AC = args[1];

	return 0;
}

/* shregs */
static int show_regs(int argc, char *argv[])
{
	WORD args[MAXARGS+1];

	if (octal_args(argc, argv, args, 0, 0) < 0)
		return 0;

	printf("PC=%05o  MA=%04o  L=%d  AC=%04o  MQ=%04o  DF=%d  IF=%d  IB=%d  SR=%04o\n",
			PC, MA, L, AC, MQ, DF>>12, IF>>12, IB>>12, SR);

	return 0;
}

/* slink <value> */
static int set_link(int argc, char *argv[])
{
	WORD args[MAXARGS+1];

	if (octal_args(argc, argv, args, 1, 1) < 0)
		return 0;

	L = args[1] & 1;

	return 0;
}

/* sswt <value> */
static int set_swt(int argc, char *argv[])
{
	WORD args[MAXARGS+1];

	if (octal_args(argc, argv, args, 1, 1) < 0)
		return 0;

	SR = args[1];

	return 0;
}

/* si */
static int single_step(UNUSED int argc, UNUSED char *argv[])
{
	// If the instruction we're about to execute is a breakpoint,
	// replace it with the original instruction
	if ((BP_NUM = bp_check(PC)))
		MP[PC] = bptable[BP_NUM-1].inst;

	cpu_run(PC, 1);

	int bn;
	WORD inst; 

	if ((bn = bp_check(PC)))		// If next instruction is a breakpoint
		inst = bptable[bn-1].inst;	// Show original instruction, not HLT
	else
		inst = MP[PC];

	con_trace_next(PC,inst);

	return 0;
}

/* trace 0|1 [<file>]*/
static int set_trace(int argc, char *argv[])
{
	WORD args[MAXARGS+1];
	int trace_file = 0;

	// Close previous trace file if one was open
	if (tracef && tracef != stdout) fclose(tracef);
	tracef = stdout;

	// New trace file?
	if (argc == 3) {
		trace_file = 1;
		--argc;
	}

	if (octal_args(argc, argv, args, 0, 1) < 0)
		return 0;

	if (argc == 2) {	// trace 0|1
		trace = !!args[1]; // on/off
		if (trace) {
			if (trace_file)
				tracef = fopen(argv[2],"w");
			if (!tracef) {
				printf("Could not open trace file \"%s\"\n",argv[2]);
				printf("Tracing to stdout\n");
				tracef = stdout;
			}
			traceb = 0;
		}
	}

	printf("Tracing is %s\n", trace ? "ON" : "OFF");

	return 0;
}

// assign <dev> <file>
static int assign(int argc, char *argv[])
{
	WORD args[MAXARGS+1];

	if (argc != 3) {
		printf("Invalid number of arguments\n");
		printf("assign <dev> <file>\n");
		return 0;
	}

	char *fname = argv[2];
	--argc;

	if (octal_args(argc, argv, args, 1, 1) < 0)
		return 0;

	int dev = args[1];
	if (dev < 0 || dev > 077) {
		printf("Device numbers must be between 00 and 77 octal\n");
		return 0;
	}

	switch (dev) {
	case 001:	// High speed paper tape reader
		ppt_reader_assign(fname);
		break;
	case 002:	// High speed paper tape punch
		ppt_punch_assign(fname);
		break;
	case 003:	// Console keyboard (TTY) / low speed paper tape reader
		tty_keyb_assign(fname);
		break;
	default:
		printf("Device %02o not supported\n", dev);
		break;
	}

	return 0;
}
