/* UTIL.C    V2.0
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
 | Most of the routines here should be modified to macros.
 | COMPRESS_SCB & UNCOMPRES should be modified to get a flag whether to
 |              translate to EBCDIC or not.
 | Implementation note: When an external program writes a file for us to transmit,
 | one thing should be noted in the envelope: the TYP field must come before
 | the CLS field (the later is optional).
 |
 | Various utility routines:
 | compare    - Case insensitive string comparison.
 | parse_envelope  - Parses our internal envelope in the file.
 | compress_scb    - Compress the string using SCB. In the meantime, do not
 |                   compress. Only add SCB's.
 | uncompress_scb  - Get an "SCB-compressed" line and convert it into a normal
 |                   string.
 |
 | V1.1 - Parse-Envelope - add the FLG field. Currently it contains only the
 |        NOQUIET code.
 | V1.2 - Parse-Envelope - If the link on which a message should be sent equals
 |        the link name from which it should have come, change the From to
 |        be MAILER@Local-Site. This is in order to protect the neighbour...
 | V1.3 - UNOMPRESS-SCB inserted ASCII spaces instead of EBCDIC spaces when
 |        space-compression has been found...
 | V1.4 - 29/1/90 - Recognise the value BINARY in the FMT field.
 | V1.5 - 14/2/90 - Replace > NULL with != NULL for all Unix systems compatibility.
 | V1.6 - 7/3/90 - Correction to V1.2 - Use Find_line_index instead of Get_route
 |        record().
 | V1.7 - 12/3/90 - Handle empty fields in header which contains only spaces
 |        (remove the spaces and put default contents there). The space causes
 |        some RSCS versions to refuse the file.
 | V1.8 - 28/10/90 - Remove the correction of V1.2; it was found that the
 |        problem lies somewhere else.
 | V1.9 - 11/3/91 - Uread() - add the stream number.
 | V2.0 - 23/8/92 - Add handling of the TAG field.
 */
#include "consts.h"
#include "ebcdic.h"

EXTERNAL struct	LINE	IoLines[MAX_LINES];

char	*strchr();

/*
 |  Case insensitive strings comparisons. Return 0 only if they have the same
 |  length.
*/
#define	TO_UPPER(c)	(((c >= 'a') && (c <= 'z')) ? (c - ' ') : c)
compare(a, b)
char	*a, *b;
{
	register char	*p, *q;

	p = a; q = b;

	for(; TO_UPPER(*p) == TO_UPPER(*q); p++,q++)
		if((*p == '\0') || (*q == '\0')) break;

	if((*p == '\0') && (*q == '\0'))	/* Both strings done = Equal */
		return 0;

/* Not equal */
	return(TO_UPPER(*p) - TO_UPPER(*q));
}


/* 
 | Read the file's envelope, and fill the various parameters from the
 | information stored there.
 | The CLS line must appear AFTER the TYP line, since the TYP line defines
 | the job class.
 | The FLG line must appear after the type and class, since it adds bits
 | to the type.
 | If the From address is too long we put there "MAILER@Local-Site". If the
 | To address is too long, we put there "INFO@Local-Site" so the postmaster
 | will ge it back.
 |
 | The Header format is:
 | FRM: BITnet sender address.
 | TYP: MAIL, FILE (NetData), PRINT (Print file), PUNCH (Send file as punch),
 |      FASA (NetData with ASA CC), PASA (Print with ASA CC).
 | FNM: File name
 | EXT: File name extension.
 | FMT: Character set (ASCII/ASCII8/DISPLAY/BINARY)
 | CLS: one-Character (the job class)
 | FID: Number - The job id to use (if given).
 | TOA: Address. May be replicated the necessary number of times.
 | FLG: flags. FLG_NOQUIET (defined by MAILER.H) is translated to F_NOQUIET to
 |      not put the QUIET form code.
 | TAG: The tag information.
 | END: End of envelope.
 | Data...
 */
