if get_option('build-examples')

example_sources = [
  ['infinite-loop'],
]

libgio_unix_dep = dependency('gio-unix-2.0', required: false)

if libgio_unix_dep.found()
  example_sources += [
    ['cat', [libgio_unix_dep]],
  ]
endif

foreach example: example_sources
  example_name = example.get(0)
  example_sources = ['@0@.c'.format(example_name)]

  example_exe = executable(example_name, example_sources,
          c_args: deprecated_c_args,
    dependencies: [libdex_static_dep] + example.get(1, []),
         install: false,
  )
endforeach

endif
