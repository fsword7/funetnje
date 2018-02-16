/*  NJE_BUILD.C	V 1.0
 | Primitive program to build the routing table for the NJEF. Should be improved...
 |  WARNNING: The format of the standard routing table is assumed to be:
 |            _ROUTE_Site-name_other things
 |            Where _ is a space.
 | The format of the header file is:
 |  SITE  LINE  FORMAT
 |
*/

#include <stdio.h>
#include <rms.h>


main()
{
	struct	FAB	fab;
	struct	RAB	rab;
	struct	XABKEY	xabkey;
	char	key_name[33] = "Site name & user name (orig.)  ";
	char	*c, line[132],
		site[21], LineName[21], format[20],
		temp_line[132];	/* Just a place holder */
	char	in_file[80], header_file[80], out_file[80];
	FILE	*Ifd, *fd, *Dfd;
	long	AccessLevel, i, counter, status, Dcount;

/* Get files' names */
	printf("Header file: "); gets(header_file);
	printf("Routing table: "); gets(in_file);
	printf("Output file: "); gets(out_file);
	if((c = strchr(header_file, '\n')) != NULL) *c = '\0';
	if((c = strchr(in_file, '\n')) != NULL) *c = '\0';
	if((c = strchr(out_file, '\n')) != NULL) *c = '\0';

	if((Ifd = fopen(header_file, "r")) == NULL) {
		printf("Can't open header file '%s'\n", header_file);
		exit(1);
	}
	if((fd = fopen(in_file, "r")) == NULL) {
		printf("Can't open input file: %s\n", in_file);
		exit(1);
	}

	fab = cc$rms_fab;  rab = cc$rms_rab; xabkey = cc$rms_xabkey;

	fab.fab$l_fna = &out_file;
	fab.fab$b_fns = strlen(out_file);
	fab.fab$b_fac = (FAB$M_GET | FAB$M_PUT);
	fab.fab$l_fop = (FAB$M_CBT | FAB$M_DFW);
	fab.fab$b_org = FAB$C_IDX;
	fab.fab$b_rfm = FAB$C_VAR;	/* Variable records length */
	fab.fab$l_xab = &xabkey;
	fab.fab$l_alq = 100;		/* Start with 100 blocks */
	fab.fab$w_deq = 100;		/* And extend each time with 100 blocks */
	fab.fab$b_bks = 3;		/* Suggested by ANA/RMS/OPTIMIZE */
	rab.rab$l_fab = &fab;
	rab.rab$l_kbf = temp_line;		/* Key buffer */
	rab.rab$b_ksz = 8;		/* Length of key */
	rab.rab$b_krf = 0;		/* Main key */
	rab.rab$b_rac = RAB$C_KEY;
	rab.rab$l_rop = RAB$M_WBH;
	xabkey.xab$w_dfl = 0;
	xabkey.xab$b_dtp = XAB$C_STG;	/* Key is of dtring type */
	xabkey.xab$b_flg = (XAB$M_DAT_NCMPR | XAB$M_DAT_NCMPR | XAB$M_KEY_NCMPR);
	xabkey.xab$b_prolog = 3;
	xabkey.xab$b_ref = 0;		/* Main key */
	xabkey.xab$b_siz0 = 8;		/* 22 characters long */
	xabkey.xab$l_knm = key_name;

	if(((status = sys$create(&fab)) & 0x1) == 0) {
		printf("Can't create output file '%s', %d\n", out_file, status);
		return status;
	}
	if(((status = sys$connect(&rab)) & 0x1) == 0) {
		printf("Can't connect output file, %d\n", status);
		return status;
	}

	counter = 0;  Dcount = 0;
	printf("Records read:\n");

/* The header file */
	while(fgets(line, sizeof line, Ifd) != NULL) {
		if(*line == '*') continue;		/* Comment line */
		if((c = strchr(line, '\n')) != 0) *c = 0;
		counter++;  if(counter/100*100 == counter) printf("%d ", counter);
		if((status = sscanf(line, "%s %s %s",
				site, LineName, format)) < 3) {
			printf("Illegal line: %s\n", line);
			continue;
		}
		for(i = strlen(site); i < 8; i++) site[i] = ' '; site[8] = 0;
		sprintf(temp_line, "%s %s %s",
				site, LineName, format);

		rab.rab$l_kbf = temp_line;
		rab.rab$b_ksz = 8;		/* Key size */
		rab.rab$b_rac = RAB$C_KEY;	/* 'Rewind' file */
		rab.rab$l_rbf = temp_line;
		rab.rab$w_rsz = strlen(temp_line);
		if(((status = sys$put(&rab)) & 0x1) == 0) {
			printf("Can't write, %d: %s\n", status, temp_line);
		}
	}

/* Now process the routing table */
	while(fgets(line, sizeof line, fd) != NULL) {
		if(*line == '*') continue;		/* Comment line */
		if((c = strchr(line, '\n')) != 0) *c = 0;
		counter++;  if(counter/100*100 == counter) printf("%d ", counter);
		if((status = sscanf(line, "%s %s %s",
				temp_line, site, LineName)) < 2) {
			printf("Illegal line: %s\n", line);
			continue;
		}
		for(i = strlen(site); i < 8; i++) site[i] = ' '; site[8] = 0;
		sprintf(temp_line, "%s %s EBCDIC",
				site, LineName);

/* Test for duplicate records, and write them in DUPLICATE.TMP */
		rab.rab$l_kbf = temp_line;
		rab.rab$b_ksz = 8;		/* Key size */
		rab.rab$b_rac = RAB$C_KEY;	/* 'Rewind' file */
		rab.rab$l_rbf = temp_line;
		rab.rab$w_rsz = strlen(temp_line);
		if(((status = sys$put(&rab)) & 0x1) == 0) {
			printf("Can't write, %d: %s\n", status, temp_line);
		}
	}

	sys$close(&fab);
	fclose(fd);  fclose(Ifd);
	printf("\nTotal records inserted: %d, %d duplicates in DUPLICATE.TMP\n",
		counter, Dcount);
}
