anthywl

Work-in-progress Japanese input method for sway

Required patch for popup support:
https://github.com/swaywm/sway/pull/5890

Default config:

```
active-at-startup
global-bindings {
    Ctrl+Shift+Backspace toggle
}
composing-bindings {
    space select
    Return accept
    Escape discard
    Backspace delete-left
    Left move-left
    Left move-right
}
selecting-bindings {
    Escape discard
    Return accept
    BackSpace delete-left
    Left move-left
    Right move-right
    Shift+Left expand-left
    Shift+Right expand-right
    Up prev-candidate
    Down next-candidate
    space cycle-candidate
}
```

Any feedback on how things should work is appreciated, open a GitHub
issue or discussion if you have any.
