default install all: lib src test

clean:
	git clean -dfx

.PHONY: lib src

lib:
	$(MAKE) -C lib

src: lib
	$(MAKE) -C src

test: lib src
	$(MAKE) -C test
