if get_option('build-examples')

example_sources = [
  ['infinite-loop'],
]

foreach example: example_sources
  example_name = example.get(0)
  example_sources = ['@0@.c'.format(example_name)]

  example_exe = executable(example_name, example_sources,
          c_args: deprecated_c_args,
    dependencies: [libdex_static_dep],
         install: false,
  )
endforeach

endif
