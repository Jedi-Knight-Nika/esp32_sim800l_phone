# ESP32 SIM800L Keypad Phone

This branch turns the project into a small old-style phone UI. It still uses
the OLED for now, but the code is written so the screen drawing can later be
moved to a simple Arduino LCD.

## What It Does

- Soft power button: press to boot, long press to shut down
- Boot and shutdown screens on the OLED
- Home screen with SIM/network state, operator name, and signal bars
- Keypad menu for calls and messages
- Outgoing calls
- Incoming call screen with answer/reject
- Basic SMS inbox and SMS writing
- Simple buzzer sounds for keys, SMS, and incoming calls

The power button is a software power button. It turns the UI/modem state off,
but it does not physically cut battery power. For true phone-style power off,
add a latching power circuit later.

## Code Layout

The main Arduino sketch file is still `esp32_sim800l_phone.ino`. The rest of
the code is split into Arduino tab files:

- `phone_utils.ino` - state helper, beeps, small string cleanup
- `sim800l.ino` - SIM800L AT commands, network status, operator parsing
- `oled_ui.ino` - OLED screens and drawing
- `keypad_input.ino` - 4x3 keypad scanning
- `sms_text_input.ino` - old-phone SMS text typing
- `phone_actions.ino` - call and SMS actions
- `power_flow.ino` - soft power button, boot, shutdown
- `input_router.ino` - keypad routing and Serial debug commands

## Parts

- ESP32 DevKitV1 / ESP32-WROOM-32
- SIM800L GSM module
- SSD1306 128x64 I2C OLED display
- 4x3 matrix keypad with `0-9`, `*`, `#`
- Small active or passive buzzer
- Push button for soft power
- Separate 3.7V to 4.2V power supply for the SIM800L
- Level shifter or voltage divider for SIM800L `RXD`
- Common ground between all modules

## Main Wiring

On many ESP32 DevKitV1 boards these pins are printed as `D17`, `D16`, `D21`,
and so on. Some boards only print the number, like `17` or `21`.

| ESP32 pin | Connects to | Notes |
| --- | --- | --- |
| D17 / TX2 | SIM800L RXD | Use level shifter or voltage divider |
| D16 / RX2 | SIM800L TXD | Direct connection is usually OK |
| D21 / SDA | OLED SDA | I2C data |
| D22 / SCL | OLED SCL | I2C clock |
| 3V3 | OLED VCC | Some OLED boards also accept 5V |
| GND | OLED GND | Shared ground |
| D18 | Power button | Other side of button goes to GND |
| D23 | Buzzer + | Buzzer - goes to GND |

## Keypad Wiring

The code expects a 4 row x 3 column keypad.

| Keypad pin | ESP32 pin |
| --- | --- |
| Row 1 | D13 |
| Row 2 | D14 |
| Row 3 | D27 |
| Row 4 | D26 |
| Column 1 | D25 |
| Column 2 | D33 |
| Column 3 | D32 |

If your keypad pins are not labeled, use a multimeter to find which pins are
rows and which are columns.

## SIM800L Power

Do not power the SIM800L from the ESP32 3.3V pin.

The SIM800L needs its own 3.7V to 4.2V supply that can handle short current
spikes of about 2A. A weak supply is the most common reason the module resets
or cannot register on the network.

| SIM800L pin | Connects to |
| --- | --- |
| VCC | External 3.7V to 4.2V supply |
| GND | External supply ground and ESP32 GND |

## Optional Call Audio

For real calls, wire the SIM800L audio pins to a small speaker/microphone setup.
The exact pins depend on your SIM800L board.

| SIM800L pin | Connects to |
| --- | --- |
| SPK_P | Speaker positive / amplifier input positive |
| SPK_N | Speaker negative / amplifier input negative |
| MIC_P | Microphone positive |
| MIC_N | Microphone negative |

## Keypad Controls

From the home screen:

- Number keys: start dialing
- `#`: open menu
- `*`: open inbox

In menus:

- `2`: move up
- `8`: move down
- `#`: select
- `*`: back

During calls:

- `#`: answer incoming call
- `*`: hang up or reject

Reading SMS:

- `2`: scroll up
- `8`: scroll down
- `4`: previous message
- `6`: next message
- `*`: back

Writing SMS uses old phone-style text input:

- `2` = A/B/C/2
- `3` = D/E/F/3
- `4` = G/H/I/4
- `5` = J/K/L/5
- `6` = M/N/O/6
- `7` = P/Q/R/S/7
- `8` = T/U/V/8
- `9` = W/X/Y/Z/9
- `0` = space/0
- `1` = punctuation
- `#` = send
- `*` = delete/back

## Notes

- OLED address is `0x3C`. If the screen does not start, try `0x3D` in the code.
- Insert the SIM card and connect the antenna before testing calls.
- Keep SIM800L power wires short and thick if possible.
- All grounds must be connected together.
- SIM800L is 2G GSM only, so calls/SMS require working 2G service from the SIM
  operator.
