CC = gcc
CC_OPTS = -Wall -O2 
GOAL = scalpel

CC += $(CC_OPTS) 
  .c.o: 
	$(CC) -c $<

HEADER_FILES = scalpel.h prioque.h dirname.h
SRC =  helpers.c files.c scalpel.c dig.c prioque.c base_name.c
OBJS =  helpers.o scalpel.o files.o dig.o prioque.o base_name.o

all: linux

linux: CC += -D__LINUX 
linux: $(GOAL)

bsd: CC += -D__OPENBSD 
bsd: $(GOAL)

win32: CC += -D__WIN32 -Ic:\PThreads\include 
win32: $(SRC) $(HEADER_FILES)
	$(CC) -o $(GOAL).exe $(SRC) -liberty -Lc:\PThreads\lib -lpthreadGC1

$(GOAL): $(OBJS) 
	$(CC) -o $(GOAL) $(OBJS) -lm

scalpel.o: scalpel.c $(HEADER_FILES) Makefile
dig.o: dig.c $(HEADER_FILES) Makefile
helpers.o: helpers.c $(HEADER_FILES) Makefile
files.o: files.c $(HEADER_FILES) Makefile
prioque.o: prioque.c prioque.h Makefile

nice:
	rm -f *~
	rm -rf scalpel-output

clean: nice
	rm -f $(OBJS) $(GOAL) $(GOAL).exe core *.core
