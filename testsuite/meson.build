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
  '-DDEX_ENABLE_DEBUG',
  '-DG_LOG_DOMAIN="libdex"',
  '-DG_ENABLE_DEBUG',
  '-UG_DISABLE_ASSERT',
  '-UG_DISABLE_CAST_CHECKS',
]

dbus_foo = []
if have_gdbus_codegen
  ext = meson.project_source_root() / 'src' / 'dex-gdbus-codegen-extension.py'
  dbus_foo = gnome.gdbus_codegen(
    'dex-test-dbus-foo',
    sources: ['org.example.Foo.xml'],
    interface_prefix: 'org.example',
    namespace: 'DexTestDbus',
    extra_args: [f'--extension-path=@ext@'],
  )
endif

testsuite = {
  'test-async-result': {},
  'test-channel': {},
  'test-dbus': {'extra-sources': dbus_foo, 'disable': not have_gdbus_codegen},
  'test-object': {},
  'test-fiber': {},
  'test-future': {},
  'test-future-list-model': {},
  'test-scheduler': {},
  'test-semaphore': {},
  'test-stream': {},
  'test-thread': {},
  'test-version': {},
  'test-watch': {},
}

testsuite_deps = [
  libdex_static_dep,
]

if get_option('sysprof')
  testsuite_deps += [libsysprof_capture_dep]
endif

foreach test, params: testsuite
  if params.get('disable', false)
    continue
  endif

  test_exe = executable(test,
         sources: ['@0@.c'.format(test)] + params.get('extra-sources', []),
          c_args: testsuite_c_args + deprecated_c_args,
    dependencies: testsuite_deps,
  )

  test(test, test_exe, env: test_env)
endforeach
