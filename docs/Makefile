MARKDOWN := $(firstword $(shell type -P markdown markdown2 markdown_py 2>/dev/null || echo markdown))
.PHONY: docs

docs: $(subst .md,.html,$(wildcard *.md)) index.html ../README.html

%.html: %.md
	$(MARKDOWN) $< | sed -e 's/\.md/\.html/g' > $@.new
	mv -f $@.new $@

index.md: ../README.md
	sed -e 's:docs/::g' < ../README.md > index.md.new
	mv -f index.md.new index.md
