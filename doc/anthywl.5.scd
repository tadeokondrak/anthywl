anthywl(5)

# NAME

anthywl - Configuration file

# DESCRIPTION

anthywl's configuration file syntax is based on the scfg format.
See https://git.sr.ht/~emersion/scfg for details.

# DIRECTIVES

	*active-at-startup*:

	If this directive is included, anthywl will be active on startup.
	This means that typing will immediately result in conversion.

	Example:

	```
	active-at-startup
	```

	*global-bindings*, *composing-bindings*, *selecting-bindings*:

	Each sub-directive in these blocks is in the form

		<key combination> <action>

	See *ACTIONS* for information about possible values of <action>.

	Example:

	```
	global-bindings {
			Ctrl+Shift+Backspace toggle
	}
	```

# ACTIONS

	*enable*:

		If not in any mode, enters composing mode.

	*disable*:

		In any mode, leaves the mode.

	*toggle*:

		If not in any mode, enters composing mode.
		In any mode, leaves the mode.

	*delete-left*:

		In selecting mode, switches to composing mode, then deletes one
		character from the end.

		In composing mode, deletes one character to the left.

		A suggested key for this action is *BackSpace*.

	*delete-right*:

		In composing mode, deletes one character to the right.

		A suggested key for this action is *Delete*.

	*move-left*:

		In composing mode, moves the cursor one character to the left.
		In selecting mode, moves the selection one word to the left.

		A suggested key for this action is *Left*.

	*move-right*:

		In composing mode, moves the cursor one character to the right.
		In selecting mode, moves the selection one word to the right.

		A suggested key for this action is *Right*.

	*expand-left*:

		In composing mode, expands the selection one character to the
		left.
		In selecting mode, expands the current word one character to
		the left.

		A suggested key combination for this action is *Shift-Left*.

	*expand-right*:

		In composing mode, expands the selection one character to the
		right.
		In selecting mode, expands the current word one character to
		the right.

		A suggested key combination for this action is *Shift-Right*.

	*select*:

		Switches to selecting mode.

		A suggested key for this action is *Return*.

	*compose*:

		Switches to composing mode.

		Switch to composing mode.

	*accept*:

		In composing or selecting mode, commits the current text and
		returns to composing mode.

		A suggested key for this action is *space*.

	*discard*:

		In composing or selecting mode, commits the current text and
		returns to composing mode.

		A suggested key for this action is *Escape*.

	*prev-candidate*:

		In selecting mode, selects the previous candidate.

		A suggested key for this action is *Up*.

	*next-candidate*:

		In selecting mode, selects the next candidate.

		A suggested key for this action is *Down*.

	*cycle-candidate*:

		In selecting mode, selects the next candidate cycling back to
		the first candidate when the end is reached.

		A suggested key for this action is *space*.


# SEE ALSO

*anthywl*(1)
