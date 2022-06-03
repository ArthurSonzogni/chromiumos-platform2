# cros\_im

This project implements support for using ChromeOS IMEs over the Wayland
protocol zwp\_unstable\_text\_input\_v1. Googlers: See [go/crostini-ime] and
[go/crostini-ime-tests] for additional design details.

This is currently under development and many desired pieces of functionality may
not work correctly or at all. If manually building cros\_im, it is recommended
to use a developer build of Chrome, or at least a dev channel device.

## Building with pdebuild
.deb packages for bullseye on supported architectures (arm64, armhf, amd64,
i386) can be built and installed using the following commands.

**Crostini users:** Please note that these do not work on a Crostini terminal
because LXC containers in Crostini are unprivileged and do not have permission
to run the commands required.

```bash
# For all architectures
sudo ./build-packages

# Or for a specific architecture
sudo ./build-packages <arch>

# Install the resultant package for your device
sudo apt install <path to .deb file>
```

## Manual build instructions
IME support for GTK3 Wayland applications can be compiled and configured as per
below. Configuring sommelier to allow the host compositor to handle Ctrl+Space
is recommended.

```bash
git clone https://chromium.googlesource.com/chromiumos/platform2
cd platform2/vm_tools/cros_im
sudo apt install clang cmake googletest libgtk-3-dev libgtkmm-3.0-dev libwayland-bin meson pkg-config
meson build && cd build && ninja

DEB_TARGET_GNU_TYPE=$(dpkg-architecture -q DEB_TARGET_GNU_TYPE)
sudo ln -s $(pwd)/libim_cros_gtk.so /usr/lib/${DEB_TARGET_GNU_TYPE}/gtk-3.0/3.0.0/immodules/im-cros.so
# Manually update GTK's IM modules cache. For IM modules installed from a .deb,
# this would be automatically run by libgtk-3.0's postinst hook.
sudo /usr/lib/${DEB_TARGET_GNU_TYPE}/libgtk-3-0/gtk-query-immodules-3.0 --update-cache
```

## Set the default GTK IM module.
Enabling the #crostini-ime-support flag in chrome://flags is currently required. It is also recommended to set the default GTK IM module:

```bash
echo "GTK_IM_MODULE=cros" >> ~/.config/environment.d/ime.conf
```

[go/crostini-ime]: https://goto.google.com/crostini-ime
[go/crostini-ime-tests]: https://goto.google.com/crostini-ime-tests
