/* HEADERS.H   V1.3
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
 | The definition of the structures used by NJE.
 | When conforming with RSCS-2.1, and handling splitted records, add the
 | NDVTAGR field in the RSCS section of the NDH.
 |
 | V1.1 - CORRECT the EOFblock.
 | V1.2 - Correct SIGNON record. We forgot the features field at its end...
 | V1.3 - Add the field NDHVTAGR if INCLUDE_TAG is defined.
 */
#include "ebcdic.h"

/* The RSCS version we emulate (currently 1.3) */
#define	RSCS_VERSION	1
#define	RSCS_RELEASE	3

/* The Enquiry block */
struct	ENQUIRE {
		unsigned char	soh, enq, pad;	/* 3 characters block */
	} ;

/* The negative ACK block */
struct	NEGATIVE_ACK {
		unsigned char	nak, pad;
	} ;

/* The positive ACK */
struct	POSITIVE_ACK {
		unsigned char	dle, ack, pad;
	} ;

/* Final signoff */
struct	SIGN_OFF {
		unsigned char	RCB, SRCB, pad;
	};

/* End of File block */
struct	EOF_BLOCK {
		unsigned char	RCB, SRCB, F1, F2;
	};

/* Permit reception of file */
struct	PERMIT_FILE {
		unsigned char	RCB, SRCB, SCB, END_RCB;
	};

/* Ack transmission complete */
struct	COMPLETE_FILE {
		unsigned char	RCB, SRCB, SCB, END_RCB;
	};

/* Reject a file request */
struct	REJECT_FILE {
		unsigned char	RCB, SRCB, SCB, END_RCB;
	};

struct	JOB_HEADER {
		unsigned short	LENGTH;	/* The header which comes before */
		unsigned char	FLAG,
				SEQUENCE;
		unsigned short	LENGTH_4;
		unsigned char	ID,
				MODIFIER;	/* The record itself */
/* M */		unsigned short	NJHGJID;
		unsigned char	NJHGJCLS,
				NJHGMCLS,
				NJHGFLG1,
				NJHGPRIO,
				NJHGORGQ,
				NJHGJCPY,
				NJHGLNCT,
				r1, r2, r3,	/* Reserved */
/* M */				NJHGACCT[8],
/* M */				NJHGJNAM[8],
/* M */				NJHGUSID[8],
/* M */				NJHGPASS[8],
/* M */				NJHGNPAS[8],
/* M */				NJHGETS[8],
/* M */				NJHGORGN[8],
/* M */				NJHGORGR[8],
/* M */				NJHGXEQN[8],
/* M */				NJHGXEQU[8],
/* M */				NJHGPRTN[8],
/* M */				NJHGPRTR[8],
/* M */				NJHGPUNN[8],
/* M */				NJHGPUNR[8],
/* M */				NJHGFORM[8];
		unsigned long	NJHGICRD,
	 	 		NJHGETIM,
				NJHGELIN,
				NJHGECRD;
/* M */		unsigned char	NJHGPRGN[20],
/* M */				NJHGROOM[8],
/* M */				NJHGDEPT[8],
/* M */				NJHGBLDG[8];
		unsigned long	NJHGNREC;
		};

struct	DATASET_HEADER_G {		/* General section */
		unsigned short	LENGTH_4;
		unsigned char	ID,
				MODIFIER;
/* M */		unsigned char	NDHGNODE[8],
				NDHGRMT[8],
/* M */				NDHGPROC[8],
/* M */				NDHGSTEP[8],
/* M */				NDHGDD[8];
		unsigned short	NDHDSNO;
		unsigned char	r1,		/* Reserved */
				NDHGCLAS;
		unsigned long	NDHGNREC;
		unsigned char	NDHGFLG1,
				NDHGRCFM;
/* M */		unsigned short	NDHGLREC;
		unsigned char	NDHGDSCT,
				NDHGFCBI,
				NDHGLNCT;
		unsigned char	r2,		/* Reserved */
/* M */				NDHGFORM[8],
/* M */				NDHGFCB[8],
/* M */				NDHGUCS[8],
/* M */				NDHGXWTR[8],
				r3[8];
		unsigned char	NDHGFLG2,
				NDHGUCSO,
				r4[2],
/* M */				NDHGPMDE[8];
		};

struct	DATASET_HEADER_RSCS {		/* RSCS section */
		unsigned short	LENGTH_4;
		unsigned char	ID,
				MODIFIER;
		unsigned char	NDHVFLG1,
				NDHVCLAS,
				NDHVIDEV,
				NDHVPGLE,
				NDHVDIST[8],
				NDHVFNAM[12],
				NDHVFTYP[12];
		unsigned short	NDHVPRIO;
		unsigned char	NDHVVRSN,
				NDHVRELN;
#ifdef INCLUDE_TAG
		unsigned char	NDHVTAGR[136];
#endif
		};

struct	DATASET_HEADER {	/* Includes the General section and RSCS section */
		unsigned short	LENGTH;
		unsigned char	FLAG,
				SEQUENCE;
		struct DATASET_HEADER_G
				NDH;
		struct DATASET_HEADER_RSCS
				RSCS;
		};

struct	JOB_TRAILER {
		unsigned short	LENGTH;
		unsigned char	FLAG,
				SEQUENCE;
		unsigned short	LENGTH_4;
		unsigned char	ID,
				MODIFIER;
		unsigned char	NJTGFLG1,
				NJTGXCLS,
				r1[2];
		unsigned long	NJTGSTRT[2],
				NJTGSTOP[2],
				r2,
/* M */				NJTGALIN,
/* M */				NJTGCARD,
				r3;
		unsigned char	NJTGIXPR,
				NJTGAXPR,
				NJTGIOPR,
				NJTGAOPR;
		} ;


struct	SIGNON {		/* Initial and response */
		unsigned char	NCCRCB,
				NCCSRCB,
				NCCIDL,
/* M */				NCCINODE[8],
				NCCIQUAL;
		unsigned long	NCCIEVNT;
		unsigned short	NCCIREST,
/* M */				NCCIBFSZ;
/* M */		unsigned char	NCCILPAS[8],
/* M */				NCCINPAS[8],
				NCCIFLG;
		unsigned long	NCCIFEAT;
	};


/* Nodal messages records: */
struct	NMR_MESSAGE {		/* Message */
		unsigned char	NMRFLAG,
				NMRLEVEL,
				NMRTYPE,
				NMRML,
				NMRTONOD[8], NMRTOQUL,
				NMRUSER[8],
				NMRFMNOD[8], NMRFMQUL,
				NMRMSG[132];
	};
