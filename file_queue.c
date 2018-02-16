/* FILE_QUEUE.C   V2.0-mea
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
 */

#include "consts.h"
#include "prototypes.h"

#ifdef	HAS_LSTAT	/* lstat(2), or only stat(2)  ?? */
# define  STATFUNC lstat
#else
# define  STATFUNC stat
#endif

static void submit_file __((const int fd, const char *FileName, int FileSize));

int	file_queuer_pipe = -1;
#define	HASH_SIZE 503
static struct QUEUE *queue_hashes[HASH_SIZE];
static int hash_inited = 0;

static void
__hash_init()
{
	int i;

	if (hash_inited) return;

	for (i = 0; i < HASH_SIZE; ++i)
	  queue_hashes[i] = NULL;
	hash_inited = 1;
}

static void
hash_insert(Entry)
struct QUEUE *Entry;
{
	int i;

	if (!hash_inited) __hash_init();

	i = Entry->hash % HASH_SIZE;

	Entry->hashnext = queue_hashes[i];
	queue_hashes[i] = Entry;
}

static void
hash_delete(Entry)
struct QUEUE *Entry;
{
	int i, match = 0;
	struct QUEUE *hp1, *hp2;

	if (!hash_inited) __hash_init();

	i = Entry->hash % HASH_SIZE;
	if (queue_hashes[i] == NULL)
	  return;

	hp1 = NULL; /* Predecessor */
	hp2 = queue_hashes[i];
	while (hp2 != NULL) {
	  while (hp2 != NULL && hp2->hash != Entry->hash) {
	    hp1 = hp2; /* predecessor */
	    hp2 = hp2->hashnext;
	  }
	  if (hp2 == NULL) return; /* Nothing to do, quit */
	  if (hp2->hash == Entry->hash) { /* Hash-match,
					     check real match: */
#ifdef	UNIX
	    if (hp2->fstats.st_dev  == Entry->fstats.st_dev &&
		hp2->fstats.st_ino  == Entry->fstats.st_ino &&
		hp2->fstats.st_size == Entry->fstats.st_size)
	      match = 1;
#else
	    match = (strcmp(hp2->FileName,Entry->FileName) == 0);
#endif
	    if (match) break;
	  }
	  hp1 = hp2; /* Predecessor */
	  hp2 = hp2->hashnext;
	}
	if (!match)
	  return; /* No match.. */

	if (hp1 == NULL) /* Match at first */
	  queue_hashes[i] = hp2->hashnext;
	else
	  hp1->hashnext = hp2->hashnext;

	Entry->hashnext = NULL; /* Superfluous, this entry will
				   be deleted next */
}

static struct QUEUE *
hash_find(Entry)
struct QUEUE *Entry;
{
	int i;
	struct QUEUE *hp2;

	if (!hash_inited) __hash_init();

	i = Entry->hash % HASH_SIZE;
	if (queue_hashes[i] == NULL)
	  return NULL;

	hp2 = queue_hashes[i];
	while (hp2 != NULL) {
	  while (hp2 != NULL && hp2->hash != Entry->hash) {
	    hp2 = hp2->hashnext;
	  }
	  if (hp2 == NULL) return NULL; /* Nothing to do, quit */
	  if (hp2->hash == Entry->hash) { /* Hash-match,
					     check real match: */
#ifdef	UNIX
	    if (hp2->fstats.st_dev  == Entry->fstats.st_dev &&
		hp2->fstats.st_ino  == Entry->fstats.st_ino &&
		hp2->fstats.st_size == Entry->fstats.st_size)
	      return hp2;
#else
	    if (strcmp(hp2->FileName,Entry->FileName) == 0)
	      return hp2;
#endif
	  }
	  hp2 = hp2->hashnext;
	}
	return NULL; /* No match.. */
}



/*
 | Zero the queue pointers. Then go over the queue and look for files queued
 | to us. Each file found is queued according to the routing table and
 | alternate routes.
 | When working on a Unix system, we have a problem of telling the Find-File
 | routine whether this is the first call or not (on VMS the value of Context
 | tells it). Thus, on Unix, if FileMask is none-zero, this is the first call.
 | If it points to a Null byte, then this is not the first call, and the
 | find-file routine already has the parameters in its internal variables.
 */
