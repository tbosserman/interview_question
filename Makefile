CFLAGS=-g -Wall
OS:=$(shell uname -s)

ifneq ($(OS), Darwin)
LIBS=libbsd.a
endif

exfil2: exfil2.o $(LIBS)

exfil2.o: exfil2.c

clean:
	$(RM) exfil2.o exfil2
