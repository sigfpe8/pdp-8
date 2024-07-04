#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/errno.h>

#include "log.h"
#include "tty.h"

extern void cpu_ireq(int dev, int updown);
extern void cpu_stop(void);

static struct termios orig_termios;
static struct termios asr33_termios;
static char keyb_buffer;
static int keyb_flag;	// 1 if keyb_buffer has a valid char
static int tty_flag;	// 1 if teleprinter is done outputing a character
static int keyb_fd ;	// Keyboard file descriptor
static int keyb_real;	// 1 if real keyboard, 0 if other file

static int tty_keyb_read(int dev);

static void tty_asr33_mode(int mode);

/*
	TTY input mode:
	  0 - Virtual console mode (cooked)

	  1 - Block on read (raw: VMIN=1, VTIME=0)
		When the simulator detects a loop like

			KSF              / Has a key been pressed?
	   		JMP      .-1     / No, go back and wait.

		it calls tty_keyb_wait1(), which puts the terminal in
		mode 1 and waits for a single character. This way the
		loop is not really executed by the simulator because
		the idle time is managed by the OS. To the simulator
		it appears that a character was available the first
		time KSF was executed.

	  2 - Don't block on read (raw: VMIN=0, VTIME=0)
	  	Mode 1 is more efficient but when it cannot be detected
		the simulator can fall back to these basic calls, which
		use mode 2 (no wait):

	  3 - Read 0 or 1 character, timeout = 0.5 sec (raw: VMIN=0, VTIME=5)

		tty_keyb_getflag() (returns 1 if a character is available)
		tty_keyb_inp1() (returns whatever is in the keyb buffer)
*/
static int	asr33_mode;

void tty_init(void)
{
	// Save current settings
	if (tcgetattr(0, &orig_termios) == -1)	// stdin
		log_error(errno, "tcgetattr");

	// Prepare raw mode to emulate ASR33
	asr33_termios = orig_termios;
	asr33_termios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	asr33_termios.c_oflag &= ~(OPOST);
	asr33_termios.c_cflag |= (CS8);
	asr33_termios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	asr33_termios.c_cc[VTIME] = 0;

	keyb_buffer = 0;
	keyb_flag = 0;
	keyb_fd = 0;	// stdin
	keyb_real = 1;	// TODO: check if istty(0)
}

static void tty_asr33_mode(int mode)
{
	switch (mode) {
	case 1:	// Read 1 char (blocking, no timeout). VMIN=1, VTIME=0
		asr33_termios.c_cc[VMIN] = 1;
		asr33_termios.c_cc[VTIME] = 0;
		break;
	case 2:	// Read 0 or 1 char (non-blocking). VMIN=0, VTIME=0
		asr33_termios.c_cc[VMIN] = 0;
		asr33_termios.c_cc[VTIME] = 0;
		break;
	case 3:	// Read 0 or 1 char (blocking, timeout = 0.5 sec), VMIN=0, VTIME=5
		asr33_termios.c_cc[VMIN] = 0;
		asr33_termios.c_cc[VTIME] = 5;
		break;
	}
	if (tcsetattr(0, TCSAFLUSH, &asr33_termios) == -1)
		log_error(errno, "tcsetattr");

	asr33_mode = mode;
}

void tty_exit(void)
{
	if (keyb_real && asr33_mode) {
		if (tcsetattr(0, TCSAFLUSH, &orig_termios) == -1)
			log_error(errno, "tcsetattr");

		asr33_mode = 0;
	}
}

