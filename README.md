# cbar

Fork of `bar`, which handles rendering via C instead of stdin. Used for my desktop, probably won't work for yours unless you tweak the sensors code, and how it reads from `/proc/stat`. Edit `render.c` to your liking, and `cbar.h` also contains some settings. `cbar.c` is largely internal code and hacks from a fork of `bar`.

The reason for this fork is because I do not like the 'unix style' approach of gluing shit together to get my panel, I would rather write the whole thing in C and not have needless bloat from other processes handling things line CPU, temp, and memory info. I am quite satisfied with the result.

## example
![Example](https://i.imgur.com/XFCePYv.png)