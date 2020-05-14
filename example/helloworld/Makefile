RM = rm -f
CC = gcc
CFLAGS += -O2 -g -fpie
LDFLAGS += -static-pie
LDLIBS +=

all: helloworld

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%: %.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

helloworld: helloworld.o

clean:
	$(RM) *.o *~ core helloworld
