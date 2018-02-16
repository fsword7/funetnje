#####
#	Steps to routes.bitnet
#	1) Should make old copies of data files (make copy)
#	2) Get the latest FIDOMAIN.NAMES and XMAILER.NAMES
#	          files and make sure that toronto.names is correct.
#	3) Make sure that mastermunge.exception.sh is up to date.
#	4) Run 'make' and voila routes.bitnet file.
#	5) Make sure that there are no major errors reported from the 'make'.
#
#####
#  make - default is 'all' which is  routes.bitnet and consistcheck.
#	make routes.bitnet - runs mastermunge.sh which makes
#			 routes.bitnet file.
#	make consistcheck - runs mastercheck.sh which does some consistency
#			 check. Will make routes.bitnet if it is not
#			 up to date.
#	make copy - runs mastercopy.sh that agelog data files.
#	make clean - remove some temporary files.
#	
#
#  Some shell scripts that are used by scripts listed above:
#  checksysnames.sh - list multiple node entries in SYSNAMES
#  comparetransportvssysnames.sh - list undefined routes that are need for nodes
#			in routes.bitnet
#  format.sh* - used for formatting output for transport files
#  mastermunge.exception.sh* - contains common exception list
#  munge.sh* - generic script for munging *.NAMES files
#
#		
#		johncw 8908
#
#---------------------------------------
#
#to listserv@bitnic get xmailer names
#to netserv@canada01 get xmailer names
#to netserv@canada01 get cadomain names
#####
DIR=.

all: routes.bitnet consistcheck

##### routes.bitnet
#routes.bitnet: XMAILER.NAMES CADOMAIN.NAMES toronto.names
#	@mastermunge.sh
routes.bitnet: XMAILER.NAMES FIDOMAIN.NAMES funet.names
	@mastermunge.sh

#XMAILER.NAMES: $(DIR)/XMAILER.NAMES
#	rm -f $@.Z
#	compress $@
#	cp $(DIR)/$@ .

#CADOMAIN.NAMES: $(DIR)/CADOMAIN.NAMES
#	rm -f $@.Z
#	compress $@
#	cp $(DIR)/$@ .

toronto.names:
	@echo -n "Missing file " $@

##### consistcheck
consistcheck: .mastercheck

.mastercheck: routes.bitnet
	@mastercheck.sh
	@touch $@

##### copy
copy: .mastercopy

.mastercopy:
	@mastercopy.sh
	@touch $@

##### clean
clean:
	rm -f xmailer.names.out cadomain.names.out make.log make.out
