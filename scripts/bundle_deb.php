<?php
foreach (file("Sun/sun.cpp") as $line)
{
	if (substr($line, 0, 21) == "#define SUN_VERSION \"")
	{
		$sun_version = substr($line, 21, -2);
		break;
	}
}
$sun_version or die("Failed to determine Sun version");

$arch = trim(shell_exec("dpkg --print-architecture"));
$libc = trim(explode("-", substr(shell_exec("dpkg -s libc6 | grep '^Version:'"), 9))[0]);

@mkdir("sun");
@mkdir("sun/DEBIAN");
@mkdir("sun/usr");
@mkdir("sun/usr/bin");

$control = <<<EOC
Package: sun
Version: $sun_version
Section: custom
Priority: optional
Architecture: $arch
Essential: no
Maintainer: Sainan <sainan@calamity.inc>
Description: The C++ build system that just works.
Depends: libc6 (>= $libc)

EOC;
echo $control;
file_put_contents("sun/DEBIAN/control", $control);
chmod("sun/DEBIAN/control", 0644);

copy("Sun/sun", "sun/usr/bin/sun");
chmod("sun/usr/bin/sun", 0755);

passthru("dpkg-deb --build sun");
rename("sun.deb", "sun.$sun_version.$arch.deb");
