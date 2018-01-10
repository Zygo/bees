PREFIX ?= /
LIBEXEC_PREFIX ?= $(PREFIX)/usr/lib/bees

MARKDOWN := $(firstword $(shell which markdown markdown2 markdown_py 2>/dev/null))
MARKDOWN ?= markdown

# allow local configuration to override above variables
-include localconf

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

scripts/beesd: scripts/beesd.in
	sed -e's#@LIBEXEC_PREFIX@#$(LIBEXEC_PREFIX)#' -e's#@PREFIX@#$(PREFIX)#' "$<" >"$@"

scripts/beesd@.service: scripts/beesd@.service.in
	sed -e's#@LIBEXEC_PREFIX@#$(LIBEXEC_PREFIX)#' -e's#@PREFIX@#$(PREFIX)#' "$<" >"$@"

scripts: scripts/beesd scripts/beesd@.service

README.html: README.md
	$(MARKDOWN) README.md > README.html.new
	mv -f README.html.new README.html

install: ## Install bees + libs
install: lib src test
	install -Dm644 lib/libcrucible.so $(DESTDIR)$(PREFIX)/usr/lib/libcrucible.so
	install -Dm755 bin/bees	$(DESTDIR)$(LIBEXEC_PREFIX)/bees

install_scripts: ## Install scipts
install_scripts: scripts
	install -Dm755 scripts/beesd $(DESTDIR)$(PREFIX)/usr/sbin/beesd
	install -Dm644 scripts/beesd.conf.sample $(DESTDIR)$(PREFIX)/etc/bees/beesd.conf.sample
	install -Dm644 scripts/beesd@.service $(DESTDIR)$(PREFIX)/lib/systemd/system/beesd@.service

help: ## Show help
	@fgrep -h "##" $(MAKEFILE_LIST) | fgrep -v fgrep | sed -e 's/\\$$//' | sed -e 's/##/\t/'
