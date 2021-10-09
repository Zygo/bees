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
	task \

all: test

test: $(PROGRAMS:%=%.txt) Makefile
FORCE:

include ../makeflags
-include ../localconf

LIBS = -lcrucible -lpthread
BEES_LDFLAGS = -L../lib $(LDFLAGS)

.depends:
	mkdir -p $@

.depends/%.dep: %.cc tests.h Makefile | .depends
	$(CXX) $(BEES_CXXFLAGS) -M -MF $@ -MT $(<:.cc=.o) $<

depends.mk: $(PROGRAMS:%=.depends/%.dep)
	cat $^ > $@.new
	mv -f $@.new $@

include depends.mk

$(PROGRAMS:%=%.o): %.o: %.cc ../makeflags Makefile
	$(CXX) $(BEES_CXXFLAGS) -o $@ -c $<

$(PROGRAMS): %: %.o ../makeflags Makefile ../lib/libcrucible.a
	$(CXX) $(BEES_CXXFLAGS) $(BEES_LDFLAGS) -o $@ $< $(LIBS)

%.txt: % Makefile FORCE
	./$< >$@ 2>&1 || (RC=$$?; cat $@; exit $$RC)

clean:
	rm -fv $(PROGRAMS:%=%.o) $(PROGRAMS:%=%.txt) $(PROGRAMS)
