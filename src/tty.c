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
static int keyb_flag;	/* 1 if keyb_buffer has a valid char */
static int tty_flag;	/* 1 if teleprinter is done outputing a character */

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

		tty_keyb_getflag() (returns 1 if a character is available)
		tty_keyb_inp1() (returns whatever is in the keyb buffer)
*/
static int	asr33_mode;

void tty_init(void)
{
	/* Save current settings */
	if (tcgetattr(0, &orig_termios) == -1)	/* stdin */
		log_error(errno, "tcgetattr");

	/* Prepare raw mode to emulate ASR33 */
	asr33_termios = orig_termios;
	asr33_termios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	asr33_termios.c_oflag &= ~(OPOST);
	asr33_termios.c_cflag |= (CS8);
	asr33_termios.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	asr33_termios.c_cc[VTIME] = 0;

	keyb_buffer = 0;
	keyb_flag = 0;
}

static void tty_asr33_mode(int mode)
{
	switch (mode) {
	case 1:	/* Read 1 char (blocking, no timeout). VMIN=1, VTIME=0 */
		asr33_termios.c_cc[VMIN] = 1;
		asr33_termios.c_cc[VTIME] = 0;
		break;
	case 2:	/* Read 0 or 1 char (non-blocking). VMIN=0, VTIME=0 */
		asr33_termios.c_cc[VMIN] = 0;
		asr33_termios.c_cc[VTIME] = 0;
		break;
	case 3:	/* Read 0 or 1 char (blocking, timeout = 1 sec), VMIN=0, VTIME=1 */
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
	if (asr33_mode) {
		if (tcsetattr(0, TCSAFLUSH, &orig_termios) == -1)
			log_error(errno, "tcsetattr");

		asr33_mode = 0;
	}
}

void tty_out1(int dev, int chr)
{
	char buf;

	buf = chr;
	write(1, &buf, 1);
	tty_flag = 1;
	cpu_ireq(dev, 1);	/* Request interrupt */
}

/* Set teleprinter flag to 0/1 */
/* Return old flag */
int tty_out_set_flag(int dev, int flag)
{
	int old_flag = tty_flag;

	tty_flag = flag;
	cpu_ireq(dev, flag);	/* Raise/clear int request */
	return old_flag;
}

/* Get teleprinter flag */
int tty_out_get_flag(UNUSED int dev)
{
	return tty_flag;
}

/* Wait for 1 character to be pressed */
int tty_keyb_wait1(int dev)
{
//printf("tty_keyb_wait1: keyb_flag=%d\r\n",keyb_flag);
	if (keyb_flag) return 1;
	if (asr33_mode != 1) tty_asr33_mode(1);
	if (read(0, &keyb_buffer, 1) == 1) {
		keyb_flag = 1;
		if (keyb_buffer == CTRL_C)
			cpu_stop();
		cpu_ireq(dev, 1);	/* Request interrupt */
		return 1;
	}
	keyb_flag = 0;
	cpu_ireq(dev, 0);	/* Clear interrupt request */
	return 0;
}

/* Wait for 1 character to be pressed, 1s timeout */
int tty_keyb_timed_wait1(int dev)
{
	if (keyb_flag) return 1;
	if (asr33_mode != 3) tty_asr33_mode(3);
	if (read(0, &keyb_buffer, 1) == 1) {
		keyb_flag = 1;
		if (keyb_buffer == CTRL_C)
			cpu_stop();
		cpu_ireq(dev, 1);	/* Request interrupt */
		return 1;
	}
	keyb_flag = 0;
	cpu_ireq(dev, 0);	/* Clear interrupt request */
	return 0;
}

/* Set keyboard flag to 0/1 */
/* Return old flag */
int tty_keyb_set_flag(int dev, int flag)
{
//printf("tty_keyb_set_flag(%d): was keyb_flag=%d\r\n",flag, keyb_flag);
	int old_flag = keyb_flag;

	keyb_flag = flag;
	cpu_ireq(dev, flag);
	return old_flag;
}

/* Get keyboard flag (no-wait) */
int tty_keyb_get_flag(int dev)
{
//printf("tty_keyb_get_flag(): keyb_flag=%d\r\n",keyb_flag);
	if (keyb_flag) return 1;
	if (asr33_mode != 2) tty_asr33_mode(2);
	if (read(0, &keyb_buffer, 1) == 1) {
		keyb_flag = 1;
		if (keyb_buffer == CTRL_C)
			cpu_stop();
		cpu_ireq(dev, 1);	/* Request interrupt */
		return 1;
	}
	keyb_flag = 0;
	cpu_ireq(dev, 0);	/* Clear interrupt request */
	return 0;
}

/* Read 1 character and clear flag (no-wait) */
int tty_keyb_inp1(int dev)
{
//printf("tty_keyb_inp1: keyb_flag=%d\r\n",keyb_flag);
	if (keyb_flag) {
		keyb_flag = 0;
		cpu_ireq(dev, 0);	/* Clear interrupt request */
		return keyb_buffer | 0200;
	}
	if (asr33_mode != 2) tty_asr33_mode(2);
	if (read(0, &keyb_buffer, 1) == 1) {
		keyb_flag = 0;
		if (keyb_buffer == CTRL_C)
			cpu_stop();
		cpu_ireq(dev, 0);	/* Clear interrupt request */
		return keyb_buffer | 0200;
	}
	keyb_flag = 0;
	cpu_ireq(dev, 0);	/* Clear interrupt request */
	return 0;
}
