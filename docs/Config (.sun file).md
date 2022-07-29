# Config (.sun file)

When you run Sun, it will look for a `.sun` file in the working directory, and create one if it doesn't exist.

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

## Compile to static library

Use the `sun set static` to indicate that the project is a static library.

Internally, this will simply add a line that says `static` to the .sun file.
