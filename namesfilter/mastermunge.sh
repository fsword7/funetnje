#!/bin/sh
PATH=/usr/local/bin:/etc:/usr/etc:/bin:/usr/ucb:/usr/bin:
export PATH
# Updated August 89 Johnny Chee Wah for zmailer routes.bitnet file
# Updated February 91 Matti Aarnio - for finnish node FINFILES.
# 
# This is an attempt to create routes.bitnet. Since this is an attempt,
# please try to understand this script. One may need to update CA manually
# or update the shell script.
#
# Make sure that you have XMAILER.NAMES. You may want to compare with old copy
# which you had copied into the OLD directory.
echo '***'
echo "### doing XMAILER.NAMES to xmailer.names.out."
time munge.sh < XMAILER.NAMES | mastermunge.exception.sh | \
		sort > xmailer.names.out
echo "*** DONE xmailer.names.out ***"
echo '***'
#
# Make sure you have FIDOMAIN.NAMES. You may want to compare with old copy
# which you had copied into the OLD directory.
#	You may want to compare things to the FI registery file.
#
# Should check make sure that #OTR does not exist
echo '***'
echo "### doing FIDOMAIN.NAMES to fidomain.names.out."
echo "***      FIDOMAIN.NAMES should already contains DOMAIN.NAMES as standard"
echo "***      with .FI default removed and .uucp override to FUUG.FI added"
time munge.sh < FIDOMAIN.NAMES | 
	mastermunge.exception.sh | format.sh | sort > fidomain.names.out
echo "*** DONE fidomain.names.out ***"
echo '***'
#
#
echo '***'
echo "### Merging xmailer.names.out, fidomain.names.out and funet.names"
echo "*** to routes.bitnet."
echo '***     toronto.names contains the local entries bitnet entry '
echo '***     should keep it up to date and include it in hosts.transport.'
echo '***     This should be good to include this in routes.bitnet'
echo '***     and keep hosts.transport smaller?'
time cat xmailer.names.out fidomain.names.out funet.names | \
		format.sh | sort -u > routes.bitnet
echo "*** DONE routes.bitnet ***"
echo '***'
