/* SEND_FILE.C		V2.1
 | Copyright (c) 1988,1989,1990,1991 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use or misuse of this software.
 |
 | Send a file to the other side. This module fills a buffer and send it to
 | the other side.
 | When sending a NetData file, if the file has empty lines we convert them to
 | lines with one space. If we leave them as empty lines than IBM won't eat it...
 | TO DO: 1. When sending NETDATA, we have to see whether a record have place
 |           inside the block. We currently check whether there is place for
 |           90 characters (4 for punch headers and 4 for SCBs). This check should
 |           be improved to take the size after the compression.
 | It currently supports only stream #0.
 | There is no check thAT the compressed line fits inside it's compression buffer.
 | The way that a line which had no room in  the buffer is saved for the next
 | one should be changed when converting to multiple streams.
 | Currently we can send NET DATA files of records up to 512 bytes only.
 | We do not confirm reception of NetData, even if requested so.
 |
 | When we send a file, we currently send it with SRCB of "No carriage control".
 |
 | V1.1 - SEND_FILE_BUFFER - transpose the last two blocks in the function
 |        (the one that sends the buffer and the one that sets the line's state)
 |        for Unix compatibility (the ACK after send is not AST, thus delivered
 |        before we set the correct line's state).
 | V1.2 - Make the sending file more efficient. Remove unnecessary characters
 |        moves.
 | V1.3 - NetData bug 5 was replaced from a fatal bugcheck to error message in
 |        logfile. The file processing continues.
 | V1.4 - Correct a bug when the last INMR06 is placed in buffer. There was no
 |        closing SCB...
 | V1.5 - Correct another bug with INMR: if the last record is exactly 80 characters,
 |        there is an extra null RCB. Remove it...
 | V1.6 - Change the NetData section to handle binary records and other records
 |        which are up to 512 bytes long.
 | V1.7 - Correct a bug sending files with records longer than 253 bytes. We sent
 |        segments which are identical, since we did not advance in the input
 |        record while segmenting it.
 | V1.8 - 21/2/90 - Replace BCOPY calls with memcpy() calls.
 | V1.9 - 27/3/90 - Make this module more modular = split some functions.
 | V2.0 - 11/3/91 - Add MultiStream support.
 | V2.1 - 7/5/91 - When the file is of type PRINT and ASA raise the line
 |        length by 1 as JES2 does.
 */
#include "consts.h"
#include "headers.h"

EXTERNAL struct	LINE	IoLines[MAX_LINES];
char	*strchr(), *strrchr();

/* Macro to estimate the length of the line after compression. Since we do not
   compress repeatitive characters, it is simply the number of characters
   divided by 63 (the number of added SCBs) + 5 (=RCB+SRCB+length count+
   final-SCB + 1[round-up]).
*/
#define	EXPECTED_SIZE(LENGTH)	(LENGTH + (int)(LENGTH / 63) + 5)


/*
 | Send Punch or Print files (mails are punch files).
 | Fill the buffer and send it. If there is no room for a line in the buffer,
 | it is saved to the next call to this routine in a
 | static buffer.
 */
