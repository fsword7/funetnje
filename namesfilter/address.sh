#!/bin/sh
PATH=/local/bin:/etc:/usr/etc:/bin:/usr/ucb:/usr/bin:
export PATH
awk '
{
	for (i = 1; i <= NF; i++) 
		if (index($i, "@" )> 0) 
			print $i
}
'
