# Building

## Without Sun

aka. bootstrapping

### On Windows

Use the Visual Studio 2022 project to build Soup as a static lib, then build Sun.

### On Linux

```
php Sun/vendor/Soup/build_lib.php
clang -o suncli Sun/sun.cpp -ISun/vendor/Soup Sun/vendor/Soup/libsoup.a -std=c++17 -fuse-ld=lld -lstdc++ -lstdc++fs -pthreads
```

You can then use the following command to make the "suncli" executable globally available as "sun":

```
cp suncli /usr/local/bin/sun
```

### On MacOS

Should be more or less the same as on Linux, but MacOS prefers libc++ over libstdc++:

```
clang -o suncli Sun/sun.cpp -ISun/vendor/Soup Sun/vendor/Soup/libsoup.a -std=c++17 -fuse-ld=lld -lc++ -pthreads
```

## With Sun

Just run `sun` in the "Sun" folder (the one with the .sun file in it)