send_file_buffer(Index)
int	Index;
{
	register int	MaxSize, CurrentSize, i, TempVar;
	register unsigned char	*p;
	unsigned char	OutputLine[MAX_BUF_SIZE];
	struct	FILE_PARAMS	*FileParams;
	int	DecimalStreamNumber, compress_scb(), uread();
	struct	LINE	*temp;

	temp = &IoLines[Index];
	DecimalStreamNumber = temp->CurrentStream;

/* retreive the buffer size from the lines database */
	FileParams = &((IoLines[Index].OutFileParams)[DecimalStreamNumber]);
	MaxSize = IoLines[Index].MaxXmitSize;
	MaxSize -= 14;
	/* Leading BCB, FCS, DLE+..., trailing PAD, 2*crc, DLE+ETB, last SCB,
	   last RCB */
	CurrentSize = 0;	/* Empty output buffer */

/* Check whether there is something left from before */
	if(temp->XmitSavedSize[DecimalStreamNumber] > 0) {
		MaxSize -= EXPECTED_SIZE(temp->XmitSavedSize[DecimalStreamNumber]);
		if(MaxSize < 0)
			bug_check("SEND_FILE, Saved punch record too long.");
		FileParams->RecordsCount += 1;	/* Increment records count */
		OutputLine[CurrentSize++] = (((DecimalStreamNumber + 9) << 4) | 0x9);
/* If the file is of type EBCDIC, the recrd already contains the SRCB. If ASCII,
   add wither ASA CC if user reuqested so. If not - use no CC.
*/
		if(FileParams->format == ASCII) {
			if((FileParams->type & F_ASA) != 0)
				OutputLine[CurrentSize++] = CC_ASA_SRCB;
			else
				OutputLine[CurrentSize++] = CC_NO_SRCB;
			CurrentSize +=
				compress_scb(temp->XmitSavedLine[DecimalStreamNumber],
					&OutputLine[CurrentSize],
					temp->XmitSavedSize[DecimalStreamNumber]);
		}
		else {	/* EBCDIC - the SRCB is the second character in saved
			   string. */
			OutputLine[CurrentSize++] = temp->XmitSavedLine[DecimalStreamNumber][1];
			CurrentSize +=
				compress_scb(&temp->XmitSavedLine[DecimalStreamNumber][2],
					&OutputLine[CurrentSize],
					temp->XmitSavedSize[DecimalStreamNumber] - 1);
		}
		temp->XmitSavedSize[DecimalStreamNumber] = 0;
	}

/* Fill the buffer untill there is no place in it */
	while((i = uread(Index, temp->CurrentStream, &(temp->XmitSavedLine[DecimalStreamNumber][1]),
	  sizeof(IoLines[0].XmitSavedLine[DecimalStreamNumber]) - 1)) >= 0) {
		temp->XmitSavedSize[DecimalStreamNumber] = i;
		if(FileParams->format == ASCII) {
/* Truncate long records to 80/132 characters, and pad short ones to this size */
			if((FileParams->type & F_PRINT) != 0) {
/* Print format is: 132 for non-CC, 133 for print with CC */
				if((FileParams->type & F_ASA) != 0) {
					if(i > 133) i = 133;
				} else {
					if(i > 132) i = 132;
				}
			}
			else {	/* All others are sent as punch */
				if(i > 80) i = 80;
			}
			p = &(temp->XmitSavedLine[DecimalStreamNumber][1]);
			ASCII_TO_EBCDIC(p, p, i);
/* Read and write into the same string ^^^ */
/* Write the expected length into the length count field. If the real length
   is smaller, then the remote side will pad with blanks.
*/
			if((FileParams->type & F_PRINT) != 0) {
				if((FileParams->type & F_ASA) != 0) /* Add 1 for CC */
					*temp->XmitSavedLine[DecimalStreamNumber] = (unsigned char)(133);
				else
					*temp->XmitSavedLine[DecimalStreamNumber] = (unsigned char)(132);
			} else
				*temp->XmitSavedLine[DecimalStreamNumber] = (unsigned char)(80);
			i++;	/* Text length + count length */
			temp->XmitSavedSize[DecimalStreamNumber] = i;
		}
		/* if EBCDIC, no need for translation */
		else {
/* If the file is S&F, the NJT is a normal record in it. However, it must be
   the first in a block - so check it.
*/
			if(temp->XmitSavedLine[DecimalStreamNumber][1] == NJT_SRCB) {
			/* The NJT must be on start of block */
				if(CurrentSize != 0)
					break;
			}
		}

/* Put into the current block if there is space for */
		if((MaxSize -= EXPECTED_SIZE(temp->XmitSavedSize[DecimalStreamNumber])) >= 0) {
/* In S&F files the NJH and DSH are the first two records in the file. Identify
   them, and send each one as a separate block.
*/
			if(FileParams->format != ASCII) {
				if(FileParams->RecordsCount < 3) {
			/* The NJH and DSH - they must be the only record in block */
					if(CurrentSize != 0)
			/* If we already put there something */
						break;
				}
			}
			FileParams->RecordsCount += 1;
			/* there is room for this string */
			OutputLine[CurrentSize++] =
				(((DecimalStreamNumber + 9) << 4) | 0x9);
			if(FileParams->format == ASCII) {
				if((FileParams->type & F_ASA) != 0)
					OutputLine[CurrentSize++] = CC_ASA_SRCB;
				else
					OutputLine[CurrentSize++] = CC_NO_SRCB;
				CurrentSize +=
					compress_scb(temp->XmitSavedLine[DecimalStreamNumber],
						&OutputLine[CurrentSize],
						temp->XmitSavedSize[DecimalStreamNumber]);
			}
			else {	/* EBCDIC - the SRCB is the second character in saved
				   string. */
				OutputLine[CurrentSize++] =
					temp->XmitSavedLine[DecimalStreamNumber][1];
				CurrentSize +=
					compress_scb(&temp->XmitSavedLine[DecimalStreamNumber][2],
						&OutputLine[CurrentSize],
						temp->XmitSavedSize[DecimalStreamNumber] - 1);
			}
			temp->XmitSavedSize[DecimalStreamNumber] = 0;
		}
		else {
			break;	/* No room - abort loop */
		}
	}

/* Now check why loop has been terminated */
	if(temp->XmitSavedSize[DecimalStreamNumber] == 0) {	/* End of file - mark it */
		if(FileParams->format == ASCII)	/* We will have to send NJT */
			IoLines[Index].OutStreamState[DecimalStreamNumber] = S_EOF_FOUND;
		else
			IoLines[Index].OutStreamState[DecimalStreamNumber] = S_NJT_SENT;
			/* NJT was sent already as part of file */
	}

/* Send the buffer filled */
	if(CurrentSize > 0) {	/* Something was filled in buffer - send it */
		/* Add EOR RCB */
		OutputLine[CurrentSize++] = 0;
		send_data(Index, OutputLine, CurrentSize, (int)(ADD_BCB_CRC));
	}
	return;
}


