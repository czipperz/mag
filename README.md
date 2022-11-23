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






## Building

1. Install the dependencies (see below).

2. Clone the repository and the submodules.

```
git clone https://github.com/czipperz/mag
cd mag
git submodule init
git submodule update
```

3. Build Mag by running (on all platforms):

```
./build-release
```

4. After building, Mag can be ran via `./build/release/mag`.

### Linux and OSX

Required packages: a C++ compiler, ncurses, and SDL (`SDL2`, `SDL2_image`, `SDL2_ttf`).

On Ubuntu you can install: `libncurses5 libsdl2-dev libsdl2-image-2.0-0 libsdl2-ttf-2.0-0`.

On Arch you can install: `ncurses sdl2 sdl2_image sdl2_ttf`.

### Windows

After cloning but before building you must download the [SDL], [SDL_ttf], and [SDL_image]
"Development Libraries" and place them in the `SDL`, `TTF`, and `IMG` directories, respectively.

[SDL]: https://www.libsdl.org/download-2.0.php
[SDL_ttf]: https://www.libsdl.org/projects/SDL_ttf/
[SDL_image]: https://www.libsdl.org/projects/SDL_image/

You will also need to install [ImageMagick] as it is used to generate Mag's icons.

[ImageMagick]: https://imagemagick.org/script/download.php

Next, to build the project you can either use the Visual Studio gui or the Windows build script.

* To use the Visual Studio gui, open the project as a
  folder in Visual Studio and click Build -> Build All.
* To use CMake, run the PowerShell build script: `.\build-release.ps1`.  But you must set
  `$env:VCINSTALLDIR` to the path of the `VC` directory (for example `$env:VCINSTALLDIR =
  "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC"`).

After building, Mag can be ran via `.\build\release\mag.exe`.






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
