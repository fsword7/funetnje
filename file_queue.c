/* FILE_QUEUE.C   V1.6
 | Copyright (c) 1988,1989,1990 by
 | The Hebrew University of Jerusalem, Computation Center.
 |
 |   This software is distributed under a license from the Hebrew University
 | of Jerusalem. It may be copied only under the terms listed in the license
 | agreement. This copyright message should never be changed or removed.
 |   This software is gievn without any warranty, and the Hebrew University
 | of Jerusalem assumes no responsibility for any damage that might be caused
 | by use or misuse of this software.
 |
 | Handle the in-memory file queue. Queue and de-queue files.
 | Currently the host assignment to line (and thus the files queue) is fixed,
 | during the program run. However, the files queueing is dynamic.
 |   When the files are queued from the mailer, they are ordered in ascending
 | size order.
 |
 | V1.1 - Add a call to OS specific routine to return file's size when queueing
 |        files during initialization process; the size is in 512 bytes blocks.
 | V1.2 - Remove (int) which preceeded some constants in order to be compatible
 |        with more systems.
 | V1.3 - 6/3/90 - When queueing a file, call Find_line_index() in order to try
 |        alternate route if the link is currently down but there is an alternate route.
 |        Init_file_queue() also calls Queue_file() and not the queueing routine
 |        directly (although we know the exact line for this file) in order to
 |        take advantage of alternate routes when doing DEBUG RESCAN without
 |        the need to rename files.
 | V1.4 - Add a third parameter to the call of Find_line_index() which is needed
 |        by the called routine. We don't need this parameter.
 | V1.5 - 10/2/91 - When creating the initial queue, do not look for files
 |        ending with a linkname. Instead look for files whose name is
 |        ASC_* and EBC_*; this change is due to the new queueing algorithm.
 | V1.6 - 13/3/91 - Correct a bug introduced on V1.5 - If the file's extension
 |        is LOCAL leave it aside (it is intended for the mailer).
 */

#include "consts.h"
#ifdef UNIX
#include DIRENTFILE
#endif
#include <errno.h>

EXTERNAL struct LINE	IoLines[MAX_LINES];

char	*strchr(), *strrchr();
long	*malloc();

/*
 | Zero the queue pointers. Then go over the queue and look for files queued
 | to us. Each file found is queued according to the routing table and
 | alternate routes.
 | When working on a Unix system, we have a problem of telling the Find-File
 | routine whether this is the first call or not (on VMS the value of Context
 | tells it). Thus, on Unix, if FileMask is none-zero, this is the first call.
 | If it points to a Null byte, then this is not the first call, and the Find-file
 | routine already has the paramters in his internal variables.
 */
init_files_queue()
{
	register long	i, FileSize;
#ifdef VMS
	long	context;	/* Context for find file */
#else
	DIR	context;
#endif
	char	FileMask[LINESIZE], FileName[LINESIZE];

	for(i = 0; i < MAX_LINES; i++)
		IoLines[i].QueueStart = IoLines[i].QueueEnd = NULL;

/* Scan the queue for files to be sent */
#ifdef VMS
	sprintf(FileMask, "%sASC_*.*;*", BITNET_QUEUE);
	context = 0;
#else
	sprintf(FileMask, "%s/ASC_*", BITNET_QUEUE);
#endif
	while(find_file(FileMask, FileName, &context) == 1) {
		FileSize = get_file_size(FileName);	/* Call OS specific routine */
/* Although we know the exact line name we call Queue_File(); this is usefull
   when this routine is called via DEBUG RESCAN command and will queue the files
   to the alternate route if found.
*/
		queue_file(FileName, FileSize);
#ifdef UNIX
		*FileMask = '\0';	/* So we know this is not the first call */
#endif
	}

/* Same for files which start with EBC_* */
#ifdef VMS
	sprintf(FileMask, "%sEBC_*.*;*", BITNET_QUEUE);
	context = 0;
#else
	sprintf(FileMask, "%s/EBC_*", BITNET_QUEUE);
#endif
	while(find_file(FileMask, FileName, &context) == 1) {
		FileSize = get_file_size(FileName);	/* Call OS specific routine */
/* Although we know the exact line name we call Queue_File(); this is usefull
   when this routine is called via DEBUG RESCAN command and will queue the files
   to the alternate route if found.
*/
		queue_file(FileName, FileSize);
#ifdef UNIX
		*FileMask = '\0';	/* So we know this is not the first call */
#endif
	}
}


