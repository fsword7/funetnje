/* RECV_FILE.C	V3.0
 | Copyright (c) 1988,1989,1990,1991,1992 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use or misuse of this software.
 |
 | Receive a file from the remote. Really, it just parses the incoming
 | record that starts with an RCB that points to SYSOUT-n.
 | We are called each time with one record only - It's format is:
 | RCB SRCB LENGTH record data.
 | The routine that writes a record to output file currently does minimal
 | SRCB processing: ASA control is left as first character of text, and
 | machine CC is converted into ASA one (and I hope it is correct...).
 | In parse_net_data, the buffer is copied to its beginning each time a record
 | is parsed. Change it to use cyclic buffer.
 | NetData control records are partially parsed for information only. Enhance
 | this feature. At present, control fields with count=0 are not supported.
 | Does not support more than one file in Netdata transmission.
 | NETDATA: Fragments of splitted records are written as separate records.
 | this will be corrected in the future.
 |
 | V1.1 - The saved dataset and job header were kept in a single storage for all
 |        lines. The memory allocated has been moved for a per-line bases (in the
 |        IoLines module).
 | V1.2 - Save the file size returned by the close function and pass it to the
 |        routine that requeues the file to another line.
 | V1.3 - Accept SYSIN files also (they don't have NDH) <=== Have to debugged !!!
 | V1.4 - Get the file size when queuing the file to another link.
 |        Store the job id in FID envelope field.
 |        Honor the QUIET option used in NDHGFORM field.
 | V1.5 - Keep a second fragment of job-header is arrives.
 | V1.6 - Add parameters in call to Inform-Mailer for Unix version.
 | V1.7 - 4/1/90 - In unix, the inform-mailer returns the text to send  the
 |        originator as an NMR message.
 | V1.8 - 15/1/90 - A modification to the Unix's change (V1.8) - The Inform-mailer
 |        function sends all the NMRs, so we don't have to deal with them here.
 | V1.9 - 4/2/90 - If a received file is NetData and has class N, do not translate
 |        it to ASCII.
 | V2.0 - Replace BCOPY() calls with memcpy();
 | V2.1 - 18/3/90 - Correct binary file handling. In the internal strucutres we
 |        saved incorrectly ASCII value for binary files.
 | V2.2 - 27/3/90 - make this module a little bit more modular = split some long
 |        functions.
 | V2.3 - 29/3/90 - When looking for a file's destination, replace the call to
 |        get_route_record() with find_line_index(); Here we assume that
 |        files whose destination is LOCAL are ASCII and those whose destination
 |        is anything else are EBCDIC.
 | V2.4 - 31/7/90 - Save more than 2 NJH fragments. The third and more fragments
 |        are saved in dynamically allocated memory as these cases are very rare.
 | V2.5 - 14/10/90 - Add multi-stream support.
 | V2.6 - 18/11/90 - Consult the character set requested for each line. This
 |        allows up to keep incoming local mail in EBCDIC.
 | V2.7 - 19/2/91 - Ignore DSH when it arrives in the middle of a file.
 | V2.8 - 14/6/91 - Change some Logger(1) to Logger(2) to make the program less verbose.
 | V2.9 - 26/12/91 - Do not clear file's flags in Parse_headers(). They are
 |        cleared (or set to some value) in OPen_Recv_file().
 | V3.0 - 18/2/92 - Don't hangup the line on double NJT but pass them to the
 |        remote node (or ignore them if the file is for the local node).
 */
#include "consts.h"
#include "headers.h"

EXTERNAL struct	LINE	IoLines[MAX_LINES];
EXTERNAL struct	COMPLETE_FILE	CompleteFile;
EXTERNAL struct	REJECT_FILE	RejectFile;
EXTERNAL struct	EOF_BLOCK	EOFblock;

char	*strchr(), *strrchr(), *malloc();

#define	MACHINE_CC	1		/* For Netdata parsing routine */

#define	CALCULATE_NEXT_CC { \
	switch(c) {	/* Calculate the next one */ \
	case 0x1:	/* Write with no space */ \
	case 0x3: \
		temp->CarriageControl[DecimalStreamNumber] = E_PLUS; break; \
	case 0x9:	/* Write and 1 space */ \
	case 0xb: \
		temp->CarriageControl[DecimalStreamNumber] = E_SP; break; \
	case 0x11:	/* Double space */ \
	case 0x13: \
		temp->CarriageControl[DecimalStreamNumber] = E_0; break; \
	case 0x19:	/* Triple space */ \
	case 0x1b: \
		temp->CarriageControl[DecimalStreamNumber] = E_MINUS; break; \
	case 0x8b:	/* Jump to next page */ \
	case 0x8d: \
		temp->CarriageControl[DecimalStreamNumber] = E_1; break; \
	default:	/* Use single space */ \
		temp->CarriageControl[DecimalStreamNumber] = E_SP; break; \
	} \
}


/*
 | Parse one record. This routine returns either 0 or 1. 0 is returned when
 | the main logic should not send ACK, since we sent here some record. 1 is
 | returned when we received something which should be acked normally.
 | This routine calls the net-data routines if the file is in NETDATA format.
 */
receive_file(Index, buffer, BufferSize)
int	Index, BufferSize;
unsigned char	*buffer;
{
	unsigned char	OutputLine[LINESIZE], MessageSender[20];
	char		*LinkName;
	char		*p, *rename_file();
	short		*StreamState;	/* The state of the stream we handle now */
	register int	i, FileSize;
	int	parse_headers(), write_record();
	struct	FILE_PARAMS	*FileParams;
#ifdef UNIX
	char	*inform_mailer();
#endif
	int	DecimalStreamNumber;	/* Stream number in the range 0-7 */

/* Buffer points to the stream number. */
	DecimalStreamNumber = ((*buffer & 0xf0) >> 4) - 9;

/* Check that the stream number is in range */
	if((DecimalStreamNumber < 0) ||
	   (DecimalStreamNumber >= IoLines[Index].MaxStreams)) {
		logger(1, "RECV_FILE: Found illegal RCB=x^%x (line=%s)\n",
			(int)(*buffer), IoLines[Index].HostName);
		file_sender_abort(Index, DecimalStreamNumber);
		return 0;	/* Abort-file already sent some record */
	}

/* Test whether we got an abort file (SCB = 0x40). If so, the Uncompress_SCB
   routine returns -1. The code that calls us adds 2 to the value returned from
   Uncompress_SCB, so if we get a record length < 2, then we know this is abort.
*/
	if(BufferSize < 2) {
		file_sender_abort(Index, DecimalStreamNumber);
		return 0;	/* Abort-file already sent some record */
	}

/* Get the relevant data from the I/O structure */
	StreamState = &((IoLines[Index]).InStreamState[DecimalStreamNumber]);

/* Test whether it is EOF block. If so - it signals end of transmission */
	if(BufferSize == 2)	/* This is the empty block */
		return finish_file(Index, DecimalStreamNumber, StreamState);

/* This was a non-EOF record. Check the SRCB and act appropriately */
	switch((buffer[1]) & 0xf0) {
/* The headers */
	case 0xc0:		/* Job header */
	case 0xd0:		/* Job Trailer */
	case 0xe0:		/* Data set header */
		IoLines[Index].CarriageControl[DecimalStreamNumber] = E_SP;	/* Init it to normal spacing */
		i = parse_headers(buffer, BufferSize, StreamState, Index, DecimalStreamNumber);
		if(i < 0)	/* Fatal error in headers */
			return 0;	/* Channel was restarted */
		else
			return 1;
/* The records themselves */
	case 0x80:
	case 0x90:
	case 0xa0:
	case 0xb0:
		if((*StreamState == S_NDH_SENT) || (*StreamState == S_NJH_SENT))
			/* We got either both or just NJH. The latter case happens
			   in SYSIN files. */
			*StreamState = S_SENDING_FILE;
		if(*StreamState != S_SENDING_FILE) {
			logger(1, "RECV_FILE: line=%s, Illegal state=%d while receiving record\n",
				IoLines[Index].HostName, (int)(*StreamState));
			restart_channel(Index);
			return 0;
		}
		i = write_record(buffer, BufferSize, Index, DecimalStreamNumber);
		if(i < 0)
			return 0;	/* File abort */
		else
			return 1;	/* OK, ack it. */
	default:
		logger(1, "RECV_FILE: Line=%s, Illegal SRCB=x^%x\n", IoLines[Index].HostName,
				(int)(*buffer));
		restart_channel(Index);
		return 0;	/* No explicit ACK */
	}
}


/*
 | End of file received. Queue file to correct disposition and inform mailer
 | if needed. Also inform sender if no Quiet form.
 | Called from Receive_file().
 */
finish_file(Index, DecimalStreamNumber, StreamState)
int	Index, DecimalStreamNumber;
short	*StreamState;	/* The state of the stream we handle now */
{
	unsigned char	OutputLine[LINESIZE], MessageSender[20];
	char		*LinkName;
	char		*p, *rename_file();
	register int	i, FileSize;
	struct	FILE_PARAMS	*FileParams;

	if(*StreamState != S_NJT_SENT) {	/* Something is wrong */
		logger(1, "RECV_FILE, line=%s, EOF received when in state %d\n",
			IoLines[Index].HostName, (int)(*StreamState));
		abort_file(Index, DecimalStreamNumber);
		return 0;
	}

/* All is ok - ack completion, and rename file to reflect its disposition */
	CompleteFile.SRCB = (((DecimalStreamNumber + 9) << 4) | 0x9);
	send_data(Index, &CompleteFile,
		(int)(sizeof(struct COMPLETE_FILE)),
		(int)(ADD_BCB_CRC));
	*StreamState = S_INACTIVE;	/* Xfer complete - Stream is idle */
	/* Because we'll get another EOF or ACK soon... */
	FileSize = close_file(Index, (int)(F_OUTPUT_FILE), DecimalStreamNumber);
	p = rename_file(Index, (int)(RN_NORMAL), (int)(F_OUTPUT_FILE),
		DecimalStreamNumber);
/* If the file link is LOCAL, then it is for our mailer; so, wakeup the mailer
   to process it. If the link type is other, queue it for that link.
   Also send back NMR telling that we've received this file.
*/
	FileParams = &(IoLines[Index].InFileParams[DecimalStreamNumber]);
	LinkName = FileParams->line;
	if(compare(LinkName, "LOCAL") == 0) {
#ifdef VMS
		inform_mailer(p, FileParams->FileName,
			FileParams->FileExt, FileParams->From,
			FileParams->To, FileParams->JobClass);
		sprintf(OutputLine,
			"Received Job %s for %s, Queued to local mailer",
			FileParams->JobName,
			FileParams->To);
#else
/* On Unix, there is no mailer. Hence, the Inform-Mailer procedure sends the
   NMR message to originator */
		inform_mailer(p, FileParams->FileName,
			FileParams->FileExt, FileParams->From,
			FileParams->To, FileParams->JobClass);
		*OutputLine = '\0';
#endif
	}
	else {	/* Queue to link */
		queue_file(p, FileSize);
		sprintf(OutputLine,
			"Job %s received, rerouted on link %s, for %s",
			FileParams->JobName, LinkName,
			FileParams->To);
	}
	sprintf(MessageSender, "@%s", LOCAL_NAME);
/* Send message back only if not found the QUIET option. */
	if(((FileParams->type & F_NOQUIET) != 0) &&
	   (*OutputLine != '\0'))
		send_nmr(MessageSender,
			FileParams->From,
			OutputLine, strlen(OutputLine),
			(int)(ASCII), (int)(CMD_MSG));
	return 0;	/* We finished here. No need to send ACK */
}