/*
 | Send a file in Netdata format. The flag tells whether this a start of file
 | (0) and we have to send the INMR's, or whether we are in the middle of a
 | file (1).
 */
#define	INMFTIME	0x1024
#define	INMLRECL	0x42
#define	INMNUMF		0x102f
#define	INMFNODE	0x1011
#define	INMFUID		0x1012
#define	INMTNODE	0x1001
#define	INMTUID		0x1002
#define	INMSIZE		0x102c
#define	INMDSORG	0x3c
#define	INMUTILN	0x1028
#define	INMRECFM	0x49
#define	INMDSNAM	0x2
#define	PHYSICAL	0x4000	/* File organization - Physical */
#define	VAR		0x4000	/* Record format - Variable */
#define	ASA_CC		0x400	/* ASA carriage control */

/* Take the Netdata buffer, and separate it into Punch records: */
#define	FILL_NETDATA_BUFFER { \
	OutputLinePosition = 0; \
	while(temp->XmitSavedSize[DecimalStreamNumber] > 80) {	/* Split it */ \
		if(90 > MaxSize) \
			break;	/* No room for this record */ \
		FileParams->RecordsCount += 1;	/* Increment Punch records count */ \
		OutputLine[CurrentPosition++] = (((DecimalStreamNumber + 9) << 4) | 0x9); \
		OutputLine[CurrentPosition++] = CC_NO_SRCB; \
		OutputLine[CurrentPosition++] = 0xc1; \
		OutputLine[CurrentPosition++] = 80; \
		j = compress_scb(&temp->XmitSavedLine[DecimalStreamNumber][OutputLinePosition], \
			&OutputLine[CurrentPosition], (int)(80)); \
		CurrentPosition += j; \
		OutputLinePosition += 80; \
		MaxSize -= (j + 4); \
		temp->XmitSavedSize[DecimalStreamNumber] -= 80; \
	} \
}

