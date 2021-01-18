wayland_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')

wayland_scanner_dep = dependency('wayland-scanner', native: true)
wayland_scanner_bin = find_program(
    wayland_scanner_dep.get_variable(pkgconfig: 'wayland_scanner'),
    native: true,
)

protocols = {
    'text-input-v3': wayland_protocols_dir / 'unstable/text-input/text-input-unstable-v3.xml',
    'zwp-input-method-unstable-v2': 'input-method-unstable-v2.xml',
    'zwp-virtual-keyboard-unstable-v1': 'virtual-keyboard-unstable-v1.xml',
}

protocols_src = []
protocols_inc = []

foreach name, path : protocols
    protocols_src += custom_target(
        name.underscorify() + '_c',
        input: path,
        output: '@BASENAME@-protocol.c',
        command: [wayland_scanner_bin, 'private-code', '@INPUT@', '@OUTPUT@']
    )

    protocols_inc += custom_target(
        name.underscorify() + '_client_protocol_h',
        input: path,
        output: '@BASENAME@-client-protocol.h',
        command: [wayland_scanner_bin, 'client-header', '@INPUT@', '@OUTPUT@']
    )
endforeach

protocols_lib = static_library(
    'protocols',
    [protocols_src, protocols_inc],
)

protocols_dep = declare_dependency(
    link_with: protocols_lib,
    sources: protocols_inc,
)