/*
 | Parse the various headers and trailers used by NJE. Currently only stores
 | them in structures.
 | If there is an error in the headers the channel is restarted and the function
 | returns -1. In a normal case it returns zero.
 | the part that parses the NDH should be modified to do it better.
 */
int
parse_headers(buffer, BufferSize, StreamState, Index, DecimalStreamNumber)
unsigned char	*buffer;
int		Index, DecimalStreamNumber, BufferSize;
short		*StreamState;
{
	register int	i, TempVar;
	unsigned char	*p, line[LINESIZE], TempLine[LINESIZE],
			Afield[20],	/* USed when translating fields to ASCII */
			format[20],	/* Message's format (Character set) */
			CharacterSet[20],	/* Character set requested for this line */
			*TempP, *TempQ;
	struct	JOB_HEADER	*LocalJobHeader;
	struct	DATASET_HEADER	*LocalDatasetHeader;
	struct	FILE_PARAMS	*FileParams;
	struct	LINE	*temp;
	int			uwrite();
	short	Ishort;

	temp = &IoLines[Index];
/* Create some equivalences */
	LocalJobHeader = (struct JOB_HEADER *)(&(temp->SavedJobHeader)[DecimalStreamNumber][1]);
	LocalDatasetHeader =
		(struct DATASET_HEADER *)(&(temp->SavedDatasetHeader)[DecimalStreamNumber][1]);
	FileParams = &((temp->InFileParams)[DecimalStreamNumber]);

	switch(buffer[1]) {	/* Dispatch according to SRCB */
	case 0xc0:	/* Job Header */
		if(*StreamState == S_NJH_SENT) {	/* Second part of segmented header */
			if(temp->SizeSavedJobHeader2[DecimalStreamNumber] != 0) {
/* More than two fragments - allocate memory for them. We use dynamic memory here
   as more than two fragments are very rare */
				for(i = 0; i < MAX_NJH_HEADERS; i++)
					if(temp->SizeSavedJobHeaderMore[DecimalStreamNumber][i] == 0)
						break;	/* Empty slot found */
				if(i == MAX_NJH_HEADERS) {
					logger((int)(1), "RECV_FILE, line=%s, No room for fragmented job header\n",
						IoLines[Index].HostName);
				} else {
					p = temp->SavedJobHeaderMore[DecimalStreamNumber][i] = (unsigned char *)
						(malloc(BufferSize));
					if(p == NULL) 
						bug_check("Can't Malloc() memory for NJH");
					temp->SizeSavedJobHeaderMore[DecimalStreamNumber][i]
						= BufferSize - 1;
					memcpy(p, &buffer[1],
						temp->SizeSavedJobHeaderMore[DecimalStreamNumber][i]);
				}
			} else {	/* Save the second fragment */
				temp->SizeSavedJobHeader2[DecimalStreamNumber] = BufferSize - 1;
				p = temp->SavedJobHeader2[DecimalStreamNumber];
				memcpy(p, &buffer[1],
					temp->SizeSavedJobHeader2[DecimalStreamNumber]);
			}
			return 1;
		}
		if(*StreamState != S_REQUEST_SENT) {
			logger((int)(1),
				"RECV_FILE: Line=%s, Job header arrived when in state %d\n",
					IoLines[Index].HostName, *StreamState);
				restart_channel(Index); return -1;
		}
		temp->SizeSavedJobHeader[DecimalStreamNumber] = BufferSize - 1; /* -1 for RCB that we don't save */
		temp->SizeSavedJobHeader2[DecimalStreamNumber] = 0;	/* Signla that it is empty */
		for(i = 0; i < MAX_NJH_HEADERS; i++)
			temp->SizeSavedJobHeaderMore[DecimalStreamNumber][i] = 0;	/* These are also empty */
		p = temp->SavedJobHeader[DecimalStreamNumber];
		memcpy(p, &buffer[1], temp->SizeSavedJobHeader[DecimalStreamNumber]);
		*StreamState = S_NJH_SENT;
		break;
	case 0xe0:	/* Dataset header */
		if(*StreamState == S_NDH_SENT) {	/* Second part of segmented header */
			if((temp->InFileParams[DecimalStreamNumber].flags & 
			    FF_LOCAL) == 0) {
				/* Write it to output file */
				uwrite(Index, DecimalStreamNumber, &buffer[1], BufferSize - 1);
			}
			return 1;
		}
		if(*StreamState == S_SENDING_FILE) {	/* JES2 sends DSH in the middle */
			logger(1, "RECV_FILE, line=%s, Ignoring DSH in the middle of a file.\n",
				IoLines[Index].HostName);
			return 1;	/* Simply ignore it */
		}
		if(*StreamState != S_NJH_SENT) {
			logger((int)(1),
				"RECV_FILE: Line=%s, Dataset header arrived when in state %d\n",
					IoLines[Index].HostName, *StreamState);
				restart_channel(Index); return -1;
		}

		temp->SizeSavedDatasetHeader[DecimalStreamNumber] = BufferSize - 1;	/* -1 for RCB */
		p = temp->SavedDatasetHeader[DecimalStreamNumber];
		memcpy(p, &buffer[1], temp->SizeSavedDatasetHeader[DecimalStreamNumber]);
		*StreamState = S_NDH_SENT;
		break;
	case 0xd0:	/* Job trailer */
		if(*StreamState != S_SENDING_FILE) {
			logger((int)(1),
				"RECV_FILE: Line=%s, Job trailer arrived when in state %d\n",
					IoLines[Index].HostName, *StreamState);
		}
/* Some nodes may send multiple NJT, so pass them on if we are S&F. */
		*StreamState = S_NJT_SENT;
		/* We don't need NJT, so write it only if EBCDIC file */
		if((temp->InFileParams[DecimalStreamNumber].flags & FF_LOCAL) ==0)
			if(uwrite(Index, DecimalStreamNumber, &buffer[1], BufferSize - 1) == 0)
				abort_file(Index, DecimalStreamNumber);
		break;
	}

/* If we got both NJH+DSH, then we can write the mailer's header */
	if(*StreamState == S_NDH_SENT) {
		FileParams->type = 0;
		/* Convert the from address */
		EBCDIC_TO_ASCII(LocalJobHeader->NJHGORGR, Afield, (int)(8));
			/* For Username */
		i = 8;
		while((i > 0) && (Afield[--i] == ' ')); i++;
			/* Remove the trailing spaces */
		Afield[i++] = '@';
		p = &Afield[i];  i += 8;
		EBCDIC_TO_ASCII(LocalJobHeader->NJHGORGN, p, (int)(8));
		while((i > 0) && (Afield[--i] == ' ')); i++;
		Afield[i] = '\0';
		strcpy(FileParams->From, Afield);
		sprintf(line, "FRM: %s", Afield);
		uwrite(Index, DecimalStreamNumber, line, strlen(line));
		EBCDIC_TO_ASCII((LocalDatasetHeader->NDH).NDHGRMT, Afield, 8);
			/* To Username */
		i = 8;
		while((i > 0) && (Afield[--i] == ' ')); i++;
		Afield[i++] = '@';
		p = &Afield[i]; i += 8;
		EBCDIC_TO_ASCII((LocalDatasetHeader->NDH).NDHGNODE, p, 8);
		while((i > 0) && (Afield[--i] == ' ')); i++;
		Afield[i] = '\0';
		strcpy(FileParams->To, Afield);
		sprintf(line, "TOA: %s", Afield);
		uwrite(Index, DecimalStreamNumber, line, strlen(line));
		EBCDIC_TO_ASCII((LocalDatasetHeader->NDH).NDHGPROC, Afield, 8);
			/* Filename */
		i = 8;
		while((i > 0) && (Afield[--i] == ' ')); i++;
		Afield[i] = '\0';
		strcpy(FileParams->FileName, Afield);
		sprintf(line, "FNM: %s", Afield);
		uwrite(Index, DecimalStreamNumber, line, strlen(line));
		EBCDIC_TO_ASCII((LocalDatasetHeader->NDH).NDHGSTEP, Afield, 8);
			/* Filename extension */
		i = 8;
		while((i > 0) && (Afield[--i] == ' ')); i++;
		Afield[i] = '\0';
		strcpy(FileParams->FileExt, Afield);
		sprintf(line, "EXT: %s", Afield);
		uwrite(Index, DecimalStreamNumber, line, strlen(line));
		if((LocalDatasetHeader->NDH).NDHGCLAS == E_M) {
			sprintf(line, "TYP: MAIL");
			FileParams->type = F_MAIL;
		}
		else {
/* Test whether it is PRINT file */
			if((((LocalDatasetHeader->NDH).NDHGFLG2) & 0x80) != 0) {
			sprintf(line, "TYP: PRINT");
			FileParams->type = F_FILE | F_PRINT;
			}
			else {
				sprintf(line, "TYP: FILE");
				FileParams->type = F_FILE;
			}
		}
		uwrite(Index, DecimalStreamNumber, line, strlen(line));
		FileParams->JobClass =
			EBCDIC_ASCII[(LocalDatasetHeader->NDH).NDHGCLAS];
		sprintf(line, "CLS: %c", FileParams->JobClass);
		uwrite(Index, DecimalStreamNumber, line, strlen(line));
/* Check for the QUIET option */
		EBCDIC_TO_ASCII((LocalDatasetHeader->NDH).NDHGFORM, Afield, 8);
		Afield[8] = '\0';
		if(strncmp(Afield, "QUIET", 5) != 0)	/* Don't be quiet */
			FileParams->type |= F_NOQUIET;
/* Some default values: */
		FileParams->NetData = 0;	/* Assume not NetData */
		FileParams->RecordsCount = 0;
/* Get the job name */
		EBCDIC_TO_ASCII(LocalJobHeader->NJHGJNAM, Afield, 8);
		i = 8;
		while((i > 0) && (Afield[--i] == ' ')); i++;
		Afield[i] = '\0';
		strcpy(FileParams->JobName, Afield);

/* Test whether this file should be in ASCII or EBCDIC */
		EBCDIC_TO_ASCII((LocalDatasetHeader->NDH).NDHGNODE, Afield, 8);
		/* To Node */
/*		if(get_route_record(Afield, TempLine, sizeof TempLine) == 0) { */
/* We have to remove trailing blanks for Find_line_index to work properly */
		i = 8;
		while((i > 0) && (Afield[--i] == ' ')); i++;
		Afield[i] = '\0';
		switch(find_line_index(Afield, FileParams->line, CharacterSet)) {
		case NO_SUCH_NODE:	/* Pass to local mailer. */
			strcpy(format, CharacterSet);
			FileParams->flags |= FF_LOCAL;	/* So we know not to save NJE headers */
			strcpy(FileParams->line, "UNKNOWN"); /* No such site */
			FileParams->format = ASCII;
			sprintf(line, "FMT: ASCII");
			strcpy(FileParams->line, "LOCAL");	/* So mailer will catch it */
			break;
		case ROUTE_VIA_LOCAL_NODE:
			strcpy(format, CharacterSet);
			FileParams->flags |= FF_LOCAL;	/* So we know not to save NJE headers */
/* Local file. Check whether to leave code conversion to mailer or do it here */
			if(compare(CharacterSet, "EBCDIC") == 0)
				FileParams->format = EBCDIC;
			else
				FileParams->format = ASCII;
/* The file should be translated to ASCII. If it is of class N change it to BINAR */
			if(FileParams->JobClass == 'N') {
				FileParams->format = BINARY;
				sprintf(line, "FMT: BINARY");
			}
			else
				sprintf(line, "FMT: %s", format);
			uwrite(Index, DecimalStreamNumber, "VIA: NJE", 8);
			break;
		default:
			strcpy(format, "EBCDIC");	/* Default */
			FileParams->format = EBCDIC;
			sprintf(line, "FMT: EBCDIC");
		}
		uwrite(Index, DecimalStreamNumber, line, strlen(line));

/* Store the file id number */
		memcpy(&Ishort, &(LocalJobHeader->NJHGJID), sizeof(short));
		sprintf(line, "FID: %d", ntohs(Ishort));
		uwrite(Index, DecimalStreamNumber, line, strlen(line));
		sprintf(line, "END:");
		uwrite(Index, DecimalStreamNumber, line, strlen(line));
		logger(3,
			"=> Receiving file %s.%s from %s to %s, format=%d, type=%d, class=%c\n",
			FileParams->FileName, FileParams->FileExt,
			FileParams->From, FileParams->To,
			FileParams->format, FileParams->type,
			FileParams->JobClass);

/* If the format is EBCDIC, save the NJH and DSH in the file */
/* Add one to the count because of the SRCB */
		if((FileParams->format == EBCDIC) &&
		   ((FileParams->flags & FF_LOCAL) == 0)) {
			if(uwrite(Index, DecimalStreamNumber, temp->SavedJobHeader[DecimalStreamNumber],
			   temp->SizeSavedJobHeader[DecimalStreamNumber]) == 0) {
				abort_file(Index, DecimalStreamNumber); return 0;
			}
			if(temp->SizeSavedJobHeader2[DecimalStreamNumber] != 0)
				uwrite(Index, DecimalStreamNumber, temp->SavedJobHeader2[DecimalStreamNumber],
				   temp->SizeSavedJobHeader2[DecimalStreamNumber]);
/* If there were more fragments for the saved job header write them */
			for(i = 0; temp->SizeSavedJobHeaderMore[DecimalStreamNumber][i] != 0; i++) {
				if(i == MAX_NJH_HEADERS) break;
				uwrite(Index, DecimalStreamNumber, temp->SavedJobHeaderMore[DecimalStreamNumber][i],
				   temp->SizeSavedJobHeaderMore[DecimalStreamNumber][i]);
				free(temp->SavedJobHeaderMore[DecimalStreamNumber][i]);	/* Free the memory */
			}
			if(uwrite(Index, DecimalStreamNumber, temp->SavedDatasetHeader[DecimalStreamNumber],
				temp->SizeSavedDatasetHeader[DecimalStreamNumber]) == 0) {
					abort_file(Index, DecimalStreamNumber); return 0;
			}
		}
/* If this is ASCII file we still have to clear the memory used by fragmented
   job headers */
		else {
			for(i = 0; temp->SizeSavedJobHeaderMore[DecimalStreamNumber][i] != 0; i++) {
				if(i == MAX_NJH_HEADERS) break;
				free(temp->SavedJobHeaderMore[DecimalStreamNumber][i]);	/* Free the memory */
			}
		}
	}
	return 1;	/* All ok */
}


/*
 | Write a record to output file. Return -1 if error, something else otherwise.
 | The initialization of INMR01 is ugly, but Unix can't use pre-initialization.
 | SRCB is currently ignored. We'll use it later in development...
 | If the record is from Netdata file, call the routine that handles it.
 */
int
write_record(buffer, BufferSize, Index, DecimalStreamNumber)
unsigned char	*buffer;
int		BufferSize, Index, DecimalStreamNumber;
{
	register int	i, TempVar;
	unsigned char	Aline[LINESIZE];
	int		uwrite();
	unsigned char	c, *p, inmr01[10];
	struct	LINE	*temp;

	strcpy(inmr01, "\311\325\324\331\360\361");	/* INMR01 */
	temp = &(IoLines[Index]);

/* Check whether this is NETDATA file and for us. If so, procees accordingly */
	 if(((temp->InFileParams[DecimalStreamNumber]).flags & FF_LOCAL) != 0) {
		if((temp->InFileParams[DecimalStreamNumber]).RecordsCount == 0) { /* First record */
			if(((temp->InFileParams[DecimalStreamNumber]).type & F_FILE) != 0) {
				if(strncmp(&buffer[5], inmr01, 6) == 0) {
					/* It's in NetData */
					(temp->InFileParams[DecimalStreamNumber]).NetData = 1;
				}
			}
		}
	}

/* If the length is shorter than the real length, then the trailing blanks were
   suppressed. Add them back. The 3rd byte is the record's length.
*/
	if(BufferSize <= (buffer[2] + 2))	/* Need to pad with blanks */
		for(i = BufferSize; i <= buffer[2] + 3; i++) /* +3 for RCB+SRCB+COUNT */
			buffer[i] = E_SP;
	BufferSize = buffer[2];	/* Length of line without the length field */

	if((temp->InFileParams[DecimalStreamNumber]).NetData != 0) {
		parse_net_data(Index, DecimalStreamNumber, &buffer[3], BufferSize);
		return 1;
	}

/* Not NETDATA - it is PUNCH format */
	/* If it is local write a record without the SRCB and length */
	(temp->InFileParams[DecimalStreamNumber]).RecordsCount += 1;	/* Increment records count */
	if(((temp->InFileParams[DecimalStreamNumber]).flags & FF_LOCAL) != 0) {
/* Test the carriage control format. Act accordingly */
		switch(buffer[1] & 0x30) {
		case 0x10:	/* Machine carriage control - translat to ASA */
			c = buffer[3];	/* This carriage control */
			buffer[3] = temp->CarriageControl[DecimalStreamNumber];	/* The previous one */
			if((temp->InFileParams[DecimalStreamNumber]).format != ASCII)
				buffer[3] = ASCII_EBCDIC[buffer[3]];	/* Convert back to EBCDIC */
			CALCULATE_NEXT_CC;
		case 0x0:		/* No carriage control */
		case 0x20:		/* ASA carriage control */
		case 0x30:		/* CPDS carriage control */
		/* Print the record as-is, including control character (if present) */
			p = &buffer[3];
/* Convert to ASCII only if needed */
			if((temp->InFileParams[DecimalStreamNumber]).format == ASCII) {
				EBCDIC_TO_ASCII(p, Aline, BufferSize); p = Aline;
			}
			if(uwrite(Index, DecimalStreamNumber, p, BufferSize) == 0) {	/* Write output line */
				abort_file(Index, DecimalStreamNumber); return 0;
			}
			break;
		}
	}
/* If EBCDIC, write the line including the length count and SRCB */
	else {
		if(uwrite(Index, DecimalStreamNumber, &buffer[1], BufferSize + 2) == 0) {
			abort_file(Index, DecimalStreamNumber);
			return 0;
		}
	}
	return 1;
}


