## Build Requirements

Only native build is supported. If you don't have an arm64 Debian device available, a T2A vm instance on GCP will be handy.

Run `src/deps.sh` to install prerequisites.

## Build instructions

Assume everything goes well, by running `src/build.sh` you will get a `baguette_image_<ARCH>.img.zst` ready to use.
