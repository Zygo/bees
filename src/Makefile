PROGRAMS = \
	../bin/bees \
	../bin/fiemap \
	../bin/fiewalk \

all: $(PROGRAMS) depends.mk

include ../makeflags

LIBS = -lcrucible -lpthread
LDFLAGS = -L../lib -Wl,-rpath=$(shell realpath ../lib)

depends.mk: Makefile *.cc
	for x in *.cc; do $(CXX) $(CXXFLAGS) -M "$$x"; done > depends.mk.new
	mv -fv depends.mk.new depends.mk

bees-version.c: Makefile *.cc *.h
	echo "const char *BEES_VERSION = \"$(shell git describe --always --dirty || echo UNKNOWN)\";" > bees-version.new.c
	mv -f bees-version.new.c bees-version.c

-include depends.mk

%.o: %.cc %.h
	$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

../bin/%: %.o
	@echo Implicit bin rule "$<" '->' "$@"
	$(CXX) $(CXXFLAGS) -o "$@" "$<" $(LDFLAGS) $(LIBS)

BEES_OBJS = \
	bees.o \
	bees-context.o \
	bees-hash.o \
	bees-resolve.o \
	bees-roots.o \
	bees-thread.o \
	bees-types.o \
	bees-version.o \

../bin/bees: $(BEES_OBJS)
	$(CXX) $(CXXFLAGS) -o "$@" $(BEES_OBJS) $(LDFLAGS) $(LIBS)

clean:
	-rm -fv bees-version.h
	-rm -fv *.o bees-version.c
