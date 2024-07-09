# Mag

Mag is a text editor.  It is designed to be fast and configurable.  Users of Mag are encouraged
to edit the source code of the editor to customize it to fit their usages.  Mag supports many
programming languages, including C, C++, CMake, Go, HTML, CSS, JS, Markdown, and Shell / Bash.

Tutorials are provided in the `tutorial` directory.

Mag is licensed under GPL3.  If you wish to purchase a
different license, email czipperz AT gmail DOT com.

## Builtin features
* Graphical and console rendering.
* Multiple cursors
  - Intuitively integrated with search and syntax highlighting functionality.
* Syntax highlighting is easily customizable.
* File name completion (Control-P equivalent).
* Built in support for `ag`, `clang-format`, `man`, `gtags`, `ctags`, and other external programs.
* Automatically updates files when edited externally.
* Animated scrolling even over large distances.
* Multi threaded architecture -- syntax highlighting and external
  programs are ran on a background thread to prevent stalls.
* Easy to add new commands and visual enhancements.

## Customizing

The `custom` folder can be customized to fit your needs.  In particular `config.cpp`'s
`create_key_map()` has all bindings for normal operation.

## Building & installation

See [INSTALL.md](./INSTALL.md) for instructions on how to build and install Mag.

## Debugging

Mag's SDL2 interface and unit tests can be debugged via GDB trivially.

Mag's NCurses interface can be debugged using the provided `debug-ncurses.sh` script.
See that file for more documentation.

## Optimization

See [OPTIMIZE.md](./OPTIMIZE.md) for instructions on how to build Mag.
