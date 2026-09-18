# esphome-gdo [![Made for ESPHome](https://img.shields.io/badge/Made_for-ESPHome-black?logo=esphome)](https://esphome.io)

This [ESPHome](https://esphome.io) external component allows control of a Garage Door Opener with a relay and one or two reed sensors. Supports:

- open/close/stop control
- most importantly position reporting and control, distinguishing it from other similar projects
- obstruction sensor

See the included `example-gdo.yaml` for my personal setup with just one reed sensor at the fully-open position.

## Configuration

Add the component to your config:

```yaml
external_components:
  - source: github://tronikos/esphome-gdo@main
```

### `cover` platform `gdo`

| Option                | Type     | Description                                                                                            |
|-----------------------|----------|--------------------------------------------------------------------------------------------------------|
| `open_duration`       | Required | Time the door takes to travel from fully closed to fully open.                                         |
| `close_duration`      | Required | Time the door takes to travel from fully open to fully closed.                                         |
| `single_press_action` | Required | Automation that pulses the relay once.                                                                 |
| `double_press_action` | Required | Automation that pulses the relay twice.                                                                |
| `triple_press_action` | Optional | Automation that pulses the relay three times. Required with `press_while_closing: stop`.               |
| `open_endstop`        | Optional | ID of a binary sensor that reads on when the door is fully open.                                       |
| `close_endstop`       | Optional | ID of a binary sensor that reads on when the door is fully closed.                                     |
| `press_while_closing` | Optional | What the opener does when the button is pressed while the door is closing: `open` (default) or `stop`. |

At least one of `open_endstop` / `close_endstop` is required. Without one the
position estimate can never be corrected, so it drifts.

The durations are used two ways: to interpolate the position while the door
moves, and as the basis for the endstop timeout. The timeout allows 25% plus 2s
over the configured duration before it gives up and reports an unknown position,
so the durations do not have to be exact to the millisecond.

Position is held just short of 100% / 0% until the corresponding endstop
actually confirms it, so the cover does not read "fully open" while the door is
still moving.

A double or triple press only starts the door travelling on its last press, so
while one is still running the position estimate is held and no stop is
triggered from it. Otherwise the estimate runs ahead of a door that has not
moved yet, and on a short move it can cut the press sequence short before its
remaining presses have fired.

A command that arrives while the relay is still working through a press is held
until that press has finished, rather than cutting it short. The newest command
wins, so mashing the buttons leaves the door doing whatever was asked last.

The component never asks the opener for a direction, it only presses the button,
so how many presses a command needs depends on what the door is doing. That is
derived from `press_while_closing`, the one button behavior that differs
between openers:

- `open` (default): a press while the door is closing reverses it, and a press
  on a door standing partway closes it. Chamberlain, LiftMaster and Genie work
  this way. At most two presses are ever needed.
- `stop`: a press while the door is closing stops it, mirroring what a press
  does while the door is opening. These openers run an impulse sequence, open -
  stop - close - stop - open, so a press on a door standing partway travels
  against the direction the door last moved in. Hörmann, Sommer and Marantec
  work this way. Sending a door that was stopped partway on again the same way
  it was already going takes three presses (start it back, stop it, go), which
  is why `triple_press_action` is required in this mode.

In `stop` mode the direction the door last moved in is only known once the door
has moved, so after a restart with the door standing partway the component
refuses to press anything and says so in the log. Moving the door to either
endstop, with the opener's own button if need be, makes it predictable again.

### `binary_sensor` platform `gdo`

Reports the state of the safety obstruction sensor.

| Option           | Type     | Description                                                                                                       |
|------------------|----------|-------------------------------------------------------------------------------------------------------------------|
| `input_obst_pin` | Required | Pin wired to the obstruction sensor circuit. Must be a pin on the ESP itself, since it is read with an interrupt. |

Defaults to `device_class: problem` and `entity_category: diagnostic`.

## Hardware requirements

- A Garage Door Opener that you open/close with a single button. The behavior of the button is expected to be:
  - If door is closed a single press opens it.
  - If door is (fully or partially) open a single press closes it.
  - If door is opening a single press stops it.
  - If door is closing a single press opens it. Openers that stop the door
    instead, and then move it against its last direction of travel, are
    supported with `press_while_closing: stop`.
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

# Validate the same configs CI does
esphome config tests/test-*.yaml
```