void
init_files_queue()
{
	int	i = -1;
	long	FileSize;
	int	sync_mode = 0;
	int	pipes[2];
	int	filecnt = 0;
	char	FileMask[LINESIZE], FileName[LINESIZE];
#ifdef VMS
	long	context;	/* Context for find file */
#else
	DIR	*context;
#endif

#ifdef	UNIX
	pipes[0]  = -1;
	sync_mode = pipe(pipes);
	if (file_queuer_pipe >= 0)
	  close(file_queuer_pipe);
	file_queuer_pipe = pipes[0];

	if (!sync_mode)
	  if ((i = fork()) > 0) return;	/* Parent.. */

	if (!sync_mode && i < 0) sync_mode = 1;	/* Must do in the usual way */
	/* When fork()'s return code is 0, we are a child process,
	   which then goes on and scans all the queue directories,
	   and sends submissions to the main program.  A LOT faster
	   start that way, if the queues are large for some reason.. */
	logger(1,"FILE_QUEUE: %s mode queue initializer running, pid=%d\n",
	       !sync_mode ? "Child":"Sync",getpid());
	if (!sync_mode && i == 0) { /* Child must die on PIPE lossage.. */
	  signal(SIGPIPE,SIG_DFL);
#ifdef	SIGPOLL
	  signal(SIGPOLL,SIG_DFL);
#endif
	  /* No point in closing all unnecessary file descriptors.
	     the final  _exit()  will take care of them. */
	}
#endif


	for (i = 0; i < MAX_LINES; i++) {
	  if (IoLines[i].HostName[0] == 0) continue;

	  /* Scan the queue for files to be sent */
#ifdef VMS
	  sprintf(FileMask, "%sASC_*.*;*", BITNET_QUEUE);
	  context = 0;
#else
	  sprintf(FileMask, "%s/%s/*", BITNET_QUEUE, IoLines[i].HostName);
	  make_dirs(FileMask); /* Make sure the dir exists.. */
#endif

	  while (find_file(FileMask, FileName, &context) == 1) {
	    FileSize = get_file_size(FileName);	/* Call OS specific routine */
	    /* Although we know the exact line name we call Queue_File();
	       this is usefull when this routine is called via DEBUG RESCAN
	       command and will queue the files to the alternate route,
	       if found.						*/
	    if (sync_mode)
	      queue_file(FileName, FileSize);
	    else
	      submit_file(pipes[1], FileName, FileSize);
	    ++filecnt;
#ifdef UNIX
	    *FileMask = '\0';	/* So we know this is not the first call */
#endif
	  }

	}
#ifdef UNIX
	sprintf(FileMask, "%s/*",BITNET_QUEUE);
	make_dirs( FileMask );

	while (find_file(FileMask, FileName, &context) == 1) {
	  FileSize = get_file_size(FileName);
	  /* Call OS specific routine */
	  if (sync_mode)
	    queue_file(FileName, FileSize);
	  else
	    submit_file(pipes[1],FileName, FileSize);
	  ++filecnt;
	  *FileMask = '\0';	/* So we know this is not the first call */
	} /* while( find_file() ) */
#endif
	if (!sync_mode) {
	  logger(1,"FILE_QUEUE: Child-mode file queuer done. Submitted %d files.\n",filecnt);
	  _exit(0);
	}
}


/* ================ submit_file()  ================ */

static void
submit_file(fd, FileName, FileSize)
const int fd;
const char	*FileName;
int	FileSize;
{
	unsigned char	line[LINESIZE];
	long	size;

	FileSize /= 512; /* receiver multiplies it again.. */

	/* Send file size */
	line[1] = (unsigned char)((FileSize & 0xff00) >> 8);
	line[2] = (unsigned char)(FileSize & 0xff);
	strcpy(&line[3], FileName);
	size = strlen(&line[3]) + 4; /* include terminating NULL.. */
	line[0] = size-1;

	writen(fd,line,size);
}


