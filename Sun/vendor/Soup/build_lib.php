<?php
require "build_config.php";

// Setup folders
if(!is_dir(__DIR__."/bin"))
{
	mkdir(__DIR__."/bin");
}
if(!is_dir(__DIR__."/bin/int"))
{
	mkdir(__DIR__."/bin/int");
}

// Find work
$files = [];
foreach(scandir(__DIR__."/soup") as $file)
{
	if(substr($file, -4) == ".cpp")
	{
		array_push($files, substr($file, 0, -4));
	}
}

echo "Compiling...\n";
$objects = [];
foreach($files as $file)
{
	echo $file."\n";
	passthru("$clang -c ".__DIR__."/soup/$file.cpp -o ".__DIR__."/bin/int/$file.o");
	array_push($objects, escapeshellarg(__DIR__."/bin/int/$file.o"));
}

echo "Bundling static lib...\n";
$archiver = "ar";
if (defined("PHP_WINDOWS_VERSION_MAJOR"))
{
	$archiver = "llvm-ar";
}
passthru("$archiver rc libsoup.a ".join(" ", $objects));
