xireloadd: xinputrc reload daemon; xidwait: XInput2 device hotplug monitor
---

xireloadd re-runs your ~/.xinputrc every time an input device is hotplugged to
X11.

If your manually-configured `xinput`/`xset`/setxkbmap` calls in your X startup
scripts are getting reset while, e.g., adding/removing devices, or using a KVM
switch, or turning your USB hub off/on, you might be interested in this.

To use, simply move all your input-related commands out of your X startup
scripts and into $HOME/.xinput, then add a single instance of `xireloadd &` to
your startup scripts. xireloadd will run until the X server stops.

xireloadd is just a bash script and is built on top of **xidmon**, which reports
XInput2 device add/remove events to standard output. Event bursts are coalesced
so as to avoid spamming.

xireloadd and xidmon are currently pre-alpha software.
