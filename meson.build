project(
    'anthywl',
    'c',
    version: '0.0.1-dev', # also update default.nix
    license: 'MIT',
    meson_version: '>=0.59.0',
    default_options: [
        'c_std=c11',
        'warning_level=2',
    ]
)

add_project_arguments(
    [
        '-D_GNU_SOURCE',
        '-Wno-unused-parameter',
    ],
    language: 'c',
)

cc = meson.get_compiler('c')

rt_dep = cc.find_library('rt')
wayland_client_dep = dependency('wayland-client')
wayland_cursor_dep = dependency('wayland-cursor')
wayland_protocols_dep = dependency('wayland-protocols')
xkbcommon_dep = dependency('xkbcommon')
anthy_dep = dependency('anthy')
pango_dep = dependency('pango')
cairo_dep = dependency('cairo')
pangocairo_dep = dependency('pangocairo')
scfg_dep = dependency('scfg', fallback: 'libscfg')
varlink_dep = dependency('libvarlink', required: get_option('ipc'))
scdoc = dependency('scdoc', native: true, required: get_option('man-pages'))

if get_option('ipc').enabled()
    add_project_arguments(['-DANTHYWL_IPC_SUPPORT'], language: 'c')
endif

anthywl_src = []
anthywl_inc = []

subdir('buildtools')
subdir('doc')
subdir('protocol')
subdir('include')
subdir('src')
