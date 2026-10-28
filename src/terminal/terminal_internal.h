#pragma once

// Internal escape-sequence parsing seams, exported for unit tests only --
// not part of the public terminal API (use ReadKey()). The `Terminal` prefix
// guards against symbol collisions in the whole-program link.

/** Decodes an xterm/kitty modifier parameter (1+bitmask) into KEY_MOD_* flags. */
int TerminalGetModifierFlags(int modifierParameter);

/** Maps a character code from a modified-key escape sequence to a key value. */
int TerminalMapCharCodeToKey(int charCode);

/** Applies modifier flags to a character code (Ctrl-letter folding etc.). */
int TerminalMapModifiedKeyCode(int charCode, int modifierFlags);

// Internal sentinel: escape sequence decoded to a key-release event that must
// be swallowed by ReadKey (never returned to callers as a keypress).
#define TERMINAL_KEY_NONE (-2)

/**
 * Parses a complete CSI sequence body (the bytes after "ESC [", including the
 * final byte). Pure, no I/O. Returns the decoded key, '\x1b' for unrecognized
 * sequences, or TERMINAL_KEY_NONE for key-release events.
 */
int TerminalParseCsi(const char* seq, int seq_len);
