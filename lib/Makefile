TAG := $(shell git describe --always --dirty || echo UNKNOWN)

default: libcrucible.so

OBJS = \
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

include ../makeflags

depends.mk: *.cc
	for x in *.cc; do $(CXX) $(CXXFLAGS) -M "$$x"; done > depends.mk.new
	mv -fv depends.mk.new depends.mk

.version.cc: Makefile ../makeflags *.cc ../include/crucible/*.h
	echo "namespace crucible { const char *VERSION = \"$(TAG)\"; }" > .version.new.cc
	mv -f .version.new.cc .version.cc

-include depends.mk

%.o: %.cc ../include/crucible/%.h
	$(CXX) $(CXXFLAGS) -fPIC -o $@ -c $<

libcrucible.so: $(OBJS) Makefile
	$(CXX) $(LDFLAGS) -fPIC -o $@ $(OBJS) -shared -Wl,-soname,$@ -luuid
