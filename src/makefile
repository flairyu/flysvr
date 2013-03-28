objects = flysvr.o \

CC = cc
CFLAGS = -O -rdynamic -s -ldl 

flysvr: $(objects)
	$(CC) -o flysvr $(CFLAGS) $(objects)

flysvr.o:flysvr.h fstype.h

.PHONY:clean
clean:
	-rm $(objects)


