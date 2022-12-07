test_env = [
  'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
  'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir()),
  'G_DEBUG=gc-friendly',
  'GSETTINGS_BACKEND=memory',
  'MALLOC_CHECK_=2',
  'NO_AT_BRIDGE=1',
]

testsuite_c_args = [
  '-DDEX_COMPILATION',
  '-DG_LOG_DOMAIN="libdex"',
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

testsuite_sources = [
  ['test-async-result'],
  ['test-channel'],
  ['test-object'],
  ['test-fiber'],
  ['test-future'],
  ['test-scheduler'],
  ['test-semaphore'],
  ['test-stream'],
]

foreach test: testsuite_sources
  test_name = test.get(0)
  test_sources = ['@0@.c'.format(test_name)]

  test_exe = executable(test_name, test_sources,
          c_args: testsuite_c_args + deprecated_c_args,
    dependencies: [libdex_static_dep],
  )

  test(test_name, test_exe, suite: package_string, env: test_env)
endforeach
