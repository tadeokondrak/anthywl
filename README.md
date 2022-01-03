# anthywl

Work-in-progress Japanese input method for sway.

## Dependencies

For popup support, a currently unmerged Sway patch is needed:
https://github.com/swaywm/sway/pull/5890

Build-time:

- meson
- ninja

Runtime:

- wayland
- wayland-protocols
- libxkbcommon
- libanthy
- cairo
- pango
- libscfg

## Building

```sh
meson build
ninja -C build
```

## Configuration

Copy `default_config` to `~/.config/anthywl/config`.
