libgio_unix_dep = dependency('gio-unix-2.0', required: false, disabler: true)
libsoup_dep = dependency('libsoup-3.0', required: false, disabler: true)

dbus_ping_pong = []
if have_gdbus_codegen
  ext = meson.project_source_root() / 'src' / 'dex-gdbus-codegen-extension.py'
  dbus_ping_pong = gnome.gdbus_codegen(
    'dex-dbus-ping-pong',
    sources: ['org.example.PingPong.xml'],
    interface_prefix: 'org.example',
    namespace: 'DexDbus',
    extra_args: [f'--extension-path=@ext@'],
  )
endif

examples = {
            'cat': {'dependencies': libgio_unix_dep},
        'cat-aio': {},
             'cp': {},
           'dbus': {'extra-sources': dbus_ping_pong, 'disable': not have_gdbus_codegen},
     'echo-bench': {},
           'host': {},
          'httpd': {'dependencies': libsoup_dep},
  'infinite-loop': {},
       'tcp-echo': {},
           'wget': {'dependencies': libsoup_dep},
}

foreach example, params: examples
  if params.get('disable', false)
    continue
  endif

  example_exe = executable(example,
         sources: ['@0@.c'.format(example)] + params.get('extra-sources', []),
          c_args: deprecated_c_args,
    dependencies: [libdex_static_dep, params.get('dependencies', [])],
         install: false,
  )
endforeach
