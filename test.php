<?php
chdir("examples");

$sun = defined("PHP_WINDOWS_VERSION_MAJOR") ? "..\\..\\Sun.exe" : "../../suncli";

function toexe($filename) {
	if (defined("PHP_WINDOWS_VERSION_MAJOR")) {
		return $filename/*.".exe"*/;
	}
	$env = (PHP_OS_FAMILY === "Darwin") ? "DYLD_LIBRARY_PATH" : "LD_LIBRARY_PATH";
	return "$env=.:\$$env ./" . $filename;
}

function assert_equal($got, $expected) {
	if ($got !== $expected) {
		throw new Exception("Expected $expected but got $got");
	}
}

chdir("dll");
passthru($sun);
if (defined("PHP_WINDOWS_VERSION_MAJOR")) {
	copy("foolib/foolib.dll", "foolib.dll");
}
elseif (PHP_OS_FAMILY === "Darwin") {
	copy("foolib/libfoolib.dylib", "libfoolib.dylib");
}
else {
	copy("foolib/libfoolib.so", "libfoolib.so");
}
assert_equal(shell_exec(toexe("consumer")), "Hello, world!\n");
chdir("..");

chdir("exe");
passthru($sun);
assert_equal(shell_exec(toexe("exe")), "Hello, world!\n");
chdir("..");

chdir("exe-lib");
passthru($sun);
assert_equal(shell_exec(toexe("exe-lib")), "Hello, world!\n");
chdir("..");