#define	FROM	1
#define	TYPE	2
#define	FNAM	3
#define	FEXT	4
#define	FRMT	5
#define	TO	6
#define	LNG	7
#define	CLASS	8
#define	FILEID	9
#define FLG	10
#define	TAG	11
#define	ILGL	0
#define	FLG_NOQUIET	1	/* From MAILER.H */
parse_envelope(Index, DecimalStreamNumber)
int	Index, DecimalStreamNumber;
{
	int	i;
	struct	FILE_PARAMS	*FileParams;
	register char	*p;
	char	line[LINESIZE], TempLine[20], FromLine[20], ToLine[20];

	FileParams = &((IoLines[Index].OutFileParams)[DecimalStreamNumber]);
		/* This routine is called to parse only outgoing files */
	FileParams->NetData = 0;
	FileParams->RecordsCount = 0;
	FileParams->type = FileParams->FileId = 0;

/* Put inital values into variables that might not have value */
	strcpy(FileParams->From, "***@****");	/* Just init it to something */
	sprintf(FileParams->To, "INFO@%s", LOCAL_NAME);	/* In case there is empty TO */
	strcpy(FileParams->FileName, "UNKNOWN");
	strcpy(FileParams->FileExt, "DATA");
#ifdef INCLUDE_TAG
	*FileParams->tag = '\0';
#endif
	FileParams->format = ASCII;

	while((uread(Index, IoLines[Index].CurrentStream, line, LINESIZE) >= 0)
	      && (strncmp(line, "END:", 4) != 0)) {
		if((p = strchr(line, ' ')) != NULL ) *p++;	/* First char after space */
		else p = &line[5];
		/* Skip over leading spaces (and I saw null filename which was
		   2-3 spaces and causes RSCS to refuse the file... */
		while(*p == ' ') *p++;
		switch(crack_header(line)) {
		case FROM: if((strlen(p) >= 20) || (*p == '\0'))
				sprintf(FileParams->To, "MAILER@%s", LOCAL_NAME);
			   else
				strcpy(FileParams->From, p);
			   continue;
		case TYPE: if((compare(p, "MAIL") == 0) ||
			      (compare(p, "BSMTP") == 0)) {
				FileParams->type = F_MAIL;
				FileParams->JobClass = 'M';	/* M class */
			   }
			   else	{ /* It is a file - parse the format */
				FileParams->JobClass = 'A';	/* A class */
				if(compare(p, "PRINT") == 0)
					FileParams->type = F_PRINT;
				else
				if(compare(p, "PUNCH") == 0)
					FileParams->type = F_PUNCH;
				else
				if(compare(p, "PASA") == 0)
					FileParams->type = F_ASA | F_PRINT;
				else
				if(compare(p, "FASA") == 0)
					FileParams->type = F_ASA;
				else
					FileParams->type = F_FILE;	/* Default */
			   }
			   continue;
		case CLASS:		/* Check whether class is ok */
			if((*p >= 'A') && (*p <= 'Z'))	/* OK */
				FileParams->JobClass = *p;
			else	/* Don't change it */
				logger((int)(1), "UTILS, Illegal job class '%c'in CLS\n",
					*p);
			continue;
		case FNAM: if(*p != '\0') {
				p[8] = '\0';	/* Delimit length */
				strcpy(FileParams->FileName, p);
			  }
			   continue;
		case FEXT: if(*p != '\0') {
				p[8] = '\0';
				strcpy(FileParams->FileExt, p);
			   }
			   continue;
#ifdef INCLUDE_TAG
		case TAG: strcpy(FileParams->tag, p);
			  continue;
#endif
		case FRMT: if(strcmp(p, "EBCDIC") == 0)
				FileParams->format = EBCDIC;
			else
			if(strcmp(p, "BINARY") == 0)
				FileParams->format = BINARY;
			else	/* Default */
				FileParams->format = ASCII;
			continue;
		case LNG:	/* Language from PC */
			   continue;	/* Ignore it currently */
		case TO:
			if(strlen(p) >= 20)
				sprintf(FileParams->To, "INFO@%s", LOCAL_NAME);
			else
			if((*p != '\0') && (*p != ' '))
				strcpy(FileParams->To, p);
			/* Else - leave the INFO@Local_node */
			continue;
		case FILEID: sscanf(p, "%d", &i);
			if((i > 0) && (i < 9900))	/* Inside range */
				FileParams->FileId = (short)(i);
			continue;
		case FLG: sscanf(p, "%d", &i);
			if((i & FLG_NOQUIET) != 0)
				FileParams->type |= F_NOQUIET;
			continue;
		case ILGL: logger((int)(1), "UTIL, Illegal header line: '%s'\n", line);
			   continue;
		}
	}

#ifdef OBSOLETE_CODE_DO_NOT_COMPILE
/* Some mails which are automatically forwarded have the "from" in the envelope
   pointing to the original sender. This causes NJEF and other NJE's to crash
   (message comes from the incorrect line). So, if the "from" is a site that
   arrives from the link on which it should be sent, change it to be
   MAILER@Loca-Site.
*/
	/* Get the link name for the To field */
	if((p = strchr(FileParams->To, '@')) != NULL )
		strcpy(TempLine, ++p);
	else	strcpy(TempLine, FileParams->To);
	if(find_line_index(TempLine, ToLine) < 0) return;

	/* Get the link name for the From field */
	if((p = strchr(FileParams->From, '@')) != NULL )
		strcpy(TempLine, ++p);
	else	strcpy(TempLine, FileParams->From);
	if(find_line_index(TempLine, FromLine) < 0) return;

	/* Compare */
	if(compare(FromLine, ToLine) == 0) {	/* Equal */
		logger(2, "UTIL: From=%s, to=%s; Changing from to MAILER@Local-Site\n",
			FileParams->From, FileParams->To);
		sprintf(FileParams->From, "MAILER@%s", LOCAL_NAME);
	}
#endif
}

