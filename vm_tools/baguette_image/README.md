## How to use the image?

Note: This instruction is subject to change. arm64 images are testesd on trogdor, amd64 images are tested on brya.

Prerequisites: A DUT running at least R134 with crostini installed (for the DLC and updated vmc tools).

On workstation:
`scp /path/to/baguette_rootfs.image.zst root@<DUT IP>:/home/chronos/user/MyFiles/Downloads`

On DUT:
`vmc create --size 15G --source /home/chronos/user/MyFiles/Downloads/baguette_rootfs.image.zst`

On DUT:
```
vmc start --vm-type baguette \
          --kernel-param "root=/dev/vdb rw net.ifnames=0 systemd.log_color=0" \
          baguette
```

What to look for:

- You will be welcomed by a shell logged in to the `chronos` user.
- If you install `mesa-utils` and run `DISPLAY=:0 glxgears`, the gears should show up on your display.
- If you run `df -h`, you should see your entire VM disk space has been occupied by the rootfs.
- If you install `featherpad`, you should see the app icon on your chrome app selector (but clicking it will lead to a window asking you to install borealis).
- If you install `qtwayland5` too, `QT_QPA_PLATFORM=wayland featherpad` will show you featherpad running on wayland.

Known issue:

- Sommelier might not have been started before profile scripts were run, causing first entry of shell not having display-related envs set

**Happy Testing!**