send_netdata_file(Index, flag)
int	Index, flag;
{
	char		line[LINESIZE], TempLine[MAX_BUF_SIZE];
	unsigned char	OutputLine[MAX_BUF_SIZE];
	register int	j, i, size, CurrentPosition,	/* Position in Punch buffer */
			OutputLinePosition,	/* Position in NetData buffer */
			MaxSize,	/* Size of xmission buffer */
			InputPosition;	/* While decomposing long records */
	int		TempVar;
	register char	*p, *q;
	struct	FILE_PARAMS	*FileParams;
	int	DecimalStreamNumber, insert_text_unit();
	struct	LINE	*temp;

	temp = &IoLines[Index];
	DecimalStreamNumber = temp->CurrentStream;
	FileParams = &((IoLines[Index]).OutFileParams)[DecimalStreamNumber];

	CurrentPosition = 0;

	if(flag == 0)
		/* We have to send the first INMR's. */
		size = fill_inmr01(Index, temp, FileParams);

/* Add normal records to the Net data. */
	MaxSize = IoLines[Index].MaxXmitSize;	/* Our buffer size */
	MaxSize -= 20;
	OutputLinePosition = 0;

/*
 | Add records as long as we have them. Read records into NetData buffer as long
 | as we have place for the longest record. When we don't have such a place, we
 | start creating the punch records. After that, we try reading again NetData
 | records; this is because the records are probably shorter than the maximum
 | length, so we can add more records into the sending block. Not very nice
 | algorithm, but more efficient...
 */
	while((i = uread(Index, temp->CurrentStream, TempLine, (int)(sizeof TempLine))) >= 0) {
ReadAgain:	if(i > 512) i = 512;	/* Truncate record */
/* Add one space to blank lines, otherwise IBM won't eat it... */
		if(i == 0) {
			*TempLine = ' ';
			i++;
		}
		if((i + temp->XmitSavedSize[DecimalStreamNumber] + 6) >	/* +6 for 3 Netdata records */
		    sizeof(IoLines[Index].XmitSavedLine[DecimalStreamNumber])) {
			logger((int)(1),
				"SEND_FILE: No room for Netdata record.\
 Line=%d, Class=%c, format=%d, from=%s, to=%s\n",
	Index, FileParams->JobClass, FileParams->format, FileParams->From,
	FileParams->To);
			logger((int)(1), "  SavedSize=%d, Record length=%d, total\
 buffer size allowed=%d\n",
				temp->XmitSavedLine[DecimalStreamNumber], i, sizeof(IoLines[Index].XmitSavedLine[DecimalStreamNumber]));
			i = 0; continue;
		}
/* Append to previous NetData buffer. If longer than 253 characters then separate
   to NetData segments. Each segment is followed by 2-bytes header.
*/
		if(i <= 253) {	/* One segment */
				/* Two bytes header: */
			temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = i + 2;
				/* Length of data + 2 header bytes */
			temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = 0xc0;
				/* C0 = data record (not splitted). */
			p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]];
			if(FileParams->format == ASCII) {
				ASCII_TO_EBCDIC(TempLine, p, i);
			} else {
				memcpy(p, TempLine, i);
			}
			temp->XmitSavedSize[DecimalStreamNumber] += i;
		}
		else {
/* Separate the block into smaller segments */
			/* Put the first record */
			temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = 255;
			temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = 0x80;
				/* 80 = First part of data record. */
			p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]];
			if(FileParams->format == ASCII) {
				ASCII_TO_EBCDIC(TempLine, p, 253);
			} else {
				memcpy(p, TempLine, 253);
			}
			temp->XmitSavedSize[DecimalStreamNumber] += 253; i -= 253;
			InputPosition = 253;	/* Next character position in TempLine */
			/* The middle segments */
			while(i > 253) {
				temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = 255;
				temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = 0;
					/* 0 = middle segment */
				p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]];
				q = (char*)&TempLine[InputPosition];
				if(FileParams->format == ASCII) {
					ASCII_TO_EBCDIC(q, p, 253);
				} else {
					memcpy(p, q, 253);
				}
				temp->XmitSavedSize[DecimalStreamNumber] += 253; i -= 253;
				InputPosition += 253;
			}
			/* The last segment */
			temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = i + 2;
			temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = 0x40;
				/* 40 = Last segment of data record */
			p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]];
			q = (char*)&TempLine[InputPosition];
			if(FileParams->format == ASCII) {
				ASCII_TO_EBCDIC(q, p, i);
			} else {
				memcpy(p, q, i);
			}
			temp->XmitSavedSize[DecimalStreamNumber] += i;
		}

