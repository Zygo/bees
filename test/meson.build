tests = [
  'chatter',
  'crc64',
  'fd',
  'limits',
  'namedptr',
  'path',
  'process',
  'progress',
  'task'
]

test_workdir = meson.current_source_dir()

foreach test_name: tests
  exe = executable(
    test_name,
    [ '@0@.cc'.format(test_name) ],
    include_directories: [ inc ],
    link_with: [ crucible_lib ],
  )
  test(test_name, exe, workdir: test_workdir)
endforeach
