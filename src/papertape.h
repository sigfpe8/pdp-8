#ifndef _papertape_h
#define _papertape_h


extern void ppt_init(void);

extern void ppt_reader_punch_ien(int onoff);
extern void ppt_reader_assign(char* fname);
extern void ppt_reader_clear_eot(void);
extern void ppt_reader_clear_flag(void);
extern int  ppt_reader_get_buffer(void);
extern int  ppt_reader_get_eot(void);
extern int  ppt_reader_get_flag(void);
extern void ppt_punch_asssign(char* fname);
extern int  ppt_punch_get_flag(void);
extern void ppt_punch_clear_flag(void);
extern void ppt_punch_putchar(int ch);

#define PPT_READER      1
#define PPT_PUNCH       2

#endif  // _papertape_h