/* Now, just to be sure, check that there is no overflow of the buffer */
		if(temp->XmitSavedSize[DecimalStreamNumber] >= sizeof(IoLines[Index].XmitSavedLine[DecimalStreamNumber])) {
			logger((int)(1), "SEND_FILE: NetData buffer overflow on line %d\n",
				Index);
			bug_check("NetData buffer overflow");
		}

/* Do we have more space in input buffer for the longest line?. Do not replace
   this line with a continue (so the outer loop will do the read), since I
   value is checked outside this loop. */
		if((temp->XmitSavedSize[DecimalStreamNumber] + 512 + 6) < sizeof(IoLines[Index].XmitSavedSize[DecimalStreamNumber])) {
			/* 512 is the block size; +6 for 3 segment headers */
			/* Yes - we have the place */
			if((i = uread(Index, temp->CurrentStream, TempLine, (int)(sizeof TempLine))) >= 0)
				goto ReadAgain;
		}

	/* Can we now create one full punch record? */
		OutputLinePosition = 0;
		FILL_NETDATA_BUFFER;
		if(temp->XmitSavedSize[DecimalStreamNumber] > 0) {
			/* Move it to start of buffer */
			p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][OutputLinePosition];
			memcpy(temp->XmitSavedLine[DecimalStreamNumber], p, temp->XmitSavedSize[DecimalStreamNumber]);
		}
		if(90 > MaxSize)
			break;	/* No room for next record */
	}


/* Check why loop has been terminated */
	if(i < 0) {	/* End of file - mark it */
		IoLines[Index].OutStreamState[DecimalStreamNumber] = S_EOF_FOUND;
		if((temp->XmitSavedSize[DecimalStreamNumber] + 8) > sizeof(IoLines[Index].XmitSavedLine[DecimalStreamNumber])) {
			/* No room for last INMR06. Should never happen */
			logger((int)(1), "SEND_FILE: Sending netdata, bug=1\n");
			exit(2);
		}
		temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = 8;
		temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]++] = 0xe0;
		p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]];
		strcpy(TempLine, "INMR06");
		ASCII_TO_EBCDIC(TempLine, p, 6);
		temp->XmitSavedSize[DecimalStreamNumber] += 6;
	}

/* Deblock last NetData buffer into punch records. Leave in buffer the last
   segment that can't fill a whole block */
	FILL_NETDATA_BUFFER;

