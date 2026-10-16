DualDisplay portable Linux build

Run ./AppRun to launch. This bundle targets Linux x86_64 and includes the Qt runtime and Qt X11 platform plugin.
System graphics, X11/Wayland, font, and C library dependencies are still provided by the host OS.

Hardware builds bundle KAYA user-space libraries. A compatible KAYA driver/service and camera hardware must be installed on the host; the driver is not portable.
Stub builds do not include KAYA libraries or camera hardware access.
