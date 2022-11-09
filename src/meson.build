libdex_sources = [
  'dex-async-pair.c',
  'dex-block.c',
  'dex-callable.c',
  'dex-cancellable.c',
  'dex-enums.c',
  'dex-function.c',
  'dex-future.c',
  'dex-future-set.c',
  'dex-init.c',
  'dex-main-scheduler.c',
  'dex-object.c',
  'dex-promise.c',
  'dex-scheduler.c',
  'dex-tasklet.c',
  'dex-timeout.c',
]

libdex_headers = [
  'dex-async-pair.h',
  'dex-callable.h',
  'dex-cancellable.h',
  'dex-enums.h',
  'dex-function.h',
  'dex-future.h',
  'dex-future-set.h',
  'dex-init.h',
  'dex-main-scheduler.h',
  'dex-object.h',
  'dex-promise.h',
  'dex-scheduler.h',
  'dex-tasklet.h',
  'dex-timeout.h',
  'libdex.h',
]

version_split = meson.project_version().split('.')
version_conf = configuration_data()
version_conf.set('VERSION', meson.project_version())
version_conf.set('MAJOR_VERSION', version_split[0])
version_conf.set('MINOR_VERSION', version_split[1])
version_conf.set('MICRO_VERSION', version_split[2])

configure_file(
          input: 'dex-version.h.in',
         output: 'dex-version.h',
  configuration: version_conf,
        install: true,
    install_dir: join_paths(get_option('includedir'), 'libdex')
)

libdex_deps = [
  glib_dep,
]

libdex_c_args = [
  deprecated_c_args,
  hidden_visibility_args,
  '-DG_LOG_DOMAIN="Dex"',
  '-DDEX_COMPILATION',
]

libdex_static = static_library('dex-@0@'.format(api_version), libdex_sources,
  dependencies: libdex_deps,
        c_args: libdex_c_args,
)

libdex_static_dep = declare_dependency(
           link_whole: libdex_static,
  include_directories: include_directories('.'),
         dependencies: libdex_deps,
)

libdex = shared_library('dex-@0@'.format(api_version),
  dependencies: [libdex_deps, libdex_static_dep],
        c_args: libdex_c_args,
       install: true,
)

install_headers(libdex_headers, subdir: 'libdex')

pkg = import('pkgconfig')
pkg.generate(
  description: 'A shared library for deferred execution',
    libraries: libdex,
         name: 'libdex',
     filebase: 'libdex-' + api_version,
      version: meson.project_version(),
      subdirs: 'libdex',
     requires: ['gobject-2.0'],
  install_dir: join_paths(get_option('libdir'), 'pkgconfig')
)

gnome = import('gnome')
libdex_gir = gnome.generate_gir(libdex,
              sources: [libdex_sources, libdex_headers],
            nsversion: api_version,
            namespace: 'Dex',
        symbol_prefix: 'dex',
    identifier_prefix: 'Dex',
             includes: ['GObject-2.0'],
              install: true,
	       header: 'libdex.h',
           extra_args: ['-DDEX_COMPILATION'],
)

libdex_dep = declare_dependency(
            link_with: libdex,
  include_directories: include_directories('.'),
         dependencies: libdex_deps,
              sources: [libdex_gir],
)
