default install all: lib src test README.html

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
