scripts_config = configuration_data({
  'PREFIX': get_option('prefix'),
  'ETC_PREFIX': get_option('prefix') / get_option('sysconfdir'),
  'LIBEXEC_PREFIX': get_option('prefix') / beeslibdir,
})

configure_file(
  configuration: scripts_config,
  input: 'beesd.in',
  install: true,
  install_dir: get_option('sbindir'),
  install_mode: 'rwxr-xr-x',
  output: 'beesd',
)

configure_file(
  configuration: scripts_config,
  input: 'beesd@.service.in',
  install: true,
  install_dir: systemdsystemunitdir,
  install_mode: 'rw-r--r--',
  output: 'beesd@.service',
)

install_data('beesd.conf.sample', install_dir: beesconfdir, install_mode: 'rw-r--r--')
