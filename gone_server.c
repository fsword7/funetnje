/* GONE_SERVER.C	V1.1
 | These are the routines used to keep the Gone database. They init it, add and
 | remove users to it, and log the messages when needed.
 |
 | V1.1 27/3/90 - Convert GoneUsersDir from an array of strings to an array
 |      of pointers, in order to save memory. However, leave the list of
 |      usernames as an array of strings, as they are smaller, and the locality
 |      of memory might speed up searching (probably not too much).
 */
#include <stdio.h>

#define	MAX_GONES	100
char	GoneUsers[MAX_GONES][16];
char	*GoneUsersDir[MAX_GONES];
int	GoneUsersNum = 0;


/*
 | Add a user to the list. We also store his login directory, so the program
 | later will not have to look for it each time there is a message to log.
 | We keep the list in sorted order, so the search later will be faster (binary
 | search).
 */
add_gone_user(line)
char	*line;
{
	char	UserName[128], LoginDirectory[128];
	int	i, j;

	if(GoneUsersNum == (MAX_GONES - 1))	/* No room */
		return;				/* Ignore it */

	if(sscanf(line, "%s %s", UserName, LoginDirectory) != 2) {
		logger(1, "GONE_SERVER, Can't get parameters from '%s'\n", line);
		return;
	}

/* Look for the correct place to put it on the list */
	for(i = 0; i < GoneUsersNum; i++) {
		if((j = strcmp(UserName, GoneUsers[i])) == 0)
			return;	/* ALready there - do nothing */
		if(j < 0)	/* We have to insert it before the current one */
			break;
	}

/* Clear a place for him - Move all entries after it one place higher */
	if(i != GoneUsersNum) {
		for(j = GoneUsersNum; j >= i; j--) {
			strcpy(GoneUsers[j + 1], GoneUsers[j]);
			GoneUsersDir[j + 1] = GoneUsersDir[j];
		}
	}

	GoneUsersNum++;
/* And insert it now */
	strcpy(GoneUsers[i], UserName);
	if((GoneUsersDir[i] = (char *)malloc(strlen(LoginDirectory) + 1)) == NULL) {
		logger(1, "GONE_SERVER, Can't malloc.\n");
		bug_check("Can't allocate memory for Gone");
	}
	strcpy(GoneUsersDir[i], LoginDirectory);
}


/*
 | Delete a user from the database if he exists there.
 */
del_gone_user(UserName)
char	*UserName;
{
	int	i, j;
	char	*LoginDirectory;	/* Need it to call Get_gone_user */

/* Look for his entry */
	if((i = get_gone_user(UserName, &LoginDirectory)) < 0)
		return;		/* Not registered */

/* Exists - delete him */
	for(j = i; j < GoneUsersNum; j++) {
		strcpy(GoneUsers[j], GoneUsers[j + 1]);
		GoneUsersDir[j] = GoneUsersDir[j + 1];
	}
	GoneUsersNum--;
	free(LoginDirectory);	/* Free the memory used by him */
}


/*
 | Try sending the message to the gone user. If he was found and we could
 | write it, ok - return -1 as number of users received the message so the
 | caller of us will know to return a Gone message. If not, return 0.
 */
send_gone(UserName, string)
char	*UserName, *string;
{
	char	*LoginDirectory, *p,
		FileName[128], *local_time();	/* Defined in MAIN */
	FILE	*fd;

/* Get login directory and open the file */
	if(get_gone_user(UserName, &LoginDirectory) < 0)
		return NULL;	/* Not found */
	sprintf(FileName, "%sYGONE_MESSAGES.TXT", LoginDirectory);
	if((fd = fopen(FileName, "a")) == NULL) {
		logger(1, "GONE_SERVER, Can't open user's file '%s'\n", FileName);
		return 0;
	}
/* Remove all controls from line and append it with the time */
	for(p = string; *p != '\0'; p++)
		if(*p < ' ') *p = ' ';
/* Jump over leading spaces */
	for(p = string; *p != '\0'; p++)
		if(*p != ' ') break;
	fprintf(fd, "%s: %s\n", local_time(), p);
	fclose(fd);
	return -1;
}


/*
 | Inform to all users in Gone that we shutdown now.
 */
shut_gone_users()
{
	int	i;

	for(i = 0; i < GoneUsersNum; i++)
		send_gone(GoneUsers[i], "***** NJE emulator shut down *****");
}


/*
 | Pass over the list of Gone users (using binary search) and return the
 | login directory of that user and the index of it in the GoneUsers array.
 | If not found, return -1;
 */
get_gone_user(UserName, LoginDirectory)
char	*UserName, **LoginDirectory;
{
	int		First, Last, Middle,	/* For binary search */
			CompareResult;

/* Do a binary search */
	First = 0; Last = GoneUsersNum - 1;	/* The last entry */
	Middle = (First + Last) / 2;

	for(;;) {
		if((CompareResult = strcmp(UserName, GoneUsers[Middle])) == 0) {
			*LoginDirectory = GoneUsersDir[Middle];
			return Middle;
		}

/* Not equal - compute new bottom/up */
		if(CompareResult < 0) {
			Last = Middle;
			Middle = (First + Last) / 2;
		} else {
			First = Middle;
			Middle = (First + Last) / 2;
		}
/* Prevent oscialtions */
		if((Last - First) <= 1) {
			if((CompareResult = strcmp(UserName, GoneUsers[First])) == 0) {
				*LoginDirectory = GoneUsersDir[First];
				return First;
			}
			if((CompareResult = strcmp(UserName, GoneUsers[Last])) == 0) {
				*LoginDirectory = GoneUsersDir[Last];
				return Last;
			} else
				return -1;	/* Not found */
		}
	}
}


/*
 | Dump the whole gone list into the logfile. This is used when using DEBUG DUMP
 | to help locate problems.
 */
dump_gone_list()
{
	int	i;

	logger(1, "** Gone list dump (%d users):\n", GoneUsersNum);
	for(i = 0; i < GoneUsersNum; i++)
		logger(1, "   %s %s\n", GoneUsers[i], GoneUsersDir[i]);
	logger(1, "End of gone list\n");
}
