objects = fsplugin.o \

CC = cc
CFLAGS = -shared -fPIC -O

targs = kos.so

$(targs): $(objects)
	$(CC) -o $(targs) $(CFLAGS) $(objects)

fsplugin.o:fsplugin.c fsplugin.h fstype.h
	$(CC) -c -o fsplugin.o $(CFLAGS) fsplugin.c

.PHONY:clean
clean:
	-rm $(objects) $(targs)

