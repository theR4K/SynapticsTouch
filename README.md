# Synaptics Touch RMI4 Controller Driver
In this repository you can find the Synaptics Touch RMI4 controller driver for Windows (KMDF).
This driver support Synaptics RMI4 3200 and 3400 touch controllers (Implements both the F11 and F12 functions).

## Disclaimer
This driver is not finished.
It is untested on Windows 10 and F12 support was not tested on a device due to lack of device.
It contains debuging code and may be missing comments as well.
In the master branch Tracing WPP calls have been replaced to DbgPrint (to help with debugging on builds without Symbols available).

Have fun =)
