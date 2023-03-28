RM = rm -f
CC = gcc
CFLAGS += -O2 -g -fpie
LDFLAGS += -pie
LDFLAGS_STATIC += -static-pie

all: helloworld helloworld_static

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

helloworld: helloworld.o
	$(CC) $(LDFLAGS) $^ -o $@

helloworld_static: helloworld.o
	$(CC) $(LDFLAGS_STATIC) $^ -o $@

clean:
	$(RM) *.o *~ core helloworld helloworld_static
