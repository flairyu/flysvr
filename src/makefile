objects = flysvr.o \

CC = cc
CFLAGS = 


flysvr: $(objects)
	$(CC) -o flysvr $(CFLAGS) $(objects)

flysvr.o:flysvr.h

.PHONY:clean
clean:
	-rm $(objects)