/* ================ queue_file() ================ */
/*
 | Given a file name, find its line number and queue it. If the line number is
 | not defined, ignore this file.
 | We try getting an alternate route if possible. If the status returned is
 | that the link exists but inactive, we must loop again and look for it.
*/
void
queue_file(FileName, FileSize)
const char	*FileName;
const int	FileSize;	/* File size in blocks */
{
	register char	*p;
	register int	i;
	int	rc, primline = -1, altline = -1;
	char	MessageSender[10];
	struct	FILE_PARAMS FileParams;
	FILE	*InFile;
	char	auxline[LINESIZE];
	char	Format[16];	/* Not needed but returned by Find_line_index() */
	struct stat stats;
	
	logger(3,"FILE_QUEUE: queue_file(%s,%d)\n",FileName,FileSize);

	i = strlen(BITNET_QUEUE);
	if (FileName[0] != '/' ||
	    strncmp(FileName,BITNET_QUEUE,i) != 0 ||
	    FileName[i] != '/') {
	  logger(1, "FILE_QUEUE: File to be queued must be within `%s/'. Its name was: `%s'\n",BITNET_QUEUE,FileName);
	  return;
	}

	if (STATFUNC(FileName,&stats) != 0 ||
	    !S_ISREG(stats.st_mode)) {
	  logger(1,"Either the file does not exist, or it is not a regular file, file: `%s'\n",FileName);
	  return;
	}
	strcpy(FileParams.SpoolFileName,FileName);

	InFile = fopen(FileName,"r+");
	if (InFile == NULL) {
	  logger(1, "FILE_QUEUE: Couldn't open file '%s' for envelope analysis.\n",FileName);
	  return;
	}
	*FileParams.line = 0;
	rc = parse_envelope( InFile, &FileParams, 0 );
	fclose( InFile );
	if ((rc < 0) || (*FileParams.From == '*')) {
	  /* Bogus file. Move to hideout... */
	  logger(1,"FILE_QUEUE, queue_file(%s) got bad file - header contains junk..\n",FileName);
	  rename_file(&FileParams, RN_JUNK, F_OUTPUT_FILE);
	  return;
	}
    

	if ((p = strchr(FileParams.To,'@')) == NULL) {
	  /* Huh! No '@' in target address ! */
	  rename_file(&FileParams, RN_JUNK, F_OUTPUT_FILE);
	  logger(1,"FILE_QUEUE: ### No '@' in target address !!!  File:%s\n",
		 FileParams.SpoolFileName);
	  return;
	}
	++p;	/* P points to target node.  Find route! */
	switch (i = find_line_index(p, FileParams.line, Format,
				    &primline, &altline)) {
	  case NO_SUCH_NODE:
	      /* Pass to local mailer. */
	      rename_file(&FileParams, RN_JUNK, F_OUTPUT_FILE);
	      logger(1, "FILE_QUEUE: Can't find line # for file '%s', line '%s', Tonode='%s'\n",

		     FileName,FileParams.line,p);
	      break;
	  case ROUTE_VIA_LOCAL_NODE:
	      inform_filearrival( FileName,&FileParams,auxline );
	      sprintf(MessageSender, "@%s", LOCAL_NAME);
	      /* Send message back only if not found the QUIET option. */
	      if (((FileParams.type & F_NOQUIET) != 0) &&
		  (*auxline != 0))
		send_nmr((char*)MessageSender,
			 FileParams.From,
			 auxline, strlen(auxline),
			 ASCII, CMD_MSG);
	      else
		logger(3,"RECV_FILE: Quiet ack of received file - no msg back.\n");
	      break;

	  case LINK_INACTIVE:	/* No alternate route available... */
	      /* Anyway place the file to there.. */
	      /* Queue it */
	      FileName = rename_file(&FileParams,RN_NORMAL,F_OUTPUT_FILE);
	      add_to_file_queue(FileName, primline, FileSize);
	      return;

	  default:	/* Hopefully a line index */
	      if ((i < 0) || (i > MAX_LINES) ||
		  IoLines[i].HostName[0] == 0) {
		logger(1, "FILE_QUEUE, Find_line_index() returned erronous index (%d) for node %s\n", i, p);
		return;
	      }
	      FileName = rename_file(&FileParams,RN_NORMAL,F_OUTPUT_FILE);
	      add_to_file_queue(FileName, i, FileSize);
	      break;
	}
}

/*
 | Given a filename and the line number (index into IoLines array),
 | the filename will be queued into that line.  Order the list according
 | to the file's size.
 */

static void /* Internal routine, common to many.. */
__add_to_file_queue(Line, Index, Entry)
struct LINE *Line;
const int Index;
struct QUEUE *Entry;
{
	struct	QUEUE	*keep, *keep2;
	int FileSize = Entry->FileSize;

