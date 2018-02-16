#!/bin/sh
# this formats non comment lines and then sorts it with comments intact.
# 	comments start with '#' character in the first column
expand |
awk '
{
	if (NF == 2)
		printf("%-31s %s\n", $1, $2)
	else
	if (NF == 3)
		printf("%-31s %s %s\n", $1, $2, $3)
}
' | unexpand -a