void tty_out1(int dev, int chr)
{
	char buf;
/* 
   For some reason I still don't understand, in FOCAL,1969 when you
      TYPE #
   with the intention of sending just a CR to the console, FOCAL 
   acctually sends CR+FF (0x0D+0x0C or "\r\f"). For example, the
   code
      TYPE "ABC",#,"XYZ",!
   should end up displaying only "XYZ" because after the CR this string
   would overwrite the "ABC". However, what we see is

	  ABC
	  XYZ

   This happens because terminal emulators nowadays don't always honor
   '\f' (preferring an ANSI sequence instead to clear the screen) and
   interpret it as a linefeed.

   Interestingly, testing with FOCAL, 8/68 we see the expected result:

      XYZ

   Defining the constant FOCAL_CR_HACK below will provide a translation
   from '\f' to '\r' so that CR+FF becomes CR+CR. This way FOCAL,1969
   also works as expected. Maybe this could be a runtime flag, but for
   now I'll leave it as a compile time option.

   As a side note, it is curious to observe that MUMPS, a language that
   was influenced by FOCAL, chose the character '#' to denote a form-feed
   exactly like the FOCAL,1969 behavior (although the FOCAL manual says
   it should be a CR).
*/
#define	FOCAL_CR_HACK
#ifdef	FOCAL_CR_HACK
	buf = chr == 0x0c ? 0x0d : chr;
#else
	buf = chr;
#endif
	write(1, &buf, 1);
	tty_flag = 1;
	cpu_ireq(dev, 1);	// Request interrupt
}

// Set teleprinter flag to 0/1
// Return old flag
int tty_out_set_flag(int dev, int flag)
{
	int old_flag = tty_flag;

	tty_flag = flag;
	cpu_ireq(dev, flag);	// Raise/clear int request
	return old_flag;
}

// Get teleprinter flag
int tty_out_get_flag(UNUSED int dev)
{
	return tty_flag;
}

// Assign the keyboard to a file
void tty_keyb_assign(char* fname)
{
	int fd;
	
	if ((fd = open(fname, O_RDONLY)) < 0) {
		printf("Could not open %s to redirect keyboard input\n", fname);
		return;
	}

	keyb_fd = fd;
	keyb_real = 0;
	tty_keyb_read(3);	// First character
}

// Wait for 1 character to be pressed
int tty_keyb_wait1(int dev)
{
	if (keyb_flag) return 1;
	if (keyb_real && asr33_mode != 1) tty_asr33_mode(1);
	return tty_keyb_read(dev);
}

// Get keyboard flag (no-wait)
int tty_keyb_get_flag(int dev)
{
	if (keyb_flag) return 1;
	if (keyb_real && asr33_mode != 2) tty_asr33_mode(2);
	return tty_keyb_read(dev);
}

// Wait for 1 character to be pressed, 0.5s timeout
int tty_keyb_timed_wait1(int dev)
{
	if (keyb_flag) return 1;
	if (keyb_real && asr33_mode != 3) tty_asr33_mode(3);
	return tty_keyb_read(dev);
}

// Set keyboard flag to 0/1
// Return old flag
int tty_keyb_set_flag(int dev, int flag)
{
	int old_flag = keyb_flag;

	keyb_flag = flag;
	cpu_ireq(dev, flag);
	return old_flag;
}

// Read 1 character and clear flag (no-wait)
int tty_keyb_inp1(int dev)
{
	if (keyb_flag) {
		keyb_flag = 0;
		cpu_ireq(dev, 0);	// Clear interrupt request
		return keyb_buffer | 0200;
	}
	if (keyb_real && asr33_mode != 2) tty_asr33_mode(2);
	return tty_keyb_read(dev);
}

// Low level read 1 character
static int tty_keyb_read(int dev)
{
	int rc;

	if ((rc = read(keyb_fd, &keyb_buffer, 1)) == 1) {
		keyb_flag = 1;
		if (keyb_buffer == CTRL_C)
			cpu_stop();
		if (keyb_buffer == 10)
			keyb_buffer = 13;	// \n --> \r
	} else if (rc == 0) {		// EOF
		keyb_flag = 0;
		if (keyb_fd) {			// If not stdin, close and reassign to 0
			close(keyb_fd);
			keyb_fd = 0;
			keyb_real = 1;
		}
	} else {					// Possibly error
		keyb_flag = 0;
		if (errno != EAGAIN)	// EWOULDBLOCK?
			log_error(errno, "read");
	}

	cpu_ireq(dev, keyb_flag);
	return keyb_flag;
}
