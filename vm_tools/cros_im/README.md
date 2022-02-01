# cros\_im

This project implements support for using ChromeOS IMEs over the Wayland
protocol zwp\_unstable\_text\_input\_v1. Googlers: See [go/crostini-ime] and
[go/crostini-ime-tests] for additional design details.

This is currently under development and many desired pieces of functionality may
not work correctly or at all. If manually building cros\_im, it is recommended
to use a developer build of Chrome, or at least a dev channel device.

IME support for GTK3 Wayland applications can be compiled and configured as per
below. Configuring sommelier to allow the host compositor to handle Ctrl+Space
is recommended.

```bash
git clone https://chromium.googlesource.com/chromiumos/platform2
cd platform2/vm_tools/cros_im
sudo apt install clang cmake googletest libgtk-3-dev libgtkmm-3.0-dev libwayland-bin meson pkg-config
meson build && cd build && ninja

sudo ln -s $(pwd)/libim_cros_gtk.so /usr/lib/x86_64-linux-gnu/gtk-3.0/3.0.0/immodules/im-cros.so
# Manually update GTK's IM modules cache. For IM modules installed from a .deb,
# this would be automatically run by libgtk-3.0's postinst hook.
sudo /usr/lib/x86_64-linux-gnu/libgtk-3-0/gtk-query-immodules-3.0 --update-cache

# Set the default GTK IM module.
sudo bash -c 'echo Environment=\"GTK_IM_MODULE=cros\" >> /etc/systemd/user/cros-garcon.service.d/cros-garcon-override.conf'
```

[go/crostini-ime]: https://goto.google.com/crostini-ime
[go/crostini-ime-tests]: https://goto.google.com/crostini-ime-tests
