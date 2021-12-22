markdown_bin = find_program(
  'cmark-gfm', 'redcarpet', 'markdown2', 'markdown', 'markdown_py',
  disabler: true,
  required: get_option('docs')
)

if markdown_bin.found() and markdown_bin.full_path().endswith('cmark-gfm')
  markdown_args = [ '-e', 'table' ]
else
  if markdown_bin.found()
    warning('The preferred markdown tool cmark-gfm was not found, you will not get Github-style tables.')
  endif
  markdown_args = []
endif

index_md = custom_target(
  input: [ 'index.py', '../README.md' ],
  output: 'index.md',
  command: [ python, '@INPUT@' ],
  capture: true,
)

docs_md = [
  'btrfs-kernel.md',
  'btrfs-other.md',
  'config.md',
  'event-counters.md',
  'gotchas.md',
  'how-it-works.md',
  'install.md',
  'missing.md',
  'options.md',
  'running.md',
  'wrong.md',
  index_md,
]

foreach doc_md: docs_md
  custom_target(
    build_by_default: true,
    capture: true,
    command: [ markdown_bin ] + markdown_args + '@INPUT@',
    input: doc_md,
    output: '@BASENAME@.html',
  )
endforeach
