# cbar

Fork of `bar`, which handles rendering via C instead of stdin. Used for my desktop, probably won't work for yours unless you tweak the sensors code, and how it reads from `/proc/stat`. Edit `render.c` to your liking, and `cbar.h` also contains some settings. `cbar.c` is largely internal code and hacks from a fork of `bar`.

## example
![Example](https://i.imgur.com/XFCePYv.png)