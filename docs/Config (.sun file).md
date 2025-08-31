# Config (.sun file)

When you run Sun, it will look for a `.sun` file in the working directory.

You can also optionally provide a project name, e.g. with `sun example` it will look for a `example.sun` file.

## Add source files

You can add source files by using the `+` operator. All further characters in the line will be considered as a file name. You can also use `*` wildcards.

For example, you can tell Sun to compile all .cpp files (this is the default):

```
+*.cpp
```

or individual files:

```
+main.cpp
+utils.cpp
```

The wildcard operator will recurse subdirectories when the path also contains a forward slash (`/`). Otherwise, you can explicitly add `-R` to enable recursion, e.g. to have Sun search recursively for cpp files:

```
+*.cpp -R
```

## Remove source files

You can remove previously-added source files by using the `-` operator.

For example, if you want to compile all .cpp files except for wasm.cpp:

```
+*.cpp
-wasm.cpp
```

This operator handles wildcards and recursion exactly like the `+` operator.

## Compile as static library

Add a line that says `static` to the .sun file to indicate that the project is a static library.

## Compile as dynamic/shared library

Add a line that says `dynamic` to the .sun file to indicate that the project is a dynamic library.

## Dependencies

Add `require REL_PATH` to the .sun file to add a dependency to your project.

Then, when you run Sun, it will build your dependencies first, and finally build your project with relevant compiler and linker include flags.

If the include directory differs from source directory, you can use `require REL_PATH include_dir=REL_PATH`.

## Project name

Sun will make a guess about your project name based on the file structure, but you can override this adding `name ...` to the .sun file.

## C++ version

You can use `c++ ...` or `cpp ...` to specify the C++ version for your project. For example:

```
cpp 20
```

## Compiler arguments

You can pass arbitrary arguments to the compiler with the `arg` keyword.

You can define preprocessor macros with the `define` keyword. For example:

```
define FOO=BAR
define ENABLE_FEATURE
```

These lines add `-DFOO=BAR` and `-DENABLE_FEATURE` to the compiler arguments.

Linker-specific arguments can be provided with the `linker_arg` keyword.

## RTTI

By default, Sun adds `-fno-rtti` to the compiler arguments. To have it omitted, add a line that reads `rtti` to your .sun file.

## 32-bit targets

You can add a line that reads `32bit` to the .sun file to add `-m32` to the compilation of your project and its dependencies.

## Conditionals

Sun supports basic conditionals with the following syntax:

```
if [not] <condition>
...
endif
```

Valid substitutions for `<condition>` are as follows: `windows`, `macos`, `linux`, `x86`, `arm`, `true`, `false`.

An example:

```
if windows
linker_arg -lKernel32
endif
```

## Compiler

You can specify a compiler other than Clang. For example, to build Emscripten projects with Sun:

```
compiler em++
```

## Add options via the CLI

As you may have noticed, the .sun file is parsed line-by-line. You can add additional lines via the CLI using `+`, for example `sun +32bit` loads the file `.sun` and acts as if a line in that file reads `32bit`.

Note that if the added line contains a space, the argument needs to be quoted, either like `sun +"name myproject"` or `sun "+name myproject"`.