	if (hash_find(Entry) != NULL) {
	  logger(2,"FILE_QUEUE: add_to_file_queue(%s, %s) already in the queue\n",
		 Line->HostName,Entry->FileName);
	  free(Entry);
	  return;
	}

	if (Line->QueueStart == NULL) { /* Init the list */
	  /* Empty list - insert at the head */
	  Line->QueueStart = Line->QueueEnd = Entry;
	} else {
	  /* Look for right place to put it */
	  keep = Line->QueueStart;
	  while ((keep->next != NULL) &&
		 (keep->next->FileSize < FileSize))
	    keep = keep->next;
#ifdef	UNIX
	  keep2 = keep;
	  while (keep2 != NULL && FileSize == keep2->FileSize) {
	    if (keep2->fstats.st_dev  == Entry->fstats.st_dev &&
		keep2->fstats.st_ino  == Entry->fstats.st_ino &&
		keep2->fstats.st_size == Entry->fstats.st_size) {
	      logger(2,"FILE_QUEUE: add_to_file_queue(%s, %s) already in the queue\n",
		     Line->HostName,Entry->FileName);
	      free(Entry);
	      return;
	    }
	    keep2 = keep2->next;
	  }
#endif
	  if ((keep == Line->QueueStart) &&
	      (keep->FileSize > FileSize)) {
	    /* Have to add at queue's head */
	    Entry->next = keep;
	    Line->QueueStart = Entry;
	  } else {		/* Insert in middle of queue */
	    Entry->next = keep->next;
	    keep->next = Entry;
	  }
	  if (Entry->next == 0)	/* Added as the last item */
	    Line->QueueEnd = Entry;
	}
	if (Entry == 0) {
#ifdef VMS
	  logger(1, "FILE_QUEUE, Can't malloc. Errno=%d, VmsErrno=%d\n",
		 errno, vaxc$errno);
#else
	  logger(1, "FILE_QUEUE, Can't malloc. Errno=%d\n",
		 errno);
#endif
	  bug_check("FILE_QUEUE, Can't Malloc for file queue.");
	}
	hash_insert(Entry);
	Line->QueuedFiles++;	/* Increment count */
	Line->QueuedFilesWaiting++;	/* Increment count */
}



void
add_to_file_queue(FileName, LineIndex, FileSize)
const char *FileName;
const int LineIndex;
const int FileSize;
{
	struct	QUEUE	*Entry;
	struct	LINE	*Line;

	Line = &(IoLines[LineIndex]);
	Entry = (struct QUEUE *)malloc(sizeof(struct QUEUE));

	if (Entry == NULL) {
#ifdef VMS
	  logger(1, "FILE_QUEUE, Can't malloc. Errno=%d, VmsErrno=%d\n",
		 errno, vaxc$errno);
#else
	  logger(1, "FILE_QUEUE, Can't malloc. Errno=%d\n",
		 errno);
#endif
	  bug_check("FILE_QUEUE, Can't malloc() memory");
	}

#ifdef	UNIX
	if (STATFUNC(FileName,&Entry->fstats) != 0) {
	  logger(1,"** FILE_QUEUE: Tried to queue a non-existent file: `%s' on line %d\n",FileName,LineIndex);
	  return;
	}
#endif

	strcpy(Entry->FileName, FileName);
#ifdef UNIX
	Entry->FileSize = Entry->fstats.st_size; /* So we can see it in
						    SHOW QUEUE */
	Entry->hash = ((Entry->fstats.st_size ^ Entry->fstats.st_ino) ^
		       Entry->fstats.st_dev);
#else
	Entry->FileSize = FileSize;
	{
	  char *s = FileName; long hash = 0;
	  while (*s) { if (hash & 1) hash = (~hash) >> 1; hash += *s++; }
	  Entry->hash     = hash;
	}
#endif
	Entry->hashnext = NULL;
	Entry->next     = NULL;
	Entry->state    = 0;
	Entry->primline = LineIndex;
	Entry->altline  = -1;

	__add_to_file_queue(Line,LineIndex,Entry);
#if 0 /* XX: Think more about multiple parallel queues.. */
#endif
}

/*
 | Show the files queued for each line.
 */
void
show_files_queue(UserName,LinkName)
const char	*UserName;		/* To whom to broadcast the reply */
char *LinkName;
{
	int	i;
	struct	QUEUE	*temp;
	char	line[LINESIZE],
		from[SHORTLINE];		/* The sender (local daemon) */
	int	queuelen = strlen(BITNET_QUEUE)+1;
	int	maxlines = 1000;

	/* Create the sender and receiver's addresses */
	sprintf(from, "@%s", LOCAL_NAME);

	upperstr(LinkName);
	if (*LinkName) {
	  maxlines = 30;
	  for (i = 0; i < MAX_LINES; ++i) {
	    if (IoLines[i].HostName[0] != 0 &&
		strcmp(IoLines[i].HostName,LinkName)==0) {
	      if ((temp = IoLines[i].QueueStart) != 0) {
		sprintf(line, "Showing at most %d files on link %s queue",
			maxlines, LinkName);
		send_nmr(from, UserName, line, strlen(line), ASCII, CMD_MSG);
		i = 0;
		while (temp != NULL && maxlines-- > 0) {
		  sprintf(line, " %3d   %s, %4d kB %s", ++i,
			  temp->FileName+queuelen, temp->FileSize/1024,
			  (temp->state < 0) ? "HELD" : ((temp->state > 0) ?
							"Sending" : "Waiting"));
		  send_nmr(from, UserName, line, strlen(line), ASCII, CMD_MSG);
		  temp = temp->next;
		}
		maxlines = i; /* Know how many files were shown */
		while (temp != NULL) {
		  temp = temp->next;
		  ++i;
		}
		sprintf(line, "End of list: %d out of %d files",maxlines,i);
	      } else {
		sprintf(line, "No files queued on link %s", LinkName);
	      }
	      send_nmr(from, UserName, line, strlen(line), ASCII, CMD_MSG);
	      return;
	    }
	  }
	  sprintf(line, "Link %s is not defined", LinkName);
	  send_nmr(from, UserName, line, strlen(line), ASCII, CMD_MSG);
	  return;
	} else {

	  sprintf(line, "Files waiting:");
	  send_nmr(from, UserName, line, strlen(line), ASCII, CMD_MSG);
	  for (i = 0; i < MAX_LINES; i++) {
	    if (IoLines[i].HostName[0] != 0 &&
		(temp = IoLines[i].QueueStart) != 0) {
	      sprintf(line, "Files queued for link %s:",
		      IoLines[i].HostName);
	      send_nmr(from, UserName, line, strlen(line),
		       ASCII, CMD_MSG);
	      while (temp != 0) {
		sprintf(line, "      %s, %4dkB %s",
			temp->FileName+queuelen, temp->FileSize/1024,
			(temp->state < 0) ? "HELD" : ((temp->state > 0) ?
						      "Sending" : "Waiting"));
		send_nmr(from, UserName, line, strlen(line), ASCII, CMD_MSG);
		temp = temp->next;
	      }
	    }
	  }
	}
	sprintf(line, "End of list:");
	send_nmr(from, UserName, line, strlen(line), ASCII, CMD_MSG);
}


void
dequeue_file_entry_ok(Index,temp)
const int Index;
struct LINE	*temp;
{
	struct	QUEUE	*FE,*FE2;
	struct	FILE_PARAMS *FP = & temp->OutFileParams[temp->CurrentStream];

	if (FP->FileEntry == NULL) {
	  logger(1,"FILE_QUEUE: dequeue_file_entry_ok(%s:%d) Non-active stream dequeued!\n",
		 temp->HostName,temp->CurrentStream);
	  return;
	}

	/* logger(2,"FILE_QUEUE: dequeue_file_entry_ok(%s:%d) fn=%s, state=%d\n",
	   temp->HostName,temp->CurrentStream,FP->SpoolFileName,
	   FP->FileEntry->state); */
	FP->FileEntry->state=0;

	hash_delete(FP->FileEntry);

