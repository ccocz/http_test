CC = gcc
CFLAGS = -Wall
TARGETS = testhttp_raw 

all: $(TARGETS) 

err.o: err.c err.h

testhttp_raw.o: testhttp_raw.c err.h

testhttp_raw: testhttp_raw.o err.o

clean:
	rm -f *.o *~ $(TARGETS)
