/* MX_TO_NJE.C		X0.0
 | This program is run by the SITE_DELIVER.COM and queues mail messages to
 | the NJE package.
 | Currently this program only transfer the addresses as they are.
 |
 | Parameters:  P1 - Filename of message.
 |              P2 - Filename of addresses to send to.
 |              P3 - Address of sender.
 |              P4 - Local BITnet name (optional).
 |              P5 - Local InterNet name (must be if P4 is defined).
 | If P4+P5 appear, in the "From" address P5 is looked for and replaced with P4.
 | It is adviseable to state P4 and P5 with the leading @-sign.
 |
 | The linkname used is DEFAULT which should be either defined in the routing
 | table of NJE or NJE should have a default route.
 |
 | Usage: Create a procedure named MX_EXE:SITE_DELIVER.COM and contains the
 | following lines:
 |
 | $! SITE_DELIVER.COM
 | $ MX_TO_NJE :== $BITNET_ROOT:[EXE]MX_TO_NJE
 | $ MX_TO_NJE 'P2' 'P3' "''P4'" "@KINERET.HUJI.AC.IL" "@KINERET"
 | $ EXIT 1
 |
 */
#include <stdio.h>
#include <time.h>		/* For making temporary filenames */
#include <iodef.h>		/* I/O codes for mailbox communication */

#define	LINESIZE	512

/* VMS string descriptor */
struct DESC {
	short	length, type;
	char	*address;
};

main(cc, vv)
char	*vv[];
int	cc;
{
	FILE	*Ifd,		/* Input message file */
		*Afd,		/* Addresses file */
		*Ofd;		/* Output file in the queue */
	char	*p, ToAddress[LINESIZE],	/* Where to send to */
		FromAddress[LINESIZE],		/* Sender's name */
		line[LINESIZE],			/* To copy message */
		OutputFilename[LINESIZE],	/* Queue filename */
		*make_temp_filename();
	int	FileSize;		/* File size in blocks */

	if(cc < 4)	/* Less than 3 parameters */
		exit(44);	/* Simply abort */

/* Open the addresses file */
	if((Afd = fopen(vv[2], "r")) == NULL) {
		perror(vv[2]); exit(44);
	}

/* Generate a sender name in the format we know */
	if(*vv[3] == '<')
		strcpy(FromAddress, &vv[3][1]);	/* Strip the < */
	else	strcpy(FromAddress, vv[3]);
	if((p = strchr(FromAddress, '>')) != NULL) *p = '\0';	/* Strip the > */

/* For each address generate a separate file */
	while(fgets(ToAddress, sizeof(ToAddress), Afd) != NULL) {
		if(*ToAddress == '<') *ToAddress = ' ';	/* This space won't make us problem */
		if((p = strchr(ToAddress, '>')) != NULL) *p = '\0';
/* Remove .BITNET if found there: */
		if((p = strstr(ToAddress, ".BITNET")) != NULL) *p = '\0';
		if((p = strstr(ToAddress, ".bitnet")) != NULL) *p = '\0';

/* OPen the input file */
		if((Ifd = fopen(vv[1], "r")) == NULL) {
			perror(vv[1]); exit(44);
		}
/* OPen a file in the BITnet queue, write our header and copy the message */
		sprintf(OutputFilename, "BITNET_ROOT:[QUEUE]ASC_*.DEFAULT");
		make_temp_filename(OutputFilename);
		if((Ofd = fopen(OutputFilename, "w")) == NULL) {
			perror("OutputFilename"); exit(44);
		}

/* Write our header */
		if(cc > 4)		/* Have to rewrite the address */
			rewrite_from(FromAddress, vv[4], vv[5]);
		fprintf(Ofd, "FRM: %s\n", FromAddress);
		fprintf(Ofd, "TOA: %s\n", ToAddress);
		fprintf(Ofd, "TYP: MAIL\n");
		fprintf(Ofd, "FMT: ASCII\n");
		fprintf(Ofd, "END:\n");	/* End of header */
		while(fgets(line, sizeof(line), Ifd) != NULL)
			fprintf(Ofd, "%s", line);
/* Get the size of the file in blocks */
		FileSize = (ftell(Ofd) / 512) + 1;
		fclose(Ifd);
		fclose(Ofd);
		queue_HUJI_NJE_file(OutputFilename, FileSize);
	}
	fclose(Afd);
	exit(1);
}


