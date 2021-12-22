project(
  'bees',
  [ 'c', 'cpp', ],
  default_options : [
    'warning_level=3',
    'c_std=c99',
    'cpp_std=c++11',
  ],
  license: 'GPL-3.0-or-later',
  version : '0.7',
)

# Helper scripts for source generation are written in python.
# Use the same python as meson was run with.
pymod = import('python')
python = pymod.find_installation()

systemd_dep = dependency('systemd', disabler: true, required: get_option('systemd'))
systemdsystemunitdir = systemd_dep.get_variable('systemdsystemunitdir')

# This is meson automagic to figure out how to enable pthreads
thread_dep = dependency('threads')

inc = include_directories('include')

beesconfdir = get_option('sysconfdir') / 'bees'
beeslibdir = get_option('libdir') / 'bees'

subdir('lib')
subdir('src')
subdir('test')
subdir('docs')
subdir('scripts')
