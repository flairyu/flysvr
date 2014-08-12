objects = fsplugin.o \

CC = cc
CFLAGS = -shared -fPIC -O

fsplugin.so: $(objects)
	$(CC) -o fsplugin.so $(CFLAGS) $(objects)

fsplugin.o:fsplugin.h fstype.h
	$(CC) -c -o fsplugin.o $(CFLAGS) fsplugin.c

.PHONY:clean
clean:
	-rm $(objects)

