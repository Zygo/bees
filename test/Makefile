PROGRAMS = \
	chatter \
	crc64 \
	fd \
	limits \
	namedptr \
	path \
	process \
	progress \
	seeker \
	table \
	task \

all: test

test: $(PROGRAMS:%=%.txt) Makefile
FORCE:

include ../makeflags
-include ../localconf

LIBS = -lcrucible -lpthread -latomic
BEES_LDFLAGS = -L../lib $(LDFLAGS)

%.dep: %.cc tests.h Makefile
	$(CXX) $(BEES_CXXFLAGS) -M -MF $@ -MT $(<:.cc=.o) $<

include $(PROGRAMS:%=%.dep)

$(PROGRAMS:%=%.o): %.o: %.cc ../makeflags Makefile
	$(CXX) $(BEES_CXXFLAGS) -o $@ -c $<

$(PROGRAMS): %: %.o ../makeflags Makefile ../lib/libcrucible.a
	$(CXX) $(BEES_CXXFLAGS) $(BEES_LDFLAGS) -o $@ $< $(LIBS)

%.txt: % Makefile FORCE
	./$< >$@ 2>&1 || (RC=$$?; cat $@; exit $$RC)

clean:
	rm -fv $(PROGRAMS:%=%.o) $(PROGRAMS:%=%.txt) $(PROGRAMS)
