crucible_version_cc = vcs_tag(
  input: 'version.cc.in',
  output: 'version.cc',
  replace_string: '@VERSION@',
)

crucible_lib = static_library(
  'crucible',
  [
    'bytevector.cc',
    'chatter.cc',
    'city.cc',
    'cleanup.cc',
    'crc64.cc',
    'error.cc',
    'extentwalker.cc',
    'fd.cc',
    'fs.cc',
    'ntoa.cc',
    'path.cc',
    'process.cc',
    'string.cc',
    'task.cc',
    'time.cc',
    'uname.cc',
    crucible_version_cc,
  ],
  dependencies: [ thread_dep ],
  include_directories: [ inc ],
)
