#!/bin/sh
PATH=/usr/local/bin:/etc:/usr/etc:/bin:/usr/ucb:/usr/bin:
export PATH
# this script is used to munge *.NAMES files for zmailer bitnet.transport
#	file format. It make no real attempt to check syntax.
#	The script is not very efficient but does the job.
#
# To do: 1) does not check all tag out ; only use the ones that we need
#	 2) we may want to check "interconnect" and "served" tags.
#	 3) need to configure the data the way we want it
# Currently the tags that are important are
#	:nick - host/domain
#	:alias - obsolete but is still there for some machines
#	:mailer
#	:interconnect - if missing then assumed no
#
# For explanation of the tags used, see DOMAIN.GUIDE
# format of :nick tag is :nick.hostname
# format of :mailer tag is
#		:mailer.[vmid[@node] [exit type [parm]]]
#	if :mailer is blank, then set up a default outgoing entry of
#		hostname hostname ? DEFRT 1
#	if :mailer.vmid is given then set up
#		hostname hostname vmid BITNET 2
#	if :mailer.vmid@node then
#		hostname node vmid BITNET 2
#	else use the given info to fill in the [exit type [parm]] part.
#            but vmid=none is assume to be nomail????
#####
#
# When :mailer is non-blank and has no @node , generate an incoming
#	entry of : hostname vmid

# remove comments | convert : to nl | remove null lines | uc 2 lc | awk munge
sed -e '/^:\*/d' | tr ':' '\012' | \
sed -e '/^$/d' -e 's/^ *//' -e 's/  *$//' | tr A-Z a-z  | \
awk '
/^nick\./ {
		nickname = substr($0, 6, length($0))
		if (nick[nickname] != "") {
			printf("nick '%s' was defined before\n", nickname) \
				| "cat 1>&2"
			next
		}
		nick[nickname] = nickname
	}

/^mailer\./	{
		mailer = ""
		mailerstring = substr($0, 8, length($0))
# get vmid
		if (mailerstring == "") {
			vmid = ""
		} else if ((there = index(mailerstring,"@")) > 0) {
			vmid = substr(mailerstring, 1, there-1)
			mailerstring = substr(mailerstring, there+1)
# there is an at sign so find node
			node = ""
			if ((there = index(mailerstring," ")) > 0) {
				node = substr(mailerstring, 1, there-1)
				mailerstring = substr(mailerstring, there+1)
				if (mailerstring == "")
					if (nickname == "") {
	    printf("Problem mailer defined before nick? line %d:\n\t%s\n", \
							 NR, $0) | "cat 1>&2"
						exit
					} else {
						node = nickname
					}
			} else {
				node = mailerstring
				mailerstring = ""
			}
		} else if ((there = index(mailerstring," ")) > 0) {
			vmid = substr(mailerstring, 1, there-1)
			node = nickname
			mailerstring = substr(mailerstring, there+1)
		} else {
			vmid = mailerstring
			if (mailerstring == "none") { 	# geesh another def for nomail?
				mailer = "nomail2"
				mailerstring = "nomail 2"
			} else {
				# i.e. mailer.MAILER then assume bsmtp3?
				mailerstring = "bsmtp 3"
				node = nickname # assumption true then no @node,
					# assume nickname
			}
		}
# get [exit type [parm]]
		argc = split(mailerstring, m_a, " ")
		unknown = 1
		if (argc == 0) {
			mailer = "defrt1"
			node = nickname
			unknown = 0
		} else if (argc == 3) {
			if (m_a[1] == "defrt" && m_a[2] == "1" \
					&& m_a[3] == "truncate") {
				mailer = "defrt1truncate"
				unknown = 0
			} else if (m_a[1] == "bsmtp" && \
				m_a[2] == "3" && m_a[3] == "truncate") {
				mailer = "bsmtp3truncate"
				unknown = 0
			} else if (m_a[1] == "bitnet" &&  \
				m_a[2] == "2" && m_a[3] == "deliver2") {
				mailer = "bitnet2deliver2"  
				unknown = 0
			} else if (m_a[1] == "nomail" && m_a[2] == "2") {
				mailer = "nomail2"
				unknown = 0
			}
		} else if (argc == 2) {
			if (m_a[1] == "defrt" && m_a[2] == "1") {
				mailer = "defrt1"
				unknown = 0
			} else if (m_a[1] == "bsmtp" && m_a[2] == "3") {
				mailer = "bsmtp3"
				unknown = 0
			} else if (m_a[1] == "bitnet" && m_a[2] == "2") {
				mailer = "bitnet2"
				unknown = 0
			} else if (m_a[1] == "nomail" && m_a[2] == "2") {
				mailer = "nomail2"
				unknown = 0
			}
		}
		if (unknown == 1) {
printf("Problem unknown mailer in line %d of preprocess file:\n\t%s\n", \
				 NR, $0) | "cat 1>&2"
			exit
		}
		mailertype[nickname] = mailer
		vmidname[nickname] = vmid
		mailnode[nickname] = node
		next
	}

