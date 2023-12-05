# esphome-gdo [![Made for ESPHome](https://img.shields.io/badge/Made_for-ESPHome-black?logo=esphome)](https://esphome.io)

This [ESPHome](https://esphome.io) package allows control of a Garage Door Opener with a relay and one or two reed sensors. Supports:

- open/close/stop control
- most importantly position reporting and control, distinguishing it from other similar projects
- obstruction sensor

See the included `example-gdo.yaml` for my personal setup with just one reed sensor at the fully-open position.

## Hardware requirements

- A Garage Door Opener that you open/close with a single button. The behavior of the button is expected to be:
  - If door is closed a single press opens it.
  - If door is (fully or partially) open a single press closes it.
  - If door is opening a single press stops it.
  - If door is closing a single press opens it.
- ESP board [compatible](https://esphome.io/#devices) with ESPHome.
- Relay to either press the physical button of the wall control panel (for Chamberlain Security + 2.0) or short the controls on the garage door opener itself (for Chamberlain Security + 1.0 or Genie etc.).
- One or two reed sensors to detect the fully-open and/or fully-closed states. If using a single reed sensor, it can be placed in either fully-open or fully-closed positions.
- Optional two 10kΩ resistors to detect the obstruction sensor.

## Credits

- Adopted from [Endstop Cover](https://esphome.io/components/cover/endstop) and [Time Based Cover](https://esphome.io/components/cover/time_based).
- The circuit for the obstruction sensor is from [rat-ratgdo](https://github.com/Kaldek/rat-ratgdo).
- The code for the obstruction sensor is from [esphome-ratgdo](https://github.com/ratgdo/esphome-ratgdo).

## Dev notes

```sh
python3 -m venv .venv
source .venv/bin/activate
# for Windows CMD:
# .venv\Scripts\activate.bat
# for Windows PowerShell:
# .venv\Scripts\Activate.ps1

pip install esphome pre-commit

pre-commit install

pre-commit run --all-files

# Compile with local code instead of pulling from Github
esphome -s external_components_source components compile example-gdo.yaml

# Deploy local code
esphome -s external_components_source components run example-gdo.yaml
```
