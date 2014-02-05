linux: rtpgen.c
	cc -Wall -g -o rtpgen rtpgen.c  -lrt
osx: rtpgen.c
	cc -Wall -g -o rtpgen rtpgen.c
win32:
	cc -Wall -o rtpgen.exe rtpgen.c -D WIN32 -lwsock32

clean:
	rm -f rtpgen 
	rm -rf rtpgen.dSYM