/*
 | Replace the InterNet name with a BITnet one. WE assume that the address
 | is in the format User@Internet-name, thus do not try to look after the
 | end of the InterNet name for other parts.
 |  The BITnet and InterNet names are converted to upper case to make the
 | comparison easier.
 */
rewrite_from(FromAddress, InternetName, BITnetName)
char	*FromAddress,		/* From as passed to us from MX */
	*InternetName,		/* Internet name of this machine */
	*BITnetName;		/* The BITnet name of this machine */
{
	char	*p;

/* First, get the size of names */
	if((strlen(InternetName) == 0) || (strlen(BITnetName) == 0))
		return;		/* Some configuration error - ignore it */

/* Now, make them all upper case to let STRSTR() make its work */
	for(p = FromAddress; *p != '\0'; *p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';
	for(p = InternetName; *p != '\0'; *p++)
		if((*p >= 'a') && (*p <= 'z')) *p -= ' ';

/* Now look for it */
	if((p = strstr(FromAddress, InternetName)) == NULL)
		return;		/* Not found at all - nothing to do */

/* Copy the BITnet name over the InterNet one */
	strcpy(p, BITnetName);
}


/* ===== These routines were copied from the mailer ones. We copied ====== */
/* ===== since MX comes instead of our mail.                        ====== */

/*
 | Create a temporary filename. Return the address of the created filename and
 | also place it over the original. It is so, because mktemp() works the same
 | way.
 |  The template should contain one asterisk in the place where a random number
 | should be placed.
 */
char *
make_temp_filename(FileName)
char	*FileName;
{

	struct tm *tm, *localtime();
	long	clock;
	char	*p, *q, TempLine[LINESIZE], tmp[LINESIZE];

/* If there is no * in the filename, try calling mktemp() as last resort */
	if((p = (char *)strchr(FileName, '*')) == NULL)
		return (char *)mktemp(FileName);

/* Get the current time. */
	time(&clock);
	tm = localtime(&clock);

/* Found - make a place for %d instead of the * */
	strcpy(TempLine, FileName);
	q = &TempLine[strlen(TempLine)];
	p = q;
	*++q = '\0';	/* New string delimiter */
	while(*p != '*')
		*--q = *--p;
	*p = '%'; *q = 's';
	sprintf(tmp, "%d%d%d_%d%d", tm->tm_mday, tm->tm_mon+1, tm->tm_year,
		tm->tm_min, tm->tm_sec);
	sprintf(FileName, TempLine, tmp);
	return FileName;
}


/* The following is for HUJI-NJE to queue files to */
#define	NJE_MAILBOX_NAME	"HUJI_NJE_CMD"
#define	CMD_QUEUE_FILE	3
/*
 | Connect to the mailbox of the NJE emulator and queue the file to it.
 */
queue_HUJI_NJE_file(file_name, FileSize)
char	*file_name;
int	FileSize;
{
	char	line[LINESIZE];
	struct	DESC	MailBoxDesc;
	int	status, size;
	short	chan, iosb[4];

	*line = CMD_QUEUE_FILE;
	line[1] = (unsigned char)((FileSize & 0xff00) >> 8);
	line[2] = (unsigned char)(FileSize & 0xff);
	strcpy(&line[3], file_name);
	size = strlen(&line[3]) + 3;	/* 3 for the command code and file size */

	MailBoxDesc.address = NJE_MAILBOX_NAME;
	MailBoxDesc.length = strlen(NJE_MAILBOX_NAME);
	MailBoxDesc.type = 0;
	status = sys$assign(&MailBoxDesc, &chan, (long)(0), (long)(0));
	if((status & 0x1) == 0) {
		printf("VMS, Can't assign channel to HUJI-NJE mailbox: %d\n",
			status);
		return;
	}

	status = sys$qiow((long)(0), chan,
		(short)(IO$_WRITEVBLK | IO$M_NOW), iosb,
		(long)(0), (long)(0),
		line, size, (long)(0),
		(long)(0), (long)(0), (long)(0));
	if(((status & 0x1) == 0) || ((iosb[0] & 0x1) == 0)) {
		printf("VMS, Can't write to NJE mailbox, status: %d, iosb: %d\n",
			status, iosb[0]);
	}
	sys$dassgn(chan);
}
