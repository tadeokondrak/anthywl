# anthywl

Basic Japanese input method for Sway and other Wayland compositors.

## Dependencies

Build-time:

- meson
- ninja
- scdoc (optional, for man pages)

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
