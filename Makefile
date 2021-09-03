#
# makefile
SRCS=c_hiccup.c hdr_histogram.c
CFLAGS=-g -Wall
LIBS=-lrt -lm 

all: c_hiccup

c_hiccup: $(SRCS)
	gcc $(CFLAGS) -o c_hiccup $(SRCS) $(LIBS)

clean: 
	rm -f c_hiccup *~ *.o

install:  c_hiccup
	cp -f c_hiccup ${DESTDIR}/usr/local/bin