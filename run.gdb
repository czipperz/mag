set sysroot
target remote localhost:12346
b panic_reached
continue
layout src
