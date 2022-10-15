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

## Remove source files

You can remove previously-added source files by using the `-` operator.

For example, if you want to compile all .cpp files except for wasm.cpp:

```
+*.cpp
-wasm.cpp
```

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

## Compiler arguments

You can pass arbitrary arguments to the compiler with the `arg` keyword.

Linker-specific arguments can be provided with the `linker_arg` keyword.

## Conditionals

Sun supports basic conditionals with the following syntax:

```
if [not] <condition>
...
endif
```

Valid substitutions for `<condition>` are as follows: `windows`, `linux`, `true`, `false`.

An example:

```
if windows
linker_arg -lKernel32
endif
```
