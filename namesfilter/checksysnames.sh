#!/bin/sh
PATH=/usr/local/bin:/etc:/usr/etc:/bin:/usr/ucb:/usr/bin:
export PATH

#
#  NETINIT (ala IBM RSCS v1) data studied
#

cat /usr/local/lib/huji/NETINIT | \
awk '
BEGIN	{ FS = " " }
	{ if (substr($0,1,1) != "*" )
		print $2 }
' |  tr A-Z a-z | sort | uniq -d
