TAG := $(shell git describe --always --dirty || echo UNKNOWN)

default: libcrucible.so
%.so: Makefile

CRUCIBLE_OBJS = \
	chatter.o \
	cleanup.o \
	crc64.o \
	error.o \
	extentwalker.o \
	fd.o \
	fs.o \
	ntoa.o \
	path.o \
	process.o \
	string.o \
	task.o \
	time.o \
	uuid.o \
	.version.o \

libcrucible.so: $(CRUCIBLE_OBJS)

include ../makeflags

depends.mk: *.cc
	for x in $^; do $(CXX) $(CXXFLAGS) -M -MG -MT "$${x/%.cc/.o}" "$$x"; done > $@.new
	mv -fv $@.new $@

.version.cc: Makefile ../makeflags *.cc ../include/crucible/*.h
	echo "namespace crucible { const char *VERSION = \"$(TAG)\"; }" > .version.new.cc
	mv -f .version.new.cc .version.cc

include depends.mk

%.o: %.cc ../makeflags
	$(CXX) $(CXXFLAGS) -fPIC -o $@ -c $<

%.so:
	$(CXX) $(LDFLAGS) -fPIC -o $@ $^ -shared -Wl,-soname,$@ -luuid
