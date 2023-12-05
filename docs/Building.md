# Building

## Without Sun

aka. bootstrapping

### Expected folder structure

The "Soup" and "Sun" repository folders should have the same parent.

### On Windows

Use the Visual Studio 2022 project to build Soup as a static lib, then build Sun.

### On Linux

```
cd Soup
php build_lib.php
cd ..
cd Sun
clang -o suncli Sun/main.cpp -I../Soup ../Soup/libsoup.a -std=c++20 -fuse-ld=lld -lstdc++ -lstdc++fs -pthreads
```

You can then use the following command to make the "suncli" executable globally available as "sun":

```
cp suncli /usr/local/bin/sun
```

## With Sun

Just run `sun` in the "Sun" folder (the one with the .sun file in it)
