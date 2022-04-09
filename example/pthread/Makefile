RM = rm -f
CC = gcc
CFLAGS += -O2 -g -fpie
LDFLAGS += -static-pie -pthread
LDLIBS +=

all: pthread

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%: %.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

pthread: pthread.o

clean:
	$(RM) *.o *~ core pthread
