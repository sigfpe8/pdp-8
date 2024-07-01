#include <stdio.h>
#include <sys/errno.h>

#include "pdp8.h"
#include "log.h"
#include "papertape.h"

static int ppt_ien;

static FILE* reader_fp;
static int   reader_eot;
static int   reader_flag;
static int   reader_buffer;

static FILE* punch_fp;
static int   punch_flag;

static void ppt_reader_read(void);
static void ppt_punch_write(int ch);

void ppt_init(void)
{
    ppt_ien = 0;
    reader_eot = 1;
    reader_flag = 0;
    reader_buffer = 0;
    reader_fp = 0;

    punch_flag = 0;
    punch_fp = 0;
}

//
// Paper tape reader functions
//

// Redirect paper tape reader to a file
void ppt_reader_assign(char* fname)
{
	FILE* fp;
	
	if ((fp = fopen(fname, "r")) == NULL) {
		printf("Could not open %s to redirect paper tape reader\r\n", fname);
		return;
	}

	reader_fp = fp;
}

// Enable paper tape reader interrupt (IOmec)
void ppt_reader_punch_ien(int onoff)
{
    ppt_ien = onoff;
}

// Clear end-of-tape flag (IOmec)
void ppt_reader_clear_eot(void)
{
    reader_eot = 0;
}

// Return end-of-tape flag (IOmec)
int ppt_reader_get_eot(void)
{
    return reader_eot;
}

// Return paper tape reader flag
int  ppt_reader_get_flag(void)
{
    return reader_flag;
}

// Return current character in reader buffer and clear flag
int  ppt_reader_get_buffer(void)
{
    reader_flag = 0;
    return reader_buffer;
}

// Clear the reader flag
// Initiate the reading of the next character from tape
void ppt_reader_clear_flag(void)
{
    ppt_reader_read();
}

// Try to read 1 character into the buffer
// Set reader flag according to success/failure
static void ppt_reader_read(void)
{
	int ch;

    if (reader_fp == NULL) {
        printf("There's no file assigned to the paper tape reader\r\n");
        return;
    }

	reader_flag = 0;            // Assume no character available

    if (HAVE_IOMEC_PPT && reader_eot)
        return;

	if ((ch = fgetc(reader_fp) != EOF)) {
        // We have a character
		reader_flag = 1;
        reader_buffer = ch == 10 ? 13 : ch; // \n --> \r
        if (HAVE_IOMEC_PPT) {
            if (ppt_ien)
		        cpu_ireq(PPT_READER, 1);// Request interrupt
        } else
		    cpu_ireq(PPT_READER, 1);	// Request interrupt
	} else {		                    // EOF or error
        // No character
        if (feof(reader_fp))
            reader_eot = 1;
	    else
		    log_error(errno, "fgetc");
        cpu_ireq(PPT_READER, 0);	        // Clear interrupt request
    }
}

//
// Paper tape punch functions
//

// Redirect paper tape punch to a file
void ppt_punch_assign(char* fname)
{
	FILE* fp;
	
    // Close previous assignment if necessary
    if (punch_fp) {
        if (fclose(punch_fp))
            log_error(errno, "fclose");
        punch_fp = 0;
    }

	if ((fp = fopen(fname, "w")) == NULL) {
		printf("Could not open %s to redirect paper tape punch\r\n", fname);
		return;
	}

	punch_fp = fp;
}

// Return punch flag
int ppt_punch_get_flag(void)
{
    return punch_flag;
}

void ppt_punch_clear_flag(void)
{
    punch_flag = 0;
}

void ppt_punch_putchar(int ch)
{
    ppt_punch_write(ch);
}

// Put character into tape
// Set punch_flag if success
static void ppt_punch_write(int ch)
{
    if (punch_fp == NULL) {
        printf("There's no file assigned to the paper tape punch\r\n");
        return;
    }

    if (fputc(ch, punch_fp) != EOF)
        punch_flag = 1;
    else
        log_error(errno, "fputc");
}
