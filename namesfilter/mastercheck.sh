#!/bin/sh
PATH=/usr/local/bin:/etc:/usr/etc:/bin:/usr/ucb:/usr/bin:
export PATH
#
TMPFILE=/tmp/routes.bitnet$$;
echo '***'
echo "### NOW FOR SOME CHECKS"
echo "1) Check that there are not more than one of the same node/name defined"
time cat routes.bitnet | \
	sed -e '/^#/d' -e 's/	.*$//' | uniq -d > $TMPFILE
#
if test -s "$TMPFILE"
then
	echo "***** PROBLEM PROBLEM PROBLEM *****"
	echo "* the following names are defined more than once"
	cat $TMPFILE
	echo "* end of multiple name list"
else
	echo "okay no problem with multiple names/nodes"
fi
echo '***'
rm -f $TMPFILE
echo "2) Check that there are no #OTR in the routes.bitnet (see munge.sh)"
echo "   basically if defined then there is interconnect tag that we do not"
echo "   know about"
time grep '#OTR' routes.bitnet > $TMPFILE
#
if test -s "$TMPFILE"
then
	echo "***** PROBLEM PROBLEM PROBLEM *****"
	echo "* the following lines have OTR defined"
	cat $TMPFILE
	echo "* end of OTR list"
else
	echo "okay no OTR encountered"
fi
echo '***'
rm -f $TMPFILE
echo "3) Check that SYSNAMES do not have multiple entries for one node"
time ./checksysnames.sh > $TMPFILE
#
if test -s "$TMPFILE"
then
	echo "***** PROBLEM PROBLEM PROBLEM *****"
	echo "* the following nodes are defined more than once in NETINIT"
	cat $TMPFILE
	echo "* end of NETINIT list"
else
	echo "okay no multiple node defined in NETINIT"
fi
echo '***'
rm -f $TMPFILE
echo "4) Check that each routes.bitnet entry has corresponding NETINIT entry"
time ./comparetransportvssysnames.sh > $TMPFILE
#
if test -s "$TMPFILE"
then
	echo "***** PROBLEM PROBLEM PROBLEM *****"
	cat $TMPFILE
	echo "* end of routes.bitnet vs NETINIT list"
else
	echo "okay all routes.bitnet nodes defined in NETINIT"
fi
echo '***'
rm -f $TMPFILE
#