/*
 | Parse NetData files. Currently, simply de-compose the records and write them
 | as they are into the output file. If the file is of class N, we do not convert
 | it to ASCII.
 */
parse_net_data(Index, DecimalStreamNumber, buffer, size)
int	Index, DecimalStreamNumber, size;
unsigned char	*buffer;
{
	int	length, i, *RecordsCount, TempVar, ClassNflag;	/* Is it class N file? */
	unsigned char	c, *p, Aline[LINESIZE],
			*TempQ, *TempP;
	int		uwrite();
	struct	LINE	*temp;

	temp = &IoLines[Index];

/* Save the class of the file. We assume class N files are binary. */
	if((temp->InFileParams[DecimalStreamNumber].JobClass) == 'N')
		ClassNflag = 1;
	else	ClassNflag = 0;

	RecordsCount = &(temp->InFileParams[DecimalStreamNumber].RecordsCount);
	if(*RecordsCount == 0)
		temp->SavedNdPosition[DecimalStreamNumber] = 0;	/* Init on first record */

/* Append to previous saved buffer */
	(*RecordsCount) += 1;	/* Increment records count */
	if((size + temp->SavedNdPosition[DecimalStreamNumber]) >
	   (sizeof(IoLines[0].SavedNdLine[DecimalStreamNumber])))
		bug_check("RECV_FILE: Netdata buffer overflow. Aborting.\n");

	p = &(temp->SavedNdLine[DecimalStreamNumber][temp->SavedNdPosition[DecimalStreamNumber]]);
	memcpy(p, buffer, size);
	temp->SavedNdPosition[DecimalStreamNumber] += size;

	i = 0;		/* Start looping over buffer */
	while(i < temp->SavedNdPosition[DecimalStreamNumber]) {
		length = (int)(temp->SavedNdLine[DecimalStreamNumber][i++] & 0xff);		/* First characters is the length */
/* Usually the last block is padded with zeros, so it makes us a NetData record
   of length 0.
*/
		if(length <= 0) {
			if(length < 0)
				logger(1,
				"RECV_FILE: Line=%s, Illegal netdata record lengt=%d\n",
					IoLines[Index].HostName, length);
			temp->SavedNdPosition[DecimalStreamNumber] = 0;
			return;
		}
		if(i + length > temp->SavedNdPosition[DecimalStreamNumber]) {
			logger((int)(4), "RECV_FILE: NetData size after reduction=%d, length=%d\n",
				temp->SavedNdPosition[DecimalStreamNumber], length);
			logger((int)(4), "First partial record chars=x^%x %x %x %x %x %x\n",
				temp->SavedNdLine[DecimalStreamNumber][i - 1], temp->SavedNdLine[DecimalStreamNumber][i],
				temp->SavedNdLine[DecimalStreamNumber][i + 1], temp->SavedNdLine[DecimalStreamNumber][i + 2],
				temp->SavedNdLine[DecimalStreamNumber][i + 3], temp->SavedNdLine[DecimalStreamNumber][i + 4]);
			i--;
			/* Re-align buffer: */
			p = &(temp->SavedNdLine[DecimalStreamNumber][i]);
			memcpy(temp->SavedNdLine[DecimalStreamNumber], p, (temp->SavedNdPosition[DecimalStreamNumber] - i));
			temp->SavedNdPosition[DecimalStreamNumber] -= i;
			return;		/* Save it for next buffer */
		}
		length -= 2;			/* Length+Flags are counted in it */
		logger((int)(4), "RECV_FILE: NetData length=%d\n",
			length);
		logger((int)(4), "First full record chars=x^%x %x %x %x %x %x\n",
			temp->SavedNdLine[DecimalStreamNumber][i - 1], temp->SavedNdLine[DecimalStreamNumber][i],
			temp->SavedNdLine[DecimalStreamNumber][i + 1], temp->SavedNdLine[DecimalStreamNumber][i + 2],
			temp->SavedNdLine[DecimalStreamNumber][i + 3], temp->SavedNdLine[DecimalStreamNumber][i + 4]);

/* Check for special record - "Count record" - ignore it */
		if(temp->SavedNdLine[DecimalStreamNumber][i] == 0xf0) {
			/* previous char was a record number. This control record
			   is only 2 bytes long (Count + control) */
			length = 0;	/* The 2 bytes were counted already */
			i++;	/* Point to next record */
		} else
/* If the 20 bit is on, this is a control record. Normal record: */
		if((temp->SavedNdLine[DecimalStreamNumber][i] & 0x20) == 0) {
			p = &(temp->SavedNdLine[DecimalStreamNumber][++i]);
/* If there is a carriage control character here, handle it: */
			if(temp->NDcc[DecimalStreamNumber] == MACHINE_CC) {
/* Convert machine carriage control to ASA */
				c = *p;		/* Save previous carriage control */
				*p = temp->CarriageControl[DecimalStreamNumber];
				CALCULATE_NEXT_CC;
			}
/* If the file is of class N, do not convert to ASCII */
			if(temp->InFileParams[DecimalStreamNumber].format == ASCII) {
				EBCDIC_TO_ASCII(p, Aline, length); p = Aline;
			}
			if(uwrite(Index, DecimalStreamNumber, p, length) == 0) {	/* Write output line */
				abort_file(Index, DecimalStreamNumber);
				temp->SavedNdPosition[DecimalStreamNumber] = 0;
				return;
			}
		}
		else {		/* Control record */
			parse_net_data_control_record(Index, DecimalStreamNumber,
				&(temp->SavedNdLine[DecimalStreamNumber][++i]),
				length);
		}
		i += length;		/* Increment pointer */
	}
	temp->SavedNdPosition[DecimalStreamNumber] = 0;
}


