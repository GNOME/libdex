libgio_unix_dep = dependency('gio-unix-2.0', required: false, disabler: true)

examples = {
  'cat': {
    'dependencies': libgio_unix_dep,
  },
  'cat-aio': {
    'dependencies': libgio_unix_dep,
  },
  'infinite-loop': {},
}

foreach example, params: examples
  example_exe = executable(example, '@0@.c'.format(example),
          c_args: deprecated_c_args,
    dependencies: [libdex_static_dep, params.get('dependencies', [])],
         install: false,
  )
endforeach
