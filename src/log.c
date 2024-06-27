#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log.h"
#include "pdp8.h"

#define	LOG_BUFSIZE	256
#define ERR_BUFSIZE 128
#define	LOG_FILE	"pdp8-log.txt"

static FILE *logf = 0;
static char last_msg[LOG_BUFSIZE] = { 0 };
static int repeat = 0;

// Log invalid/unimplemented instructions
void log_invalid(void)
{
	char this_msg[LOG_BUFSIZE];

	if (!logf) return;	// Logging is not enabled

	snprintf(this_msg, sizeof(this_msg), "Invalid %05o %04o", THISPC, IR);

	if (strcmp(last_msg, this_msg)) {	// Message is different than last one
		if (last_msg[0]) {	// If we are not starting, write last message
			fprintf(logf, "%s\n", last_msg);
			if (repeat)
				fprintf(logf, "  repeated %d times\n", repeat+1);
		}
		strcpy(last_msg, this_msg);
		repeat = 0;
	} else {							// Message is repeated
		++repeat;
	}
}

void log_error(int err, char *msg)
{
    char error_msg[ERR_BUFSIZE];
	char this_msg[LOG_BUFSIZE];

    strerror_r(err, error_msg, sizeof(error_msg));

	snprintf(this_msg, sizeof(this_msg), "Error @ %05o %04o %s: %s", THISPC, IR, msg, error_msg);

    if (!logf) {    // If log is not enabled, at least notify user via tty
        fprintf(stderr, "%s\n", this_msg);
        return;
    }

	if (strcmp(last_msg, this_msg)) {	// Message is different than last one
		if (last_msg[0]) {	// If we are not starting, write last message
			fprintf(logf, "%s\n", last_msg);
			if (repeat)
				fprintf(logf, "  repeated %d times\n", repeat+1);
		}
		strcpy(last_msg, this_msg);
		repeat = 0;
	} else {							// Message is repeated
		++repeat;
	}
}

void log_open(void)
{
	// Open log file if necessary
	if (!logf) {
		if (!(logf = fopen(LOG_FILE, "a"))) {
			fprintf(stderr, "Could not open "LOG_FILE"\n");
			return;
		}
		time_t now;
		time(&now);
		fprintf(logf, "----- Opened %s", ctime(&now));
		last_msg[0] = 0;
		repeat = 0;
	}
}

void log_close(void)
{
	if (logf) {
		// Flush last message
		fprintf(logf, "%s\n", last_msg);
		if (repeat)
			fprintf(logf, "  repeated %d times\n", repeat+1);

		time_t now;
		time(&now);
		fprintf(logf, "----- Closed %s", ctime(&now));
		fclose(logf);
		logf = 0;
		last_msg[0] = 0;
		repeat = 0;
	}
}
