#!/bin/sh
PATH=/usr/local/bin:/etc:/usr/etc:/bin:/usr/ucb:/usr/bin:
export PATH
TRANSPORT=/tmp/transport$$
SYSNAMES=/tmp/sysnames$$

cat routes.bitnet | \
awk '
BEGIN	{ FS = "!" }
	{ print $NF }	# print the last name which should be the node
' |  tr A-Z a-z | sort -u > $TRANSPORT

cat /usr/local/lib/huji/NETINIT | \
awk '
BEGIN	{ FS = " " }
	{ if (substr($0,1,1) != "*" )
		print $2 }
' |  tr A-Z a-z | sort -u > $SYSNAMES

( echo '#netinit#'; cat $SYSNAMES; echo '#transport#'; cat $TRANSPORT) |
awk '
BEGIN			{ readthis = 0 }
$1 == "#netinit#"	{ readthis = 1 ; next }
$1 == "#transport#"	{ readthis = 2 ; next }
readthis == 1		{
				sysnames[$1] = $1
				next
			}
readthis == 2 		{ 
				if (sysnames[$1] == "") {
		printf "Node %s defined in routes.bitnet but ", $1
		printf "not in NETINIT\n"
					next
				}
			}
'
rm -f $TRANSPORT $SYSNAMES
