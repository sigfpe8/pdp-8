#include <ctype.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdp8.h"
#include "console.h"
#include "tty.h"

extern void inline_asm(WORD addr);

/* Virtual Console */
#define MAXARGS	10

static int cont(int argc, char *argv[]);
static int deposit(int argc, char *argv[]);
static int examine(int argc, char *argv[]);
static int help(int argc, char *argv[]);
static int load(int argc, char *argv[]);
static int make_argv(char *line, char **argv);
static int octal_args(int argc, char *argv[], WORD args[], int minargs, int maxargs);
//static void print_argv(int argc, char *argv[]);
static int quit(int argc, char *argv[]);
static int run(int argc, char *argv[]);
static int set_acc(int argc, char *argv[]);
static int set_link(int argc, char *argv[]);
static void set_signals(void);
static int set_swt(int argc, char *argv[]);
static int set_trace(int argc, char *argv[]);
static int show_regs(int argc, char *argv[]);
static int single_step(int argc, char *argv[]);
static void sig_handler(int sig);

typedef struct {
	char *name;						/* Command name	*/
	char *args;						/* Arguments and flags */
	char *help;						/* Help text */
	int (*handler)(int,char **);	/* Command handler */
} Command;

Command cmdtable[] = {
	{ "continue","",					"Continue",			cont		},
	{ "deposit","<addr>",				"Deposit memory",	deposit		},
	{ "examine","<addr> [<count>]",		"Examine memory", 	examine,	},
	{ "help",	"",						"Display help",		help,		},
	{ "quit",	"",						"Quit emulator",	quit,		},
	{ "load",	"<file>",				"Load file",		load,		},
	{ "run",	"<addr>",				"Run program",		run,		},
	{ "sacc",	"<value>",				"Set ACC=value",	set_acc,	},
	{ "shregs",	"",						"Show registers",	show_regs,	},
	{ "si",		"",						"Single step",		single_step	},
	{ "slink",	"0|1",					"Set L=0|1",		set_link,	},
	{ "sswt",	"<value>",				"Set SR=value",		set_swt,	},
	{ "trace",	"0|1 [<file>]",			"Set/toggle trace",	set_trace,	},
	{ "?",		"",						"Display help",		help,		},
	{	0,		0,						0,					0,			}
};

static Command *find_command(char *name);

static FILE *tracef;	/* Trace file pointer */
static size_t traceb;	/* # of bytes used by trace file */

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
void con_trace(WORD lastPC)
{
	DINSTR inst;

	inst.addr = lastPC;
	inst.inst = IR;
	cpu_disasm(&inst);

#if	0
	printf("PC=%04o [%04o] %s%s%s\t L=%d  AC=%04o  MQ=%04o\r\n",
		lastPC, IR, inst.name, (strlen(inst.name) > 8 ? "" : "\t"), inst.args, L, AC, MQ);
#else
	traceb += fprintf(tracef, "PC=%05o [%04o] %s%s%s\t L=%d  AC=%04o  IF=%d  DF=%d  IB=%d  IEN=%d  IREQ=%08llx\r\n",
		lastPC, IR, inst.name, (strlen(inst.name) > 8 ? "" : "\t"), inst.args, L, AC,
		IF>>12, DF>>12, IB>>12,
		IEN, IREQ);
	if (traceb / (1024*1024) > 10) {
		if (tracef != stdout) fclose(tracef);
		tracef = stdout;
		traceb = 0;
	}
#endif
}

