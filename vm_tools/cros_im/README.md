# cros\_im

This project implements support for using ChromeOS IMEs over the Wayland
protocols `zwp_unstable_text_input_v1` and
`zcr_text_input_extension_unstable_v1`. Googlers: See [go/crostini-ime],
[go/crostini-ime-rollout] and [go/crostini-ime-tests] for additional design
details.

IME Support is currently limited to GTK3/4 applications (including Electron-based
apps), and only Debian Bullseye/Bookworm containers are officially supported.
Known issues are tracked [here][issue hotlist] and bugs can be reported
[here][new issue].

## FAQ

### App support

- Libreoffice: Please ensure the GTK3 plugin is installed:
`sudo apt install libreoffice-gtk3`.
- Anki: Only the Qt5 version is supported currently, and the Qt5 flag must be enabled (see
[below](#system-configuration)). If you manually downloaded and installed Anki, you will need to run:
```bash
sudo ln -s {/usr/lib/*/qt5,/usr/local/share/anki/lib/PyQt5/Qt5}/plugins/platforminputcontexts/libcrosplatforminputcontextplugin.so
```

## System configuration
The #crostini-ime-support flag in chrome://flags must be enabled to fully
enable Crostini IME support. It sets the env var `GTK_IM_MODULE=cros` globally
in Crostini and configures ChromeOS to work correctly with Crostini IMEs.
It is enabled by default from M116.

The Qt5 IM module can be enabled via the #crostini-qt-ime-support flag, which
sets the env var `QT_IM_MODULE=cros`.

From M112, `cros-im` is installed by apt automatically.

If manually building cros\_im, it is recommended to use a device on dev channel
as backwards compatibility with older versions of ChromeOS is not guaranteed.

### IME switching shortcuts

If you use Ctrl+Space or Ctrl+Shift+Space to switch IMEs, you may want to
configure sommelier to allow the host compositor to handle these. This
configuration is *not* officially supported by the Crostini team.

```bash
setup_conf() {
  mkdir -p "$1"
  echo -e '[Service]\nEnvironment="SOMMELIER_ACCELERATORS=Super_L,<Alt>bracketleft,<Alt>bracketright,<Control>space,<Control><Shift>space"' > "$1"/override.conf
}
setup_conf ~/.config/systemd/user/sommelier@.service.d
setup_conf ~/.config/systemd/user/sommelier-x@.service.d
# This will likely kill all running GUI apps
systemctl --user daemon-reload
systemctl --user restart sommelier{,-x}@{0,1}.service
```

## Manual build instructions
### Compiling
cros\_im can be compiled as follows:

```bash
git clone https://chromium.googlesource.com/chromiumos/platform2
cd platform2/vm_tools/cros_im
sudo apt install -y clang googletest libgtk-3-dev libgtkmm-3.0-dev libwayland-bin meson pkg-config xvfb weston dpkg-dev qtbase5-dev qtbase5-private-dev
meson build && cd build && ninja
```

Additional commands to compile with GTK4 support (from bookworm onwards):
```bash
sudo apt install -y libgtk-4-dev libgtkmm-4.0.dev
meson configure -Dbuild_gtk4=true && ninja
```

### Testing
Automated tests can be run from a build directory with `meson test`. This
invokes `../test/run_tests.py`, which can also be run directly if needed.

The GTK3 IM module can be manually tested by setting up a custom IM module cache:
```bash
/usr/lib/*/libgtk-3-0/gtk-query-immodules-3.0 im-cros-gtk3.so > dev-immodules.cache
export GTK_IM_MODULE_FILE=$(pwd)/dev-immodules.cache
```

The Qt IM module cannot be easily tested prior to installation.

### Installing
cros\_im can be installed as follows, although this will conflict with the IM
module installed by the `cros-im` apt package...
```bash
meson configure --prefix /usr && sudo meson install
# Manually update GTK's IM modules cache. For IM modules installed from a .deb,
# this would be automatically run by libgtk-3.0's postinst hook.
sudo /usr/lib/*/libgtk-3-0/gtk-query-immodules-3.0 --update-cache
```

## .deb build
.deb packages can be built for Bullseye and Bookworm on supported architectures
(arm64, amd64, i386) by running `build-packages`. The version number is the
result of `git rev-list --count HEAD`.

These steps do not work from within Crostini as LXC containers in Crostini are
unprivileged and do not have permission to run the commands required.

```bash
git clone https://chromium.googlesource.com/chromiumos/platform2
cd platform2/vm_tools/cros_im

# Build for a specific dist/arch, change args as needed.
sudo ./build-packages bookworm amd64

# Show build artifacts
ls -l *debs/
```

[go/crostini-ime]: https://goto.google.com/crostini-ime
[go/crostini-ime-rollout]: https://goto.google.com/crostini-ime-rollout
[go/crostini-ime-tests]: https://goto.google.com/crostini-ime-tests
[new issue]: https://issuetracker.google.com/issues/new?component=1161264&template=1747723
[issue hotlist]: https://issuetracker.google.com/hotlists/4536324?s=resolved_time:asc&s=priority:asc
