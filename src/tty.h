#ifndef	_tty_h
#define _tty_h

/* TTY/ASR33 public API */
extern void	tty_init(void);
extern void	tty_exit(void);

/* Teleprinter output */
extern void	tty_out1(int dev, int chr);
extern int	tty_out_get_flag(int dev);
extern int	tty_out_set_flag(int dev, int flag);

/* Keyboard input */
extern void tty_keyb_assign(char* fname);
extern int	tty_keyb_wait1(int dev);
extern int	tty_keyb_timed_wait1(int dev);
extern int	tty_keyb_get_flag(int dev);
extern int	tty_keyb_set_flag(int dev, int flag);
extern int	tty_keyb_inp1(int dev);

#define	CTRL_C	3

#ifndef UNUSED
#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#else
#define UNUSED
#endif
#endif

#endif	/* _tty_h */
