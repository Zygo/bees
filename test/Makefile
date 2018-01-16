PROGRAMS = \
	chatter \
	crc64 \
	fd \
	limits \
	path \
	process \

all: test

test: $(PROGRAMS)
	set -x; for prog in $(PROGRAMS); do ./$$prog || exit 1; done

include ../makeflags

LIBS = -lcrucible -lpthread
LDFLAGS = -L../lib -Wl,-rpath=$(shell realpath ../lib)

depends.mk: *.cc
	for x in *.cc; do $(CXX) $(CXXFLAGS) -M "$$x"; done > depends.mk.new
	mv -fv depends.mk.new depends.mk

-include depends.mk

%.o: %.cc %.h ../makeflags
	-echo "Implicit rule %.o: %.cc" >&2
	$(CXX) $(CXXFLAGS) -o "$@" -c "$<"

%: %.o ../makeflags
	-echo "Implicit rule %: %.o" >&2
	$(CXX) $(CXXFLAGS) -o "$@" "$<" $(LDFLAGS) $(LIBS)

clean:
	-rm -fv *.o
