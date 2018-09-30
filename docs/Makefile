MARKDOWN := $(firstword $(shell type -P markdown markdown2 markdown_py 2>/dev/null || echo markdown))
.PHONY: docs

docs: $(subst .md,.html,$(wildcard *.md)) ../README.html

%.html: %.md
	$(MARKDOWN) $< | sed -e 's/\.md/\.html/g' > $@.new
	mv -f $@.new $@
