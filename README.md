# Sun

The C++ build system that just works.

- [Why we're making Sun](https://calamity.gg/sun/)
- [Building Sun](<docs/Building.md>)
- [Project Configuration](<docs/Config (.sun file).md>)

## Prebuilt Binaries

You can find pre-built binaries of Sun for Windows, Linux, & MacOS in [the releases](https://github.com/calamity-inc/Sun/releases).

## Packages

### Chocolatey

You can easily install `sun` via Chocolatey as follows:
```
choco source add -n "Calamity, Inc." -s https://choco.calamity.gg/index.json
choco install sun
```

### APT Repository

You can easily install `sun` via APT as follows:
```
wget -qO- https://calamity-inc.github.io/deb-repo/key.gpg | sudo tee /usr/share/keyrings/calamity-inc.gpg > /dev/null
echo "deb [signed-by=/usr/share/keyrings/calamity-inc.gpg] https://calamity-inc.github.io/deb-repo/ buster main" | sudo tee /etc/apt/sources.list.d/calamity-inc.list > /dev/null
sudo apt update
sudo apt install sun
```
The distribution name "buster" is hardcoded. See [here](https://github.com/calamity-inc/deb-repo/issues/2) for operating system requirements.