/*
 | Given a file name, find its line number and queue it. If the line number is
 | not defined, ignore this file.
 | We try getting an alternate route if possible. If the status returned is
 | that the link exists but inactive, we must loop again and look for it.
*/
queue_file(FileName, FileSize)
char	*FileName;
int	FileSize;	/* File size in blocks */
{
	register char	*p;
	register int	i;
	char	LineName[16],	/* Needed when calling Get_Line_index() */
		Format[16];	/* Not needed but returned by Find_line_index() */

/* remove trailing semicolons */
	if((p = strchr(FileName, ';')) != NULL) *p = '\0';

	if((p = strrchr(FileName, '.')) == NULL) {	/* No extension */
		logger(1, "FILE_QUEUE: Illegal filename '%s' to queue\n",
			FileName);
		return;
	}
	else	p++;		/* Point to extension */

	if(compare(p, "LOCAL") == 0)	/* Incoming file - leave it to mailer */
		return;

	switch((i = find_line_index(p, LineName, Format))) {
	case NO_SUCH_NODE:	/* Probably mailer's table problem */
		logger(1, "FILE_QUEUE: Can't find line # for file '%s'\n",
			FileName);
		return;
	case LINK_INACTIVE:	/* No alternate route available... */
		for(i = 0; i < MAX_LINES; i++) {
			if((IoLines[i].HostName)[0] != '\0') {	/* This line is defined */
				if(compare(IoLines[i].HostName, LineName) == 0) {
					/* Queue it */
					add_to_file_queue(FileName, i, FileSize);
					return;
				}
			}
		}
		logger(1, "FILE_QUEUE, Got LINK INACTIVE response, but can't find\
 line for %s\n", FileName);
		return;
	case ROUTE_VIA_LOCAL_NODE:	/* Some mistake! */
		logger(1, "FILE_QUEUE, The node to queue file to is not connected via NJE!\n");
		logger(1, "The requested linkname is %s\n", p);
		return;
	default:	/* Hopefully a line index */
		if((i < 0) || (i > MAX_LINES)) {
			logger(1, "FILE_QUEUE, Find_line_index() returned erronous\
 index (%d) for node %s\n", i, p);
			return;
		}
		add_to_file_queue(FileName, i, FileSize);
		break;
	}
}


/*
 | Given a filename and the line number (index into IoLines array), the filename
 | will be queued into that line. Order the list according to the file's size.
 */
add_to_file_queue(FileName, LineNumber, FileSize)
char	*FileName;
int	LineNumber, FileSize;
{
	struct	QUEUE	*temp, *keep;
	struct	LINE	*TempLine;

	TempLine = &(IoLines[LineNumber]);
/* Empty list - insert at the head */
	if(TempLine->QueueStart == NULL) {	/* Init the list */
		TempLine->QueueStart = TempLine->QueueEnd =
			temp = (struct QUEUE *)malloc(sizeof(struct QUEUE));
			if(temp == NULL) {
#ifdef VMS
				logger(1, "FILE_QUEUE, Can't malloc. Errno=%d, VmsErrno=%d\n",
					errno, vaxc$errno);
#else
				logger(1, "FILE_QUEUE, Can't malloc. Errno=%d\n",
					errno);
#endif
				bug_check("FILE_QUEUE, Can't malloc() memory");
			}
			temp->next = 0;		/* Last item in list */
	}
	else {
/* Look for right place to put it */
		keep = TempLine->QueueStart;
		while((keep->next != 0) &&
		      ((keep->next)->FileSize <= FileSize))
			keep = keep->next;
		if((temp = (struct QUEUE *)malloc(sizeof(struct QUEUE))) != 0) {
			if((keep == TempLine->QueueStart) &&
			   (keep->FileSize > FileSize)) {
				/* Have to add at queue's head */
				temp->next = keep;
				TempLine->QueueStart = temp;
			} else {	/* Insert in middle of queue */
				temp->next = keep->next;
				keep->next = temp;
			}
			if(temp->next == 0)	/* Added as the last item */
				TempLine->QueueEnd = temp;
		}
	}
	if(temp == 0) {
#ifdef VMS
		logger(1, "FILE_QUEUE, Can't malloc. Errno=%d, VmsErrno=%d\n",
			errno, vaxc$errno);
#else
		logger(1, "FILE_QUEUE, Can't malloc. Errno=%d\n",
			errno);
#endif
		bug_check("FILE_QUEUE, Can't Malloc for file queue.");
	}
	strcpy(temp->FileName, FileName);
	temp->FileSize = FileSize;	/* So we can see it in SHOW QUEUE */
	TempLine->QueuedFiles++;	/* Increment count */
}



/*
 | Show the files queued for each line.
 */
show_files_queue(UserName)
char	*UserName;	/* To whome to broadcast the reply */
{
	int	i;
	struct	QUEUE	*temp;
	char	line[LINESIZE],
		from[SHORTLINE],		/* The sender (local daemon) */
		to[SHORTLINE];		/* to whome to send */

/* Create the sender and receiver's addresses */
	sprintf(from, "HUJI-NJE@%s", LOCAL_NAME);
	sprintf(to, "%s@%s", UserName, LOCAL_NAME);

	sprintf(line, "Files waiting:", LOCAL_NAME);
	send_nmr(from, to, line, strlen(line), ASCII, CMD_MSG);
	for(i = 0; i < MAX_LINES; i++) {
		if((temp = IoLines[i].QueueStart) != 0) {
			sprintf(line, "Files queued for link %s:",
				IoLines[i].HostName);
			send_nmr(from, to, line, strlen(line),
				ASCII, CMD_MSG);
			while(temp != 0) {
				sprintf(line, "      %s, %d blocks",
					temp->FileName, temp->FileSize);
				send_nmr(from, to, line, strlen(line),
					ASCII, CMD_MSG);
				temp = temp->next;
			}
		}
	}
	sprintf(line, "End of list:");
	send_nmr(from, to, line, strlen(line), ASCII,
		CMD_MSG);
}
