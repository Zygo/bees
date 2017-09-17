PREFIX ?= /

MARKDOWN := $(shell which markdown markdown2 markdown_py 2>/dev/null)
MARKDOWN ?= markdown

default all: lib src test README.html

clean: ## Cleanup
	git clean -dfx

.PHONY: lib src test

lib: ## Build libs
	$(MAKE) -C lib

src: ## Build bins
src: lib
	$(MAKE) -C src

test: ## Run tests
test: lib src
	$(MAKE) -C test

README.html: README.md
	$(MARKDOWN) README.md > README.html.new
	mv -f README.html.new README.html

install: ## Install bees + libs
install: lib src test
	install -Dm644 lib/libcrucible.so $(PREFIX)/usr/lib/libcrucible.so
	install -Dm755 bin/bees $(PREFIX)/usr/bin/bees

install_scripts: ## Install scipts
	install -Dm755 scripts/beesd $(PREFIX)/usr/bin/beesd
	install -Dm644 scripts/beesd.conf.sample $(PREFIX)/etc/bees/beesd.conf.sample
	install -Dm644 scripts/beesd@.service $(PREFIX)/lib/systemd/system/beesd@.service

help: ## Show help
	@fgrep -h "##" $(MAKEFILE_LIST) | fgrep -v fgrep | sed -e 's/\\$$//' | sed -e 's/##/\t/'