/* Check whether we have to null fill the last buffer */
	if(IoLines[Index].OutStreamState[DecimalStreamNumber] == S_EOF_FOUND) {
		if((temp->XmitSavedSize[DecimalStreamNumber] + 10) > MaxSize) {
			IoLines[Index].OutStreamState[DecimalStreamNumber] = S_SENDING_FILE;
			/* So we'll send the next records */
			/* Remove the last INMR06, since it'll be added again
			   on next call */
			if(temp->XmitSavedSize[DecimalStreamNumber] < 8)	/* It is not there... */
				logger((int)(1),
					"SEND_FILE, Netdata bug=5. Please inform INFO@HUJIVMS");
				/* Leave it there. We'll have double INMR06... */
			else	/* It is there - remove it. */
				temp->XmitSavedSize[DecimalStreamNumber] -= 8;
		}
		else {
			OutputLine[CurrentPosition++] = (((DecimalStreamNumber + 9) << 4) | 0x9);
			OutputLine[CurrentPosition++] = CC_NO_SRCB;
			OutputLine[CurrentPosition++] = 0xc1;
			OutputLine[CurrentPosition++] = 80;
			j = compress_scb(&temp->XmitSavedLine[DecimalStreamNumber][OutputLinePosition],
				&OutputLine[CurrentPosition], temp->XmitSavedSize[DecimalStreamNumber]);
			CurrentPosition += j;
			j = 80 - temp->XmitSavedSize[DecimalStreamNumber];	/* Number of Nulls we have to pad */
			temp->XmitSavedSize[DecimalStreamNumber] = 0;
			if(j > 0) {	/* Have to extend string. Remoce last SCB=0 */
					CurrentPosition--;
				while(j > 31) {
					OutputLine[CurrentPosition++] = 0xbf;
					OutputLine[CurrentPosition++] = 0;
					j -= 31;
				}
				OutputLine[CurrentPosition++] = 0xa0 + j;
				OutputLine[CurrentPosition++] = 0;
				OutputLine[CurrentPosition++] = 0;	/* Closing SCB */
			}
		}
	}

	if(temp->XmitSavedSize[DecimalStreamNumber] > 0) {
		/* Move it to start of buffer */
		p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][OutputLinePosition];
		memcpy(temp->XmitSavedLine[DecimalStreamNumber], p, temp->XmitSavedSize[DecimalStreamNumber]);
	}

/* Send the buffer filled */
	if(CurrentPosition > 0) {	/* Something was filled in buffer - send it */
		/* Add EOR RCB */
		OutputLine[CurrentPosition++] = 0;	/* EOR RCB */
		send_data(Index, OutputLine, CurrentPosition, (int)(ADD_BCB_CRC));
	}
}


/*
 | Start of a Netdata file. Fill the first 3 INMR's into the buffer.
 */
fill_inmr01(Index, temp, FileParams)
int	Index;
struct	LINE	*temp;
struct	FILE_PARAMS	*FileParams;
{
	char	TempLine[MAX_BUF_SIZE], line[LINESIZE];
	register int	size, CurrentPosition,	/* Position in Punch buffer */
			OutputLinePosition,	/* Position in NetData buffer */
			i, TempVar;
	register char	*p;
	int	DecimalStreamNumber, insert_text_unit();

	DecimalStreamNumber = temp->CurrentStream;
	temp->XmitSavedSize[DecimalStreamNumber] = 0;
	size = 1;	/* We use SavedLine as temporary storage
			   We start from the second position, since
			   the first one will be the length */
	temp->XmitSavedLine[DecimalStreamNumber][size++] = 0xe0;	/* Control record */

	p = (char*)&(temp->XmitSavedLine[DecimalStreamNumber][size]);
	strcpy(TempLine, "INMR01");
	ASCII_TO_EBCDIC(TempLine, p, 6);
	size += 6;
	strcpy(line, FileParams->From);
	if((p = (char*)strchr(line, '@')) == NULL) p = line;
	*p++ = '\0';	/* Separate username from nodename */
	i = strlen(line); 	/* Username */
	ASCII_TO_EBCDIC(line, line, i);
	size += insert_text_unit((int)(INMFUID), (int)(0), line,
		i, (int)(1), &(temp->XmitSavedLine[DecimalStreamNumber][size]));
	i = strlen(p);		/* Node name */
	ASCII_TO_EBCDIC(p, p, i);
	size += insert_text_unit((int)(INMFNODE), (int)(0), p,
		i, (int)(1), &(temp->XmitSavedLine[DecimalStreamNumber][size]));

