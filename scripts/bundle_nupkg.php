<?php
foreach (file("Sun/sun.cpp") as $line)
{
    if (substr($line, 0, 21) == "#define SUN_VERSION \"")
    {
        $sun_version = substr(trim($line), 21, -1);
        break;
    }
}
$sun_version or die("Failed to determine Sun version");

file_put_contents(".chocogen.json", <<<EOC
{
    "path": ["Sun.exe"],
    "title": "Sun",
    "description": "The C++ build system that just works.",
    "repository": "https://github.com/calamity-inc/Sun",
    "license": "https://github.com/calamity-inc/Sun/blob/senpai/LICENCE",
    "changelog": "https://github.com/calamity-inc/Sun/releases",
    "issues": "https://github.com/calamity-inc/Sun/issues"
}
EOC);
file_put_contents("chocogen.php", file_get_contents("https://raw.githubusercontent.com/calamity-inc/chocogen/0b1c843eaa167caf590bd56f46f5a57f6ffc6000/chocogen.php"));
passthru("php chocogen.php $sun_version");
rename("Sun.$sun_version.nupkg", "sun.nupkg");
rename("sun.nupkg", "sun.$sun_version.nupkg");
