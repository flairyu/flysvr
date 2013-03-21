objects = flysvr.o \

CC = cc
CFLAGS = -rdynamic -s -ldl 

flysvr: $(objects)
	$(CC) -o flysvr $(CFLAGS) $(objects)

flysvr.o:flysvr.h fstype.h

.PHONY:clean
clean:
	-rm $(objects)