/*
 | Try pasrsing the net-data control records. Ignore all non relevant records.
 | Currently, only write the information gathered from control records and don't
 | do much more than that.
 */
#define INMFNODE	0x1011
#define	INMFUID		0x1012
#define	INMTNODE	0x1001
#define	INMTUID		0x1002
#define	INMDSORG	0x3c
#define	INMLRECL	0x42
#define	INMRECFM	0x49
#define	INMSIZE		0x102c
#define	INMNUMF		0x102f
/* File organization: */
#define	VSAM		0x8
#define	PARTITIONED	0x200
#define	SEQUENTIAL	0x4000

parse_net_data_control_record(Index, DecimalStreamNumber, buffer, length)
int	Index, DecimalStreamNumber, length;
unsigned char	*buffer;
{
	register unsigned char	*p;
	register int	size, i, TempVar;
	int		key;		/* Key of values */
	unsigned char	value[LINESIZE];	/* Value of text unit */
	char		Aline[LINESIZE];	/* For ASCII values */
	unsigned char	inmr[10];

	strcpy(inmr, "\311\325\324\331");	/* INMR */
	if(strncmp(buffer, inmr, (int)(4)) != 0) {
		logger((int)(1), "RECV_FILE: Line=%s, Illegal ND control record=x^%x,%x,%x,%x\n",
			IoLines[Index].HostName,
			buffer[-2], buffer[-1], buffer[0], buffer[1]);
		return;
	}
/* We know that the first 4 characters are INMR. Now find which one */
	p = &buffer[5];	/* [4] = 0, [5] = second number */
	length -= 6;
	switch(*p++) {
	case 0xf2:	/* 02 */
		p += 4; length -= 4;	/* INMR02 has 4 byte file number before
					   text units. Jump over it */
	case 0xf1:	/* 01 */
	case 0xf3:	/* INMR03 */
	case 0xf4:	/* User specified */
	case 0xf6:	/* Last INMR - ignore */
		while(length > 0) {
			if((size = get_text_unit(p, length, &key, value)) <= 0)
				return;
			switch(key) {
			case INMDSORG:
			case INMRECFM:
			case INMLRECL:
			case INMSIZE:
			case INMNUMF:
				switch(size) {
				case 1: i = value[0]; break;
				case 2: i = (value[0] << 8) + value[1];
					break;
				case 4: i = (value[0] << 24) +
					     (value[1] << 16) +
					     (value[2] << 8) + value[3];
					break;
				default: break;
				}
				logger(4,
					"RECV_FILE: INMR: key=x^%x, value=x^%x\n",
					key, i);
				switch(key) {
				case INMDSORG: if(i != SEQUENTIAL)
					logger(4,
					  "RECV_FILE: Line=%s, Netdata file organization=x^%x not supported\n",
						IoLines[Index].HostName, i); break;
				case INMRECFM:
					IoLines[Index].NDcc[DecimalStreamNumber] = 0;	/* Default */
					switch(i) {
						/* Have to convert MAchine to ASA: */
					case 0x200: IoLines[Index].NDcc[DecimalStreamNumber] = MACHINE_CC;
					case 0x1:
					case 0x2:
					case 0x4000:
					case 0x8000:	/* We can handle all of these */
						break;
					default:	/* Can't handle them */
						logger(2,
					  "RECV_FILE: Line=%s, Netdata record format=x^%x not supported\n",
							IoLines[Index].HostName, i);
					}
					break;
				case INMLRECL: if(i >= (LINESIZE * 2))
					logger(2,
					"RECV_FILE: Line=%s, Netdata record length=%d too long\n",
						IoLines[Index].HostName, i); break;
				}
				break;
			default:
				EBCDIC_TO_ASCII(value, Aline, size);
				Aline[size] = '\0';
				logger(4,
					"RECV_FILE: INMR: key=x^%x, value='%s'\n",
					key, Aline);
				break;
			}
			size += 6;	/* 6 for key+count+length */
			length -= size; p += size;
		}
		break;
	default: break;
	}
}


