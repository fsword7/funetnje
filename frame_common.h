/* FRAME_COMMON.H		V1.0
 | Common routine for frame assembly. This is a high language code of the
 | LOAD_DMF routine...
 | Used by the VMS_IO for async terminal lines.
 |
 | Calling: The macro HANDLE_CHARACTER is defined which accepts one argument:
 | a character variable (not a pointer). It expects also to be defined a variable
 | names "status" (integer) and a pointer named "temp" which points to
 | IoLines[Index] of the current active receive.
 | Status is either zero in order to continue to next character, COMPLETE_READ
 | to signal that we have a buffer ready, and IGNORE_CHAR to ignore the current
 | character and continue to the next one. These last two symbols are bit
 | masks.
 */

/* Some common definitions: */
#define	IGNORE_CHAR	2	/* Ignore this character */
#define	COMPLETE_READ	4	/* This character completes the frame */

/* Some EBCDIC characters: */
#define	SOH		1	/* Start of header */
#define	STX		2	/* Start of text */
#define	DLE		16	/* Data link escape */
#define	ETB		38	/* End of text block */
#define	NAK		61	/* Negative ACK */
#define	ACK0		112	/* Positive ACK */

/* Flags for our usage: */
#define	DLE_FOUND	16	/* DLE was the previous character */
#define	M_DLE_FOUND	65536	/* The mask of the above value */
#define	NOT_FIRST_CHAR	31	/* Not the first character in frame */

/*
 | This routine (MACRO) gets one character and decides what to do with it.
 | It returns either 0 (queue character) or COMPLETE_READ when this character
 | should be buffered and end the frame.
 */
#define HANDLE_CHARACTER(C) {\
	status = 0;	/* Default - queue character */ \
	if((temp->TcpState & (1 << NOT_FIRST_CHAR)) == 0) {	/* First character */ \
		temp->TcpState |= (1 << NOT_FIRST_CHAR); \
		if(C == NAK) {	/* NAK is a signle character record */ \
			temp->TcpState = 0;	/* Next character will end the frame */ \
			status = COMPLETE_READ; \
		} \
		else \
		if(C == SOH) {	/* SOH-ENQ */ \
			temp->TcpState |= 2;	/* Read next character */ \
		} \
		else \
		if(C == DLE)	/* DLE found - have to check next character */ \
			temp->TcpState |= (M_DLE_FOUND); \
		else {	/* Illegal character  - Ignore and re-start packet */ \
			temp->TcpState = 0; \
			status = IGNORE_CHAR; \
		}\
	} \
/* Not first character - test whether we count CRC's now */ \
	else { \
		if((temp->TcpState & 0xff) != 0) {	/* Yes - increment count */ \
			if((++temp->TcpState & 0xff) == 3)	/* End of frame */ \
				status = COMPLETE_READ; \
		} \
		else {	/* Not counting CRCs - check character */ \
			if((temp->TcpState & M_DLE_FOUND) == 0) {	/* Handle as normal char */ \
				if(C == DLE) \
					temp->TcpState |= M_DLE_FOUND; \
			} \
			else {	/* Previous character was DLE */ \
				temp->TcpState &= ~M_DLE_FOUND; \
				if(C == ACK0) \
					status = COMPLETE_READ; \
				else \
				if(C == ETB)	/* Start counting CRCs */ \
					temp->TcpState++; \
			} \
		} \
	} \
};