	if (FP->FileEntry->next == NULL && FP->FileEntry != temp->QueueEnd) {
	  /* Actually do nothing, as this is a lone devil left over from
	     DEBUG RESCAN -- an active stream at the time of the rescan.. */
	} else if (FP->FileEntry == temp->QueueStart) {
	  /* Ours is the first one.. */
	  temp->QueueStart = FP->FileEntry->next;
	  if (temp->QueueStart == NULL)	/* And the only one.. */
	    temp->QueueEnd = NULL;

	} else {
	  FE = temp->QueueStart;
	  FE2 = FE;
	  while (FE->next) {	/* MUST go at least once, as else it was
				   the first one.. */
	    if (FE == FP->FileEntry) break;
	    FE2 = FE;
	    FE = FE->next;
	  }
	  /* We might have ordered DEBUG RESCAN, which can cause
	     a situation, where only pointer to the file entry is
	     FP->FileEntry...					 */
	  if (FP->FileEntry == FE) {
	    /* Now FE points to the entry to be dequeued,
	       and FE2 points to its predecessor		*/
	    FE2->next = FE->next;
	    if (FE2->next == NULL) /* End of list, predecessor is the tail */
	      temp->QueueEnd = FE2;
	  }
	}
	free(FP->FileEntry);
	FP->FileEntry = NULL;
	temp->QueuedFiles -= 1;
}

void
requeue_file_entry(Index,temp)
const int Index;
struct LINE	*temp;
{
	int i = temp->CurrentStream;

	dequeue_file_entry_ok(Index,temp);
	/* Requeue file */
	queue_file(temp->OutFileParams[i].SpoolFileName,
		   temp->OutFileParams[i].FileSize);
}


struct QUEUE *
pick_file_entry(Index,temp)
const int Index;
const struct LINE *temp;
{
	struct QUEUE *FileEntry = temp->QueueStart;
	int i = temp->QueuedFiles+1;

	/* In case the file is queued on an alternate line as well, we have
	   to make sure that when the primary link is active, we don't rob
	   files from there to the alternate link.. */

#if 1
	while (FileEntry && FileEntry->state != 0 && --i > 0)
#else
	while (FileEntry && FileEntry->state != 0 && --i > 0 &&
	       FileEntry->altline == Index &&
	       IoLines[FileEntry->primline].state == ACTIVE)
#endif

	  FileEntry = FileEntry->next;


	if (i <= 0)
	  bug_check("Aborting because of corruption of  file-entry queue!");
	return FileEntry;
}


int /* Count actives */
requeue_file_queue(Line)
struct LINE *Line;
{
	struct QUEUE	*Entry, *NextEntry;
	int activecnt = 0;

	Entry = Line->QueueStart;

	/* Free all non-active streams, and collect active ones
	   back into the link queue ... */

	Line->QueueStart = NULL;
	Line->QueueEnd   = NULL;
	
	while (Entry != NULL) {
	  NextEntry = Entry->next;
	  Entry->next = NULL;

	  /* States are: 0: Waiting, -1: held, +1: active */
	  if (Entry->state < 1) {	/* Don't free active files! */
	    hash_delete(Entry);
	    queue_file(Entry->FileName,Entry->FileSize);
	    free(Entry);
	  } else {
	    ++activecnt;
	    if (Line->QueueStart == NULL)
	      /* Can be NULL only at the first time. */
	      Line->QueueStart = Entry;
	    else
	      /* Inserted at second, and later times.*/
	      Line->QueueEnd->next = Entry;
	    Line->QueueEnd = Entry;
	  }
	  Entry = NextEntry;
	}

	return activecnt;
}


int /* Count actives */
delete_file_queue(Line)
struct LINE *Line;
{
	struct QUEUE	*Entry, *NextEntry;
	int activecnt = 0;

	Entry = Line->QueueStart;

	/* Free all non-active streams, and collect active ones
	   back into the link queue ... */

	Line->QueueStart = NULL;
	Line->QueueEnd   = NULL;

	while (Entry != NULL) {
	  NextEntry = Entry->next;
	  Entry->next = NULL;

	  /* States are: 0: Waiting, -1: held, +1: active */
	  if (Entry->state < 1) {	/* Don't free active files! */
	    hash_delete(Entry);
	    free(Entry);
	  } else {
	    ++activecnt;
	    if (Line->QueueStart == NULL)
	      /* Can be NULL only at the first time. */
	      Line->QueueStart = Entry;
	    else
	      /* Inserted at second, and later times.*/
	      Line->QueueEnd->next = Entry;
	    Line->QueueEnd = Entry;
	  }
	  Entry = NextEntry;
	}

	return activecnt;
}