/*
 | Retreive one text unit from the buffer passed.
 */
get_text_unit(buffer, length, key, value)
unsigned char	*buffer, *value;
int	*key, length;
{
	register int	j, i, count, size, position, TempVar;
	unsigned char	*TempP, *TempQ;

	*key = ((buffer[0] << 8) + buffer[1]);
	count = ((buffer[2] << 8) + buffer[3]);
	size = 0; position = 4;

/* Loop the count numbered. Separate values by spaces */
	for(j = 0; j < count; j++) {
		i = ((buffer[position] << 8) + buffer[position + 1]);
		if((i > (length - 6)) || (i <= 0)) {
			logger(2,
				"RECV_FILE, get-text-unit (key=%x), field-length=%d>length=%d\n",
				*key, i, length);
			return -1;
		}
		position += 2;
		memcpy(&value[size], &buffer[position], i);
		size += i; position += i;
/* Add 2 spaces. This will count also for the 2 bytes length field... */
		value[size++] = E_SP; value[size++] = E_SP;
	}
	size -= 2;	/* For the last 2 spaces */
	value[size] = '\0';
	return size;
}


/*
 | Abort the receiving file because of sender's abort. Send EOF as reply,
 | close the output file, put it on hold-Abort for later inspection, and
 | reset the stream's state.
 */
