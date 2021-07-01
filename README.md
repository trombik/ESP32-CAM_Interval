ESP32-CAM Interval
==================

__Status Update:__ Forked from [dimhoff/ESP32-CAM_Interval](https://github.com/dimhoff/ESP32-CAM_Interval).

This firmware turns a ESP32-CAM module into a low power time-lapse camera.
Taking pictures at a set interval, and storing them to SD card. In between
captures the device will go into deep sleep mode to save battery power.

About the fork
--------------

The fork is intended to:

* follow updates in dependency
* modify the firmware to my needs

Compilation
-----------

Use [`platformio`](https://platformio.org/) to build.

```console
sh gen_html_content.sh
pio run
```

To upload, run:

```console
pio run -t upload --upload-port /dev/cuaU0
```

`/dev/cuaU0` should be replaced with the path to serial port on your machine.

The tzinfo.json file can be updated with the tool from
https://github.com/pgurenko/tzinfo. But this is normally not necessary.

## Build flags

Use `src_build_flags` in `platformio.ini` to set build flags.

Optionally, you may create `platformio_user.ini` to override defaults.

```ini
; platformio_user.ini
[common]
src_build_flags = -DWITH_SLEEP -DWITH_SD_4BIT
```

### `WITH_SLEEP`

Enable deep-sleep in between pictures if defined.

### `WITH_FLASH`

Enable Flash LED support if defined.

### `WITH_CAM_PWDN`

Enable Camera Power down support if defined. Note that this requires a
modification on the AI-Thinker ESP32-CAM boards.

### `WITH_EVIL_CAM_PWR_SHUTDOWN`

Shutdown camera low voltage regulators.  This shuts down the camera 1.2 and
2.8 Volt regulators in sleep state. This brings down the current
consumption to about 4 mA. However it leaves the camera in a half powered
state since the 3.3 Volt is not shut down. Doing this is probably not
according to spec. Using the `WITH_CAM_PWDN` build flag instead is suggested.

### `WITH_SD_4BIT`

Enable 4-BIT bus signalling to SD card(if supported by card) if defined. With
this flag:

* The bus width is 4 bit (slightly faster access) instead of 1 bit
* The microSD card will use the `GPIO4`, `GPIO12`, `GPIO13` data lines (you
  cannot use the pins for other purpose)
* The onboard LED flashlight blinks when accessing SD card

The flag should not be defined in most cases.

### `WITH_SETUP_MODE_BUTTON`

Require Button to enter Set-up mode Normally Set-up mode is automatically
entered upon first boot. However if the camera is in a privacy sensitive
location this also mean that if the camera reboots accidentally, it will start
an open Wi-Fi AP through which you can see the camera images. In these cases
you can use a button/switch between `GPIO12` and ground. Only if the button is
pressed upon first boot the camera will go into set-up mode.

Picture Names
-------------
Every time the device boots a new directory is created on the SD card. The
directory name is created following the template 'timelapseXXXX', where 'XXXX'
is replaced by a free sequence number. Pictures are stored to this directory.

The picture filenames contain the date and time of taking the pictures. If the
time is not set, the clock will start at UNIX epoch, i.e. 01-01-1970 00:00:00.

Set-up mode
-----------
When the camera is powered up it will go into set-up mode, or time is not
set. In this mode the camera has an open Wi-Fi access point and a web server.
Connect to the access point and go to http://camera.local to configure the
camera and enable it.

Upon opening the set-up mode web site the camera's clock is automatically
synchronized to the browsers clock.

In set-up mode the LED will blink at a 1 second interval.

Configuration file
------------------
To configure the software create a file named camera.cfg in the root
directory of the SD card. The configuration file is text based containing one
configuration option per line. Configuration options have the format
'key=value'. Spaces are striped from the front and back of both the key and
value. Lines starting with a '#' and empty lines are ignored.

For a full list of available configuration options, see the camera.cfg example
configuration.

Power saving
------------
The firmware puts the device in deep sleep if the interval between images is
big. However the camera is _not_ powered down. This is because the pin required
to properly power down the camera is not connected to the MCU by default on the
ESP32-CAM board. While the CAM_PWR pin that is available only switches of the
1.2 and 2.8 Volt voltage regulators. This only partially powers down the camera
and leaves it in some undefined state. This is probably not according to spec

To properly power down the camera a modification must be made to the PCB. For
details see doc/power_consumption.md.

Generating video file from the pictures
---------------------------------------

You need to install [`ffmpeg`](https://ffmpeg.org/).

To generate video file from the pictures, mount the SD card on your machine,
run:

```console
ffmpeg -framerate 5 -pattern_type glob -i "/mnt/timelapse0017/*.jpg" output.mp4
```

Troubleshooting
---------------
If the red LED is flashing two short pulses every 2 seconds, this means an fatal
software error occurred.

The only way to troubleshoot issues is to connect to the serial port and check
the logging.

The serial port uses the following settings: 115200 Baud, 8N1.

Notes
-----
The following things are important to know about the ESP32-CAM board hardware:

 - The OV3640(/OV3660?) camera is NOT supported by the ESP32-CAM board. Because
   the camera uses a different core voltage, 1.5 V instead of 1.2 V. And the
   maximum Vdd I/O is only 3.0 V, instead of 3.3 V.
 - Other cameras then the OV2640 are unlikely to work. Because the driver uses
   the old parallel DVP bus to communicate to the camera. Most newer cameras
   use the serial MIPI bus instead.
 - GPIO12/HS_DATA2 pin is free for use in 1-bit SD-card bus mode. However, this
   pin is used by the ESP-32s module as strapping pin to configure the VDD_SDIO
   voltage, and thus must be low or floating at boot. See:
   https://wiki.ai-thinker.com/esp32/spec/esp32s