/^site\./ {
		site = substr($0, 6, length($0))
		next
	}

/^gatemast\./ {
		gatemasters = substr($0, 10, length($0))
		next
	}

/^tech-mailbox\./ {
		techmailbox = substr($0, 14, length($0))
		next
	}

/^interconnect\./ {
		interconnecttype = substr($0, 14, length($0))
		if (interconnect[nickname] != "") {
			printf("%s interconnect '%s' defined before as %s\n", \
				nickname, interconnecttype, \
				interconnect[nickname]) \
				| "cat 1>&2"
			next
		}
		interconnect[nickname] = interconnecttype
		next
	}

/^served\./ {	# do not really use this since interconnect is not defined
		# when this is defined and it is yes.
		servedtype = substr($0, 8, length($0))
		if (served[nickname] != "") {
			printf("%s served '%s' defined before as %s\n", \
				nickname, interconnecttype, \
				interconnect[nickname]) \
				| "cat 1>&2"
			next
		}
		served[nickname] = servedtype
		next
	}

/^alias\./ {
		aliasname = substr($0, 7, length($0))
		if (alias[nickname] != "") {
			printf("nick %s alias '%s' defined before as %s\n", \
				nickname, aliasname, alias[nickname]) \
				| "cat 1>&2"
			next
		}
		alias[nickname] = aliasname
		next
	}

/^net\./ {
		net = substr($0, 5, length($0))
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
		printf("invalid tag line '%s'\n", $0) | "cat 1>&2"
		next
	}

END { 
	for (nodeentry in nick) { # create normal stuff
		# skip machines that do not want mail
		if (mailertype[nodeentry] == "nomail2")
		 	continue

		if (interconnect[nodeentry] == "")
			interconnect[nodeentry] = "no"

		if (interconnect[nodeentry] == "no") {
			# set up to which machine or domain
			if (alias[nodeentry] != "" && \
				alias[nodeentry] != nick[nodeentry])
				if (vmidname[nodeentry] == "" || \
						index(mailertype[nodeentry], "defrt1") > 0)
					printf("#IC.NO:%.30s %s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						mailnode[nodeentry])
				else
					printf("#IC.NO:%.30s %s!%s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						vmidname[nodeentry], \
						mailnode[nodeentry])
			if (vmidname[nodeentry] == "" || \
				index(mailertype[nodeentry],"defrt1") > 0) 
				printf("#IC.NO:%.30s %s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					mailnode[nodeentry])
			else
				printf("#IC.NO:%.30s %s!%s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					vmidname[nodeentry], \
					mailnode[nodeentry])
			continue
		}
		
		if (interconnect[nodeentry] == "mx")  {
			# set up to which machine or domain
			if (alias[nodeentry] != "" && \
				alias[nodeentry] != nick[nodeentry])
				if (vmidname[nodeentry] == "" || \
					index(mailertype[nodeentry], "defrt1") > 0)
					printf("%.30s %s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						mailnode[nodeentry])
				else
					printf("%.30s %s!%s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						vmidname[nodeentry], \
						mailnode[nodeentry])
			if (vmidname[nodeentry] == "" || \
				index(mailertype[nodeentry],"defrt1") > 0) 
				printf("%.30s %s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					mailnode[nodeentry])
			else
				printf("%.30s %s!%s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					vmidname[nodeentry], \
					mailnode[nodeentry])
			continue
		}
		
# if there machines are on the internet then forget them,
# well just comment them out for now so that we can
# override if we need to, by uncommenting.
		if (interconnect[nodeentry] == "yes")  {
			# set up to which machine or domain
			if (alias[nodeentry] != "" && \
				alias[nodeentry] != nick[nodeentry])
				if (vmidname[nodeentry] == "" || \
					index(mailertype[nodeentry], "defrt1") > 0)
					printf("#IC.Y:%.30s %s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						 mailnode[nodeentry])
				else
					printf("#IC.Y:%.30s %s!%s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						vmidname[nodeentry], \
						mailnode[nodeentry])
			if (vmidname[nodeentry] == "" || \
				index(mailertype[nodeentry],"defrt1") > 0) 
				printf("#IC.Y:%.30s %s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					mailnode[nodeentry])
			else
				printf("#IC.Y:%.30s %s!%s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					vmidname[nodeentry], \
					mailnode[nodeentry])
			continue
		}
		
		# other(OTR) than MX, YES, NO
		{
			# set up to which machine or domain
			if (alias[nodeentry] != "" && \
				alias[nodeentry] != nick[nodeentry])
				if (vmidname[nodeentry] == "" || \
					index(mailertype[nodeentry], "defrt1") > 0)
					printf("#IC.OTR:%.30s %s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
					 	mailnode[nodeentry])
				else
					printf("#IC.OTR:%.30s %s!%s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						vmidname[nodeentry], \
						mailnode[nodeentry])
			if (vmidname[nodeentry] == "" || \
				index(mailertype[nodeentry],"defrt1") > 0) 
				printf("#IC.OTR:%.30s %s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
				 	mailnode[nodeentry])
			else
				printf("#IC.OTR:%.30s %s!%s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					vmidname[nodeentry], \
					mailnode[nodeentry])
			continue
		}
	} # for

# domain names "sub.domain" is actually .sub.domain and sub.domain for zmailer
	for (nodeentry in nick) { # create .names for subdomain
		dot = index(nick[nodeentry],".")
		if ((dot == 1) || (dot == 0))
			continue
		# skip machines that do not want mail
		if (mailertype[nodeentry] == "nomail2")
		 	continue

		if (interconnect[nodeentry] == "")
			interconnect[nodeentry] = "no"

		if (interconnect[nodeentry] == "no") {
			# set up to which machine or domain
			if (alias[nodeentry] != "" && \
				alias[nodeentry] != nick[nodeentry])
				if (vmidname[nodeentry] == "" || \
					index(mailertype[nodeentry], "defrt1") > 0)
					printf("#IC.NO:.%.30s %s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						mailnode[nodeentry])
				else
					printf("#IC.NO:.%.30s %s!%s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						vmidname[nodeentry], \
						mailnode[nodeentry])
			if (vmidname[nodeentry] == "" || \
				index(mailertype[nodeentry],"defrt1") > 0) 
				printf("#IC.NO:.%.30s %s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					mailnode[nodeentry])
			else
				printf("#IC.NO:.%.30s %s!%s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					vmidname[nodeentry], \
					mailnode[nodeentry])
			continue
		}
		
		if (interconnect[nodeentry] == "mx") {
			# set up to which machine or domain
			if (alias[nodeentry] != "" && \
				alias[nodeentry] != nick[nodeentry])
				if (vmidname[nodeentry] == "" || \
					index(mailertype[nodeentry], "defrt1") > 0)
					printf(".%.30s %s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						mailnode[nodeentry])
				else
					printf(".%.30s %s!%s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						vmidname[nodeentry], \
						mailnode[nodeentry])
			if (vmidname[nodeentry] == "" || \
				index(mailertype[nodeentry],"defrt1") > 0) 
				printf(".%.30s %s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					mailnode[nodeentry])
			else
				printf(".%.30s %s!%s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					vmidname[nodeentry], \
					mailnode[nodeentry])
			continue
		}
		
# if there machines are on the internet then forget them,
# well just comment them out for now so that we can
# override if we need to, by uncommenting.
		if (interconnect[nodeentry] == "yes")  {
			# set up to which machine or domain
			if (alias[nodeentry] != "" && \
				alias[nodeentry] != nick[nodeentry])
				if (vmidname[nodeentry] == "" || \
					index(mailertype[nodeentry], "defrt1") > 0)
					printf("#IC.Y:.%.30s %s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						mailnode[nodeentry])
				else
					printf("#IC.Y:.%.30s %s!%s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						vmidname[nodeentry], \
						mailnode[nodeentry])
			if (vmidname[nodeentry] == "" || \
				index(mailertype[nodeentry],"defrt1") > 0) 
				printf("#IC.Y:.%.30s %s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					mailnode[nodeentry])
			else
				printf("#IC.Y:.%.30s %s!%s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					vmidname[nodeentry], \
					mailnode[nodeentry])
			continue
		}
		
		# other(OTR) than MX, YES, NO
		{
			# set up to which machine or domain
			if (alias[nodeentry] != "" && \
				alias[nodeentry] != nick[nodeentry])
				if (vmidname[nodeentry] == "" || \
					index(mailertype[nodeentry], "defrt1") > 0)
					printf("#IC.OTR:.%.30s %s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
					 	mailnode[nodeentry])
				else
					printf("#IC.OTR:.%.30s %s!%s!%s\n", alias[nodeentry], \
						mailertype[nodeentry], \
						vmidname[nodeentry], \
						mailnode[nodeentry])
			if (vmidname[nodeentry] == "" || \
				index(mailertype[nodeentry],"defrt1") > 0) 
				printf("#IC.OTR:.%.30s %s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
				 	mailnode[nodeentry])
			else
				printf("#IC.OTR:.%.30s %s!%s!%s\n", nick[nodeentry], \
					mailertype[nodeentry], \
					vmidname[nodeentry], \
					mailnode[nodeentry])
			continue
		}
	} # for
} # END
'
