# Mag

Mag is a text editor.  It is designed to be fast and configurable.  Users of Mag are encouraged to
edit the source code of the editor to customize it to fit their usages.  Mag is designed to be used
primarily on C and C++ code, but coding in any language is supported.

## Builtin features
* Multiple cursors
  - Intuitively integrated with search and syntax highlighting functionality.
* Syntax highlighting for C/C++, Markdown, CSS, diff files, and more.
* File name completion (Control-P equivalent).
* Built in support for `git grep`, `clang-format`, `man`, and other external programs.
* Automatically updates files when edited externally.
* Easy to add new commands and visual enhancements.

## Building

### Linux and OSX

Clone the repository and the submodules and run the build script:

```
git clone https://github.com/czipperz/mag
cd mag
git submodule init
git submodule update
./build-release.sh
```

Required packages: a C++ compiler, ncurses, and sdl.

### Windows

First, clone the repository and the submodules:

```
git clone https://github.com/czipperz/mag
cd mag
git submodule init
git submodule update
```

Before building you must download the SDL, SDL_ttf, and SDL_image development files and place them
in the `SDL`, `TTF`, and `IMG` directories, respectively.  You will also need to install ImageMagick
as it is used to generate Mag's icons.

Next, open the project as a folder in Visual Studio and click Build -> Build All.

## Customizing
The `custom` folder can be customized to fit your needs.  In particular `config.cpp`'s
`create_key_map()` has all bindings for normal operation.

## Optimizing
We use Tracy to optimize Mag.  See the
[manual](https://bitbucket.com/wolfpld/tracy/downloads/tracy.pdf) for more information.

To prepare we have to build Mag with Tracy enabled and also build Tracy's profiler.  Once both are
built, we then run the profiler and Mag at the same time.

Build Mag with Tracy enabled:
```
./build-tracy.sh
```

By cloning the `tracy` submodule you will already have the source downloaded.  Build the `profiler`
sub project:
```
cd tracy/profiler/build/unix
make release
```

Then we run Tracy:
```
./tracy/profiler/build/unix/Tracy-release
```

Then run Mag with Tracy enabled.  Run it as the super user to enable context switching recognition.
```
sudo ./build/tracy/mag
```
