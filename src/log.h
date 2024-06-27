#ifndef	_log_h
#define _log_h

extern void log_close(void);
extern void log_error(int err, char *msg);
extern void log_invalid(void);
extern void log_open(void);

#endif	/* _log_h */
