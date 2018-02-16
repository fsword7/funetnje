$! NJE_TO_MX.COM - Once in a minutes look for NJE files destined to us. If found
$! submit them to the MX package.
$! Note: We rely on the fact that there is only one TO field in incoming messages.
$!
$! Local names: Change BITNET_NAME and INTERNET_NAME to hold your local names.
$!
$ BITNET_NAME = "KINERET"
$ INTERNET_NAME = "KINERET.HUJI.AC.IL"
$ MX_ENTER := $MX_EXE:MX_SITE_IN
$ SET PROCESS/NAME="NJE -> MX"
$!
$LOOP:
$ FILENAME = F$SEARCH("BITNET_ROOT:[QUEUE]ASC*.LOCAL;*")
$ IF FILENAME .EQS. "" THEN GOTO REST	! No files waiting for us.
$!
$! OPen the file, read its header.
$!
$ FROM = "***"		! Some initial value.
$ TO = "***"
$ OPEN/READ/ERROR=LOOP INFILE 'FILENAME'
$HEADER_LOOP:
$ READ/END=END_FILE INFILE RECORD
$ IF F$EXTRACT(0, 4, RECORD) .EQS. "FRM:" THEN -	! From field
	FROM = F$EXTRACT(5, 100, RECORD)
$ IF F$EXTRACT(0, 4, RECORD) .EQS. "TOA:" THEN -	! Receipient name
	TO = F$EXTRACT(5, 100, RECORD)
$ IF F$EXTRACT(0, 4, RECORD) .EQS. "END:" THEN GOTO END_HEADER
$ GOTO HEADER_LOOP
$!
$! Header done - append .BITNET to From and replace local BITnet name with Internet one.
$!
$END_HEADER:
$ FROM = FROM + ".BITNET"
$ LOCATION = F$LOCATE("@''BITNET_NAME'", TO)
$ IF LOCATION .LT. F$LENGTH(TO) THEN -	! It was found there
	TO = F$EXTRACT(0, LOCATION, TO) + "@''INTERNET_NAME'"
$!
$! Create a file with the receipient address
$!
$ OPEN/WRITE/ERROR=END_FILE OUTFILE BITNET_ROOT:[QUEUE]TEMP.ADR
$ WRITE OUTFILE "<''TO'>"
$ CLOSE OUTFILE
$!
$! Copy the contents to  a temporary file so it won't have our NJE header.
$!
$ OPEN/WRITE/ERROR=END_FILE OUTFILE BITNET_ROOT:[QUEUE]TEMP.TXT
$COPY_LOOP:
$ READ/END=END_FILE INFILE RECORD
$ WRITE OUTFILE RECORD
$ GOTO COPY_LOOP
$!
$! End of message. Close files, feed MX and delete message and temp files.
$!
$END_FILE:
$ CLOSE INFILE
$ CLOSE OUTFILE
$ MX_ENTER BITNET_ROOT:[QUEUE]TEMP.TXT BITNET_ROOT:[QUEUE]TEMP.ADR "''FROM'"
$ DELETE BITNET_ROOT:[QUEUE]TEMP.TXT;
$ DELETE BITNET_ROOT:[QUEUE]TEMP.ADR;
$ DELETE 'FILENAME'
$ GOTO LOOP
$!
$! End of files. Sleep one minute and try again.
$REST:
$ WAIT 00:01:00
$ GOTO LOOP
