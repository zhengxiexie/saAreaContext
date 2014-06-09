CC      = gcc
CFLAGS  = -O0 -m64 -g3 -D_THREAD_SAFE_ -D_REENTRANT_ -Werror -Wall -D_DEBUG -D__DEBUG__
LDFLAGS = -O0 -m64 -g3 -lpthread

OBJECTS = config.o context.o reader-thread.o main.o comsumer-thread.o log.o

.PHONY: clean all

all:: $(OBJECTS)
	$(CC) $(LDFLAGS) -o saAreaContext $(OBJECTS)
	-mkdir -p ../bin
	mv saAreaContext ../bin

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

config.o: config.c config.h log.h
context.o: context.c config.h context.h log.h reader-thread.h
reader-thread.o: reader-thread.c reader-thread.h context.h comsumer-thread.h config.h log.h
comsumer-thread.o: comsumer-thread.c comsumer-thread.h context.h config.h log.h
main.o: main.c comsumer-thread.h context.h config.h log.h reader-thread.h
log.o: log.c log.h

install:: all
	cp ../bin/saAreaContext ../../../../bin/c

clean:
	-rm -f *.o ../bin/*

