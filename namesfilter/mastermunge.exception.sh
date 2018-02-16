#!/bin/sh
# Put the exception list here
# Be careful what you do here
# 1) awk - for any dot names that are not in .fi, override (#LOR)
# 2) sed - names that have interconnect.no (#IC.NO), make it valid
# 3) sed - override anything - most likely addition/deletion are done here
# 4) sed - absolute override
#	 - on UTORVM. all mailer utorant files are routed to mailer utorugw
awk '
{
	if (index($0, ".") > 0) {
		if (index($0, ".fi ") > 0)
			print $0
		else 
			printf("#LOR:%s\n", $0)
	} else {
		print $0
	}
}' |
sed -e '/#IC.NO:/s/^.*://' |
sed -e '/:.nl /s/^#.*://' |
sed -e '/!interbit$/s/^/#LOR:/' \
    -e '/^.uucp	/s/^/#LOR:/'
