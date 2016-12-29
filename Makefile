PREFIX ?= /

default all: lib src test README.html

clean:
	git clean -dfx

.PHONY: lib src

lib:
	$(MAKE) -C lib

src: lib
	$(MAKE) -C src

test: lib src
	$(MAKE) -C test

README.html: README.md
	markdown README.md > README.html.new
	mv -f README.html.new README.html

install: lib src test
	install -Dm644 lib/libcrucible.so $(PREFIX)/usr/lib/libcrucible.so
	install -Dm755 bin/bees $(PREFIX)/usr/bin/bees
