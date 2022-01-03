anthywl(1)

# NAME

anthywl - A Japanese input method for Wayland

# SYNOPSIS

*anthywl*

# DESCRIPTION

anthywl is a Japanese input method for Wayland compositors supporting the
input-method-v2 protocol.

# CONFIGURATION

anthywl is configured using a single configuration file using the scfg format.

It looks for it at $XDG_CONFIG_HOME/anthywl/config. If $XDG_CONFIG_HOME is
unset, it defaults to *$HOME/.config*. If none is found, a default is used.

See *anthywl*(5) for details on the configuration syntax and options.

# AUTHORS

Tadeo Kondrak <me@tadeo.ca>

# SEE ALSO

*anthywl*(5)