	strcpy(line, FileParams->To);
	if((p = (char*)strchr(line, '@')) == NULL) p = line;
	*p++ = '\0';	/* Separate username from nodename */
	i = strlen(line); 	/* Username */
	ASCII_TO_EBCDIC(line, line, i);
	size += insert_text_unit((int)(INMTUID), (int)(0), line,
		i, (int)(1), &(temp->XmitSavedLine[DecimalStreamNumber][size]));
	i = strlen(p);		/* Node name */
	ASCII_TO_EBCDIC(p, p, i);
	size += insert_text_unit((int)(INMTNODE), (int)(0), p,
		i, (int)(1), &(temp->XmitSavedLine[DecimalStreamNumber][size]));

/* Create a dummy time stamp */
	strcpy(TempLine, "19880101");
	i = strlen(TempLine);
	ASCII_TO_EBCDIC(TempLine, line, i);
	size += insert_text_unit((int)(INMFTIME), (int)(0), line,
		i, (int)(1), &(temp->XmitSavedLine[DecimalStreamNumber][size]));
	size += insert_text_unit((int)(INMLRECL), (int)(80), (int)(0),
		(int)(0), (int)(0), &(temp->XmitSavedLine[DecimalStreamNumber][size]));
	size += insert_text_unit((int)(INMNUMF), (int)(1), (int)(0),
		(int)(0), (int)(0), &(temp->XmitSavedLine[DecimalStreamNumber][size]));

/* Second INMR */
	temp->XmitSavedLine[DecimalStreamNumber][0] = size;
	temp->XmitSavedSize[DecimalStreamNumber] = size;
	size = 1;

	temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size] = 0xe0; size++;
	p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][size + temp->XmitSavedSize[DecimalStreamNumber]];
	strcpy(TempLine, "INMR02");
	ASCII_TO_EBCDIC(TempLine, p, (int)(6));
	size += 6;
	p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][size + temp->XmitSavedSize[DecimalStreamNumber]];
	memcpy(p, "\0\0\0\1", (int)(4)); /* one file */
	size += 4;
	strcpy(TempLine, "INMCOPY");
	i = strlen(TempLine);
	ASCII_TO_EBCDIC(TempLine, line, i);
	size += insert_text_unit((int)(INMUTILN), (int)(0), line,
		i, (int)(1), &temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
	size += insert_text_unit((int)(INMDSORG), (int)(PHYSICAL),
		(int)(0), (int)(0), (int)(0),
		&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
	size += insert_text_unit((int)(INMLRECL), (int)(256), (int)(0),
		(int)(0), (int)(0),
		&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
	if((FileParams->type & F_ASA) != 0)
		i = VAR | ASA_CC;
	else
		i = VAR;	/* Variable records, no CC */
	size += insert_text_unit((int)(INMRECFM), i, (int)(0),
		(int)(0), (int)(0),
		&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
	size += insert_text_unit((int)(INMSIZE),
		(int)(IoLines[Index].OutFileParams[DecimalStreamNumber].FileSize),
		(int)(0),
		(int)(0), (int)(0),
		&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
/* Create the file name as "A File-name File-Ext". The A is for IBM and must
   be the first */
	sprintf(line, "A %s %s", FileParams->FileName,
			FileParams->FileExt);
	i = strlen(line);
	ASCII_TO_EBCDIC(line, line, i);
	size += insert_text_unit((int)(INMDSNAM), (int)(0), line,
		i, (int)(1), &temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
	temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]] = size;
	temp->XmitSavedSize[DecimalStreamNumber] += size;
	size = 1;

/* Third INMR */
	temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size] = 0xe0; size++;
	p = (char*)&temp->XmitSavedLine[DecimalStreamNumber][size + temp->XmitSavedSize[DecimalStreamNumber]];
	strcpy(TempLine, "INMR03");
	ASCII_TO_EBCDIC(TempLine, p, (int)(6));
	size += 6;
	if((FileParams->type & F_ASA) != 0)
		i = VAR | ASA_CC;
	else
		i = VAR;	/* Variable records, no CC */
	size += insert_text_unit((int)(INMRECFM), i, (int)(0),
		(int)(0), (int)(0),
		&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
	size += insert_text_unit((int)(INMLRECL), (int)(80), (int)(0),
		(int)(0), (int)(0),
		&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
	size += insert_text_unit((int)(INMDSORG), (int)(0x4000), (int)(0),
		(int)(0), (int)(0),
		&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
	size += insert_text_unit((int)(INMSIZE),
		(int)(IoLines[Index].OutFileParams[DecimalStreamNumber].FileSize),
		(int)(0),
		(int)(0), (int)(0),
		&temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber] + size]);
	temp->XmitSavedLine[DecimalStreamNumber][temp->XmitSavedSize[DecimalStreamNumber]] = size;
	temp->XmitSavedSize[DecimalStreamNumber] += size;

	return size;
}


/*
 | Insert a signle text unit into the output line. The input is either string
 | or numeric. In case of string, the size of a string is limited to 80
 | characters.
 | The returned value is the number of characters written to OutputLine.
 | Input parameters:
 |     InputKey - The keyword (a number).
 |     InputParameter - A numeric value if the keyword needs numeric data.
 |     InputString - the parameter when keyword needs string data.
 | (Either InputParameter or InputString is used, but not both).
 |     InputSize - size of the string (if exists).
 |     InputType = Do we have to use InputParam or InputString?
 */
int
insert_text_unit(InputKey, InputParameter, InputString, InputSize, InputType,
	OutputLine)
int	InputKey, InputType, InputParameter, InputSize;
unsigned char	*InputString, *OutputLine;
{
	int	size, i, j, TempVar;
	unsigned char	TextUnits[3][SHORTLINE];
	int	NumTextUnits;
	size = 0;

	OutputLine[size++] = ((InputKey & 0xff00) >> 8);
	OutputLine[size++] = (InputKey & 0xff);

	if(InputType == 0) {		/* Number */
		OutputLine[size++] = 0;
		OutputLine[size++] = 1;		/* One item only */
		OutputLine[size++] = 0;	/* Count is 2 or 4, so the high order byte
					   count is always zero */
		/* Test whether it fits in 2 bytes or needs 4 bytes: */
		if(InputParameter <= 0xffff) {
			OutputLine[size++] = 2;	/* We use 2 bytes integer */
			OutputLine[size++] = ((InputParameter & 0xff00) >> 8);
			OutputLine[size++] = (InputParameter & 0xff);
			return size;
		} else {
			OutputLine[size++] = 4;	/* We use 4 bytes integer */
			OutputLine[size++] = ((InputParameter & 0xff000000) >> 24);
			OutputLine[size++] = ((InputParameter & 0xff0000) >> 16);
			OutputLine[size++] = ((InputParameter & 0xff00) >> 8);
			OutputLine[size++] = (InputParameter & 0xff);
			return size;
		}
	}

/* String: */
/* Check whether the string has more than one value (separated by spaces).
   If so, convert it to separate text units */
	NumTextUnits = 0; j = 0;
	for(i = 0; i < InputSize; i++) {
		if(InputString[i] == E_SP) {	/* Space found */
			TextUnits[NumTextUnits][0] = j;	/* Size */
			NumTextUnits++;	/* We do not check for overflow */
			j = 0;
		} else {
			TextUnits[NumTextUnits][j + 1] = InputString[i];
			j++;
		}
	}
	TextUnits[NumTextUnits++][0] = j;	/* Size */

	OutputLine[size++] = 0;
	OutputLine[size++] = NumTextUnits;		/* One item only */
	for(i = 0; i < NumTextUnits; i++) {
		OutputLine[size++] = 0;
		OutputLine[size++] = (TextUnits[i][0] & 0xff);
		memcpy(&OutputLine[size], &(TextUnits[i][1]),
			(int)(TextUnits[i][0] & 0xff));
		size += TextUnits[i][0] & 0xff;
	}
	return size;
}
