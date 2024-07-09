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
