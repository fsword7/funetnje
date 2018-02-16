#!/bin/sh
PATH=/local/bin:/etc:/usr/etc:/bin:/usr/ucb:/usr/bin:
export PATH
# remove comments | convert : to nl | remove null lines | uc 2 lc | awk munge
sed -e '/^:\*/d' | tr ':' '\012' | \
sed -e '/^$/d' -e 's/^ *//' -e 's/  *$//' | tr A-Z a-z  | \
awk '
/^node\./ {
		nodename = substr($0, 6, length($0))
		netnorth = 0
	}


/^net\.netnorth/ {
		netnorth = 1
		next
	}

/^acontact/ {
		if (netnorth == 1)
		print substr($0,index($0,".")+1)
		next
	}

/^bitdirector\./ {
		if (netnorth == 1)
		print substr($0,index($0,".")+1)
		next
	}

/^contact\./ {
		if (netnorth == 1)
		print substr($0,index($0,".")+1)
		next
	}

/^director\./ {
		if (netnorth == 1)
		print substr($0,index($0,".")+1)
		next
	}

/^inforep\./ {
		if (netnorth == 1)
		print substr($0,index($0,".")+1)
		next
	}

/^inform/ {
		if (netnorth == 1)
		print substr($0,index($0,".")+1)
		next
	}

/^linkfail\./ {
#		if (netnorth == 1)
#		print substr($0,index($0,".")+1)
		next
	}

/^postmast\./ {
		if (netnorth == 1)
		print substr($0,index($0,".")+1)
		next
	}

/^techrep\./ {
		if (netnorth == 1)
		print substr($0,index($0,".")+1)
		next
	}

/^tech-mailbox\./ {
		if (netnorth == 1)
		print substr($0,index($0,".")+1)
		next
	}

/^netsoft\./ {
		netsoft = substr($0, 9, length($0))
		next
	}

/^*/ {
		comment = substr($0, 2, length($0))
		next
	}

/./ {
		next
	}

'
