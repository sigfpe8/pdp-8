#include "pdp8.h"

// Command register
WORD RX01_CMD;

/*
	See
	https://homepage.divms.uiowa.edu/~jones/pdp8/man/rx01.html

            00 01 02 03 04 05 06 07 08 09 10 11
            ___________________________________
           |  |  |  |  |  |  |  |  |  |  |  |  |
           |__|__|__|__|__|__|__|__|__|__|__|__|
           |     |  |  |  |  |     |        |  |
           |     | H| D| M| E|  U  |    F   |  |

The command register holds the current diskette interface function, and the operation of loading the command register initiates these functions. The command register has the following fields:

H -- Head Select (not RX01).
For the RX02 and RX03, this selects the head to use. On an RX02, it must be zero (because only one head but the selection function is implemented). On an RX03, it is used to determine which side of the disk is to be used.

D -- Density (not RX01).
For the RX02, this selects the recording density to be used.

M -- Maintenance.
If this bit is set, the interface operates in maintenance mode.

E -- Eight bit mode.
If this bit is set, the interface loads and stores sectors as streams of 8 bit bytes. If reset, sectors are loaded and stored as streams of 12 bit words.

U -- Unit select.
The RX01 through RX03 dual diskette drives use only the low order bit of the unit select field. The DSD210 allows up to 4 drives and uses the full unit select field.

F -- Function select.
		0 -- Fill Buffer.
		1 -- Empty Buffer.
		2 -- Write Sector.
		3 -- Read Sector.
		4 -- No Op.
		5 -- Read Status.
		6 -- Write Deleted Data Sector.
		7 -- Read Error Register.

	MAINT=  0200    / the maintenance bit
    BITS8=  0100    / select 8 bit mode
    BITS12= 0000    / select 12 bit mode
    DRIVE0= 0000    / select drive 0
    DRIVE1= 0020    / select drive 1
    FNFILL= 0000    / fill buffer from cpu
    FNEMPT= 0002    / empty buffer to cpu
    FNWRIT= 0004    / write buffer to disk
    FNREAD= 0006    / read buffer from disk
    FNRDST= 0012    / read status to cpu
    FNWRDE= 0014    / write deleted buffer to disk
    FNRDER= 0016    / read error register to cpu
*/

#define RX01_MAINT		0200	// the maintenance bit
#define RX01_BITS8		0100	// select 8 bit mode
#define RX01_BITS12		0000	// select 12 bit mode
#define RX01_DRIVE0		0000	// select drive 0
#define RX01_DRIVE1		0020	// select drive 1
#define RX01_FNFILL		0000	// fill buffer from cpu
#define RX01_FNEMPT		0002	// empty buffer to cpu
#define RX01_FNWRIT		0004	// write buffer to disk
#define RX01_FNREAD		0006	// read buffer from disk
#define RX01_FNRDST		0012	// read status to cpu
#define RX01_FNWRDE		0014	// write deleted buffer to disk
#define RX01_FNRDER		0016	// read error register to cpu


// Error Status Register
WORD RX01_ERSTAT;

/*

Error Status Register
            00 01 02 03 04 05 06 07 08 09 10 11
            ___________________________________
           |  |  |  |  |  |  |  |  |  |  |  |  |
           |__|__|__|__|__|__|__|__|__|__|__|__|
                           DD    DER   ID    CRC
                        RDY   DEN   WP    PAR
                                    RX2   DS
The Error Status Register contains a large number of one-bit flags; for some of these, the meaning depends on which drive type is in use.
RDY -- Device ready.
DD -- Deleted data encountered on read.
DEN -- not RX01: Density of sector just read or written.
DER -- not RX01: Density error in read.
WP -- RX01 and DSD210 only: Write protect or not-ready.
RX2 -- not RX01: always 1, reports RX02 attached.
ID -- initialization done.
PAR -- RX01 and DSD210 only: Parity error detected.
DS -- RX03 only: Double sided media detected.
CRC -- A CRC Error was detected.

*/

// Error code register
WORD RX01_ERCODE;

/*

Error Code Register
            00 01 02 03 04 05 06 07 08 09 10 11
            ___________________________________
           |  |  |  |  |  |  |  |  |  |  |  |  |
           |__|__|__|__|__|__|__|__|__|__|__|__|
           |           |                       |
           |           |      Error Code       |

The Error Code Register contains a numeric error code indicating the cause of the most recently detected error. Codes are given in octal.
0010 -- Drive 0 failed to see home on initialize.
0020 -- Drive 1 failed to see home on initialize.
0030 -- Found home prematurely on init (RX01 only).
0040 -- Bad track address.
0050 -- Home was found prematurely during seek.
0060 -- Self diagnostic error during init (RX01 only).
0070 -- Seek failed (bad sector address).
0100 -- Attempt to write a protected disk (RX01 only).
0110 -- No diskette in the requested drive.
0120 -- No preamble found (disk not formatted).
0130 -- Preamble but no ID mark (bad format).
0140 -- CRC error on a header (RX01 only).
0150 -- Wrong track address in header.
0160 -- Too many tries to find header.
0170 -- No data following header.
0200 -- CRC error on read from disk.
0210 -- Internal parity error (not RX01).
0220 -- R/W electronics failed self-test (not RX01).
0230 -- Word count overflow (not RX01).
0240 -- Density error (not RX01).
0250 -- Wrong key for set media density (not RX01).

*/

