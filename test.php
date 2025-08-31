<?php
chdir("examples");

$sun = defined("PHP_WINDOWS_VERSION_MAJOR") ? "..\\..\\Sun.exe" : "../../suncli";

function assert_equal($got, $expected) {
	if ($got !== $expected) {
		throw new Exception("Expected $expected but got $got");
	}
}

chdir("dll");
passthru($sun);
copy("foolib/foolib.dll", "foolib.dll");
assert_equal(shell_exec("consumer"), "Hello, world!\n");
chdir("..");

chdir("exe");
passthru($sun);
assert_equal(shell_exec("exe"), "Hello, world!\n");
chdir("..");

chdir("exe-lib");
passthru($sun);
assert_equal(shell_exec("exe-lib"), "Hello, world!\n");
chdir("..");
