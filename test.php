<?php
chdir("examples");

$sun = defined("PHP_WINDOWS_VERSION_MAJOR") ? "..\\..\\Sun.exe" : "../../suncli";

function toexe($filename) {
	if (!defined("PHP_WINDOWS_VERSION_MAJOR")) {
		return "LD_LIBRARY_PATH=.:\$LD_LIBRARY_PATH " . "./".$filename;
	}
	return $filename/*.".exe"*/;
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
