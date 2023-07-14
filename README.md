# SMA Inverter to MQTT via ESP32

This project uses an ESP32 to connect to an SMA SunnyBoy inverter, using Bluetooth, and regularly publish generation data via MQTT. There is also an option to publish the required topics to allow Home Assistant to automatically detect the various sensors.

It was originally forked from https://github.com/stmcodes/ESP32_to_SMA_ESPHome, https://github.com/habuild/ESP32_to_SMA, https://github.com/sillyfrog/ESP32_to_SMA, https://github.com/SBFspot/SBFspot and finally from https://github.com/keerekeerweere/ESP32_to_SMA_ESPHome.


This is working for me - but the code is some what of a mess and could do with some (lots of) cleaning up.
The version of keerekeerweere didn't work for me. This one is.

## Setup

All required configuration is done in the `site_details.h` file, to create this, copy the `site_details-example.h` file to `site_details.h` and update all of the constants accordingly.

The project is developed and designed for use in VScode/PlatformIO. It may work in Arduino, but I would _highly_ recommend using PlatformIO as it'll also automatically get the libraries etc that are required.

*Check out Gitpod, this fork works great on that platform!
execute "pip install platformio" in terminal first.
Then "pio run".
*

## Flashing the image

The image initially needs to be flashed via Serial, update your `platformio.ini` accordingly to match your setup.

Once the first image is uploaded, and it's connected to the network, you can update using OTA updades:

- Build the `firmware.bin` file (PlatfromIO Build or pio run) and flash through serial connection.
- If the firmware is already loaded, you can update a new firmware by surfing to http://IPNR/update

If you have any questions, please create a GitHub issue and I'll try to help.
