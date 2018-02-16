#!/bin/awk -f
#
# Checks bitnet.transport file for hosts with several different mailer
# definitions, as might be the case if CADOMAIN NAMES and XMAILER NAMES
# give different specs for the mailer on a particular host.
#
{	split($2,a,"!")
	if (a[3] == "") {
		host = a[2]
		mailer = a[1]
	} else {
		host = a[3]
		mailer = a[1] "!" a[2]
	}
	if (spec[host] == "")
		spec[host] = mailer
	else if (index(spec[host],mailer) == 0) {
		spec[host] = spec[host] " " mailer
		multi[host] = 1
	}
}
END	{
	for (host in multi)
		printf "%-10.10s%s\n", host ":", spec[host]
}
