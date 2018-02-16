#!/bin/sh
#
#  NETINIT processor
#
#  This is run once a day, when new data is available, it is processed
#  straight on.
#

#N=-n
N=
NJEDIR=/usr/local/lib/huji
export N NJEDIR

PATH=/usr/local/bin:/usr/local/etc:$PATH
export PATH

cd /usr/local/lib/mail/namesfilter

qrdr -u NETINIT -l |		\
(   while read  fname datatype src dest fn ft junk1 class junk 
    do {
	if [ "$datatype" = "NETDATA" ] ; then
	    if [ "$fn.$ft" = "FINFILES.NETINIT" ]; then
		echo "FINFILES.NETINIT: " $fname $datatype \
			$src $dest $fn.$ft $class
		ndparse $N -a -o ${NJEDIR}/finfiles.netinit $fname	&& \
		{   # Received most successfully the NETINIT file
		    echo "Ndparse ok, generating routes.."
		    (cd ${NJEDIR} ; ./njeroutes finfiles.header finfiles.netinit finfiles.routes)
		    # Routing regenerated, order route database reopen
		    ucp rescan route
		}
		
	    elif [ "$fn.$ft" = "XMAILER.NAMES" ]; then
		echo "XMAILER.NAMES: " $fname $datatype $src \
			$dest $fn.$ft $class

		ndparse $N -a -o XMAILER.NAMES $fname		&& \
		{   # Received XMAILER.NAMES successfully.
		    make
		    cp routes.bitnet ../db
		    /etc/zmailer router
		}
	    elif [ "$fn.$ft" = "DOMAIN.NAMES" ]; then
		echo "DOMAIN.NAMES: " $fname $datatype $src \
			$dest $fn.$ft $class

		ndparse $N -a -o DOMAIN.NAMES $fname		&& \
		{   # Received DOMAIN.NAMES successfully.
		    rm routes.bitnet
		    make
		    cp routes.bitnet ../db
		    (cd ../db; ../bin/makendbm routes.bitnet)
		    /etc/zmailer router
		}
	    else
		echo "Unknown ND file for NETINIT: " $fname \
			$datatype $src $dest $fn.$ft $class
	    fi
	else
	    echo "Non ND file for NETINIT: " $fname $datatype \
		 $src $dest $fn.$ft $class
	fi ;
    } done
)

exit 0