/* Show *next* instruction to be executed + current state */
void con_trace_next(void)
{
	DINSTR inst;

	inst.addr = PC;
	inst.inst = MP[PC];
	cpu_disasm(&inst);

	printf("PC=%05o [%04o] %s\t%s\n", PC, MP[PC], inst.name, inst.args);
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
		if (arg >= memsize) {
			printf("Octal number is too big: 0%o (must be <= 0%lo)\n", arg, (memsize-1));
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

/* continue */
static int cont(UNUSED int argc, UNUSED char *argv[])
{
	cpu_run(PC, 0);

	if (!RUN && (IR == HALT)) {
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
	WORD code;
	size_t count;
	int byte1, byte2;
	DINSTR inst;
	FILE *out;
	char ascii[5];	/* "XY" or 'A' or '\n' */
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

	if (addr + count > memsize) count = memsize - addr;
	if (!count) count = 1;

	fprintf(out,"\n");

	ascii[4] = 0;

	while (count--) {
		inst.addr = addr;
		inst.inst = MP[addr];
		cpu_disasm(&inst);
		if (((code = MP[addr]) < 0400) && (code & 0200)) {
			/* Possibly an ASCII constant */
			code -= 0200; /* Remove mark bit */
			ascii[0] = '\'';
			if (code == 127) { ascii[1] = 'R'; ascii[2] = 'O'; }	/* Rubout */
			else if (code < 32) { /* Control */
				ascii[1] = '\\';
				ascii[3] = '\'';
				switch (code) {
				case '\t': ascii[2] = 't'; break;
				case '\f': ascii[2] = 'f'; break;
				case '\n': ascii[2] = 'n'; break;
				case '\r': ascii[2] = 'r'; break;
				default:   ascii[1] = '^'; ascii[2] = code + 64; break;
				}
			} else { ascii[1] = code; ascii[2] = '\''; ascii[3] = ' '; } /* Printable ASCII */
		} else {
			ascii[0] = '"'; ascii[3] = '"';
			byte1 = (code >> 6) & 077;
			byte2 = code & 077;
			if (byte1 <= 032) ascii[1] = byte1 + '@';
			else if (byte1 <= 037) ascii[1] = byte1 + '[';
			else ascii[1] = byte1;

			if (byte2 <= 032) ascii[2] = byte2 + '@';
			else if (byte2 <= 037) ascii[2] = byte2 + '[';
			else ascii[2] = byte2;
		}
		fprintf(out, "%05o:%s %04o  %s  %s%s%s\n", addr, (addr == PC ? ">" : " "),
			MP[addr], ascii,
			inst.name, (strlen(inst.name) > 8 ? "" : "\t"), inst.args);
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

#define	FILEFMT_ASM		1	/* macro-8 assembler source */
#define	FILEFMT_BIN		2
#define	FILEFMT_RIM		3
#define	FILEFMT_TXT		4	/* Text version of RIM: <addr> <code> */

static int load(int argc, char *argv[])
{
	FILE *inp;
	char *sep;
	int ffmt;

	if (argc != 2) {
		printf("'load' takes 1 argument: <filename>\n");
		return 0;
	}

	if ((sep = strrchr(argv[1], '.')) && strlen(sep+1) < 5) {
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
	} else if ((sep = strrchr(argv[1], '-'))) {
		if (!strcmp(sep+1,"pb"))
			ffmt = FILEFMT_BIN;
		else if (!strcmp(sep+1,"pm"))
			ffmt = FILEFMT_RIM;
		else {
			printf("Unknown file type\n");
			return 0;
		}
	}

	if (!(inp = fopen(argv[1],"r"))) {
		printf("Could not open '%s'\n", argv[1]);
		return 0;
	}

	switch (ffmt) {
	case FILEFMT_ASM:
		load_asm(inp,stdout,stderr);
		break;
	case FILEFMT_BIN:
		load_bin(inp,stdout,stderr);
		break;
	case FILEFMT_RIM:
		load_rim(inp,stdout,stderr);
		break;
	case FILEFMT_TXT:
		load_txt(inp,stdout,stderr);
		break;
	}

	fclose(inp);

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
	cpu_run(PC, 1);
	con_trace_next();

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

	trace = !!args[1]; /* on/off */
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
	printf("Trace is %s\n", trace ? "ON" : "OFF");

	return 0;
}

