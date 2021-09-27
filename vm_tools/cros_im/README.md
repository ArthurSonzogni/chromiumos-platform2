# cros\_im

This project implements support for using ChromeOS IMEs over the Wayland protocol
zwp\_unstable\_text\_input\_v1. Googlers: See go/crostini-ime for more details

This is currently under development and many desired pieces of functionality may
not work correctly or at all.

IME support for GTK3 Wayland applications as per below. Configuring sommelier
to allow the host compositor to handle Ctrl+Space is recommended.

```
$ meson build && cd build && ninja
# cd /usr/lib/x86_64-linux-gnu/gtk-3.0/3.0.0
# ln -s /path/to/cros_im/build/libim_cros_gtk.so immodules
# mv immodules.cache{,.bak}
# /usr/lib/x86_64-linux-gnu/libgtk-3-0/gtk-query-immodules-3.0 > immodules.cache
$ GTK_IM_MODULE=cros gedit
```
