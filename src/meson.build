anthywl_src += files(
    'anthywl.c',
    'actions.c',
    'buffer.c',
    'config.c',
    'graphics_buffer.c',
)

if get_option('ipc').enabled()
    anthywl_src += files('ipc.c')
endif

anthywl_bin = executable(
    'anthywl',
    anthywl_src,
    install: true,
    include_directories: anthywl_inc,
    dependencies: [
        wayland_client_dep,
        wayland_cursor_dep,
        xkbcommon_dep,
        protocols_dep,
        anthy_dep,
        pango_dep,
        cairo_dep,
        pangocairo_dep,
        scfg_dep,
        varlink_dep,
    ],
)