file_sender_abort(Index, DecimalStreamNumber)
int	Index;
{
	char		*rename_file();

/* Send EOF block to confirm this abort */
	EOFblock.SRCB = (((DecimalStreamNumber + 9) << 4) | 0x9);
	send_data(Index, &EOFblock, (int)(sizeof(struct EOF_BLOCK)),
		(int)(ADD_BCB_CRC));

/* Signal that the file was closed and hold it */
	close_file(Index, (int)(F_OUTPUT_FILE), DecimalStreamNumber);
	rename_file(Index, (int)(RN_HOLD_ABORT), (int)(F_OUTPUT_FILE),
		DecimalStreamNumber);
	IoLines[Index].InStreamState[DecimalStreamNumber] = S_INACTIVE;
}


/*
 | Abort the receiving file because we have a problem.
 | close the output file, put it on hold-Abort for later inspection, and
 | reset the stream's state.
 */
abort_file(Index, DecimalStreamNumber)
int	Index, DecimalStreamNumber;
{
	char		*rename_file();

/* Send Reject-file to abort file */
	RejectFile.SRCB = (((DecimalStreamNumber + 9) << 4) | 0x9);
	send_data(Index, &RejectFile, (int)(sizeof(struct REJECT_FILE)),
		(int)(ADD_BCB_CRC));

/* Signal that the file was closed and hold it */
	close_file(Index, (int)(F_OUTPUT_FILE), DecimalStreamNumber);
	rename_file(Index, (int)(RN_HOLD_ABORT), (int)(F_OUTPUT_FILE), DecimalStreamNumber);
	IoLines[Index].InStreamState[DecimalStreamNumber] = S_REFUSED;	/* To stop this stream */
}
