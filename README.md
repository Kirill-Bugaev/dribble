# dribble

Usage: `dribble [OPTION]... PARTITION`

Write charecter specified by `-b` option ('\0' by default) in file
specified by `-h` option ('.hole' by default) on `PARTITION` each time
interval (in seconds) specified by `-p` option (60 by default).

	-b CHARCODE		ball (written value), oct. 0-377
	-h FILENAME		hole (name of file which will be written)
	-p INTEGER		pause
	-u				specified uuid instead of device label
	-d 			run as daemon
	-v			print verbose messages
	--			display this help and exit
