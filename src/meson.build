libdex_sources = [
  'dex-aio.c',
  'dex-aio-backend.c',
  'dex-async-pair.c',
  'dex-async-result.c',
  'dex-block.c',
  'dex-callable.c',
  'dex-cancellable.c',
  'dex-channel.c',
  'dex-delayed.c',
  'dex-enums.c',
  'dex-error.c',
  'dex-fiber.c',
  'dex-function.c',
  'dex-future.c',
  'dex-future-set.c',
  'dex-gio.c',
  'dex-init.c',
  'dex-main-scheduler.c',
  'dex-object.c',
  'dex-platform.c',
  'dex-promise.c',
  'dex-routine.c',
  'dex-scheduler.c',
  'dex-semaphore.c',
  'dex-stack.c',
  'dex-static-future.c',
  'dex-tasklet.c',
  'dex-thread-pool-scheduler.c',
  'dex-thread-pool-worker.c',
  'dex-thread-storage.c',
  'dex-timeout.c',
  'dex-work-queue.c',
  'dex-work-stealing-queue.c',
]

libdex_headers = [
  'dex-async-pair.h',
  'dex-async-result.h',
  'dex-block.h',
  'dex-callable.h',
  'dex-cancellable.h',
  'dex-channel.h',
  'dex-delayed.h',
  'dex-enums.h',
  'dex-error.h',
  'dex-fiber.h',
  'dex-function.h',
  'dex-future.h',
  'dex-future-set.h',
  'dex-gio.h',
  'dex-init.h',
  'dex-main-scheduler.h',
  'dex-object.h',
  'dex-platform.h',
  'dex-promise.h',
  'dex-routine.h',
  'dex-scheduler.h',
  'dex-static-future.h',
  'dex-tasklet.h',
  'dex-thread-pool-scheduler.h',
  'dex-timeout.h',
  'libdex.h',
]

if liburing_dep.found()
  libdex_sources += [
    'dex-uring-aio-backend.c',
    'dex-uring-future.c',
  ]
endif

if have_func_kqueue and have_func_kevent
  libdex_sources += [
    'dex-kqueue-aio-backend.c',
    'dex-kqueue-future.c',
  ]
  config_h.set10('HAVE_KQUEUE', true)
endif

if host_machine.system() != 'windows'
  libdex_sources += ['dex-unix-signal.c']
  libdex_headers += ['dex-unix-signal.h']
endif

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
  cc.find_library('atomic', required: false),
  liburing_dep,
  glib_dep,
]

if get_option('sysprof')
  libdex_deps += [libsysprof_capture_dep]
endif

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

libdex_dep_sources = []

install_headers(libdex_headers, subdir: 'libdex')

pkg.generate(
  description: 'A shared library for deferred execution',
    libraries: libdex,
         name: 'libdex',
     filebase: 'libdex-' + api_version,
      version: meson.project_version(),
      subdirs: 'libdex',
     requires: ['gio-2.0'],
  install_dir: join_paths(get_option('libdir'), 'pkgconfig')
)

if get_option('introspection').enabled()
  libdex_gir = gnome.generate_gir(libdex,
                sources: [libdex_sources, libdex_headers],
              nsversion: api_version,
              namespace: 'Dex',
          symbol_prefix: 'dex',
      identifier_prefix: 'Dex',
        install_dir_gir: girdir,
    install_dir_typelib: typelibdir,
               includes: ['Gio-2.0'],
                install: true,
                 header: 'libdex.h',
             extra_args: ['-DDEX_COMPILATION'],
  )

  libdex_dep_sources += [libdex_gir]

  dex_vapi = gnome.generate_vapi('libdex',
          sources: libdex_gir.get(0),
          install: true,
      install_dir: join_paths(datadir, 'vala', 'vapi'),
         packages: ['gio-2.0'],
  )
endif

libdex_dep = declare_dependency(
            link_with: libdex,
  include_directories: include_directories('.'),
         dependencies: libdex_deps,
              sources: [libdex_dep_sources],
)
