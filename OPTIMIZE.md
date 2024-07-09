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