/*
 | Return the code relative to the given keyword from the file's envelope.
 */
crack_header(line)
char	*line;
{
	char	temp[LINESIZE];

	strcpy(temp, line);	temp[4] = 0;
	     if(compare(temp, "FRM:") == 0) return FROM;
	else if(compare(temp, "TYP:") == 0) return TYPE;
	else if(compare(temp, "CLS:") == 0) return CLASS;
	else if(compare(temp, "FNM:") == 0) return FNAM;
	else if(compare(temp, "EXT:") == 0) return FEXT;
	else if(compare(temp, "FMT:") == 0) return FRMT;
	else if(compare(temp, "LNG:") == 0) return LNG;
	else if(compare(temp, "TOA:") == 0) return TO;
	else if(compare(temp, "FID:") == 0) return FILEID;
	else if(compare(temp, "FLG:") == 0) return FLG;
	else if(compare(temp, "TAG:") == 0) return TAG;
	else	return ILGL;	/* No legal keyword */
}


/*
 | Add SCB's to the given string.
 */
int
compress_scb(InString, OutString, InSize)
unsigned char	*InString, *OutString;
int	InSize;
{
	register int	i, OutSize;
	register unsigned char	*p, *q;

	p = InString;  q = OutString; i = OutSize = 0;
	while(InSize > 63) {
		*q++ = 0xff;	/* SCB of 63 characters */
		for(i = 0; i < 63; i++) {
			*q++ = *p++;		/* Copy 63 characters */
		}
		OutSize += 64;	/* 63 characters + SCB character */
		InSize -= 63;
	}

/* Copy what is left */
	if(InSize > 0) {
		OutSize += (InSize + 1);	/* +1 for SCB */
		*q++ = (0xc0 + InSize);		/* SCB for the left characters */
		for(i = 0; i < InSize; i++) {
			*q++ = *p++;
		}
	}
	*q++ = 0; OutSize++;	/* Final SCB = end of string */
	return OutSize;
}


/*
 | Get a string (first char is the first SCB) and convert it into a normal
 | string. Return the resulting string size.
 */
uncompress_scb(InString, OutString, InSize, OutSize, SizeConsumed)
unsigned char	*InString, *OutString;
int		InSize, OutSize, /* Size of preallocated space for out string */
		*SizeConsumed;	/* How many characters we consumed from input string */
{
	register unsigned char	*p, *q, SCB;
	register int		i, j, ErrorFound,
SavedInSize;

	SavedInSize = InSize;
	i = j = *SizeConsumed = 0;
	p = InString; q = OutString;
	ErrorFound = 0;

	while(InSize-- > 0) {	/* Decrement by one for the SCB byte */
		*SizeConsumed += 1;	/* SCB byte */
		switch((SCB = *p++) & 0xf0) {	/* The SCB */
		case 0:		/* End of record */
			if(ErrorFound == 0)
				return j;
			else	return -1;
		case 0x40:	/* Abort */
			return -1;
		case 0x80:
		case 0x90:	/* Blanks */
			j += (SCB &= 0x1f);	/* Get the count */
			if(j > OutSize) {	/* Output too long */
				logger((int)(1),
					"UTILS: Output line in convert too long (spaces).\n");
				ErrorFound = -1;
				continue;
			}
			for(i = 0; i < SCB; i++)
				*q++ = E_SP;
			continue;
		case 0xa0:
		case 0xb0:	/* Duplicate single character */
			j += (SCB &= 0x1f);
			if(j > OutSize) {	/* Output too long */
				logger((int)(1),
					"UTILS: Output line in convert too long (Dup).\n");
				ErrorFound = -1;
				*p++; InSize--; *SizeConsumed += 1;
				continue;
			}
			for(i = 0; i < SCB; i++)
				*q++ = *p;
			*p++; InSize--; *SizeConsumed += 1;
			continue;
		case 0xc0:
		case 0xd0:
		case 0xe0:
		case 0xf0:	/* Copy characters as they are */
			j += (SCB &= 0x3f);
			if(j > OutSize) {	/* Output too long */
				logger((int)(1),
					"UTILS: Output line in convert too long.\n");
				ErrorFound = -1;
				InSize -= SCB;
				*SizeConsumed += SCB;
				p += SCB;	/* Jump over the characters */
				continue;
			}
			InSize -= SCB;
			*SizeConsumed += SCB;
			for(i = 0; i < SCB; i++) {
				*q++ = *p++;
			}
			continue;
		}
	}

/* If we are here, then there is no closing SCB. Some error... */
	logger((int)(1), "UTILS: No EOR SCB in convert.\n");
trace(InString, SavedInSize, 1);
	return -1;
}
