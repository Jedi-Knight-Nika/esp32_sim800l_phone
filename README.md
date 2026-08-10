# ESP32 SIM800L Phone Wiring

This project uses an ESP32 DevKit, a SIM800L GSM module, and a small SSD1306
OLED screen.

## Parts

- ESP32 DevKitV1 / ESP32-WROOM-32
- SIM800L GSM module
- SSD1306 128x64 I2C OLED display
- Separate 3.7V to 4.2V power supply for the SIM800L
- Level shifter or voltage divider for the SIM800L RX pin
- Common ground wire between all modules

## Wiring

On many ESP32 DevKitV1 boards these pins are printed as `D17`, `D16`, `D21`,
and `D22`. Some boards only print the number, like `17` or `21`.

| ESP32 pin | Connects to | Notes |
| --- | --- | --- |
| D17 / TX2 | SIM800L RXD | Use a level shifter or voltage divider |
| D16 / RX2 | SIM800L TXD | Direct connection is usually OK |
| D21 / SDA | OLED SDA | I2C data |
| D22 / SCL | OLED SCL | I2C clock |
| 3V3 | OLED VCC | Some OLED boards also accept 5V |
| GND | OLED GND | Shared ground |
| GND | SIM800L GND | Must share ground with ESP32 |

## SIM800L Power

Do not power the SIM800L from the ESP32 3.3V pin.

The SIM800L needs its own 3.7V to 4.2V supply that can handle short current
spikes of about 2A. A weak supply is the most common reason the module keeps
resetting or fails to register on the network.

Connect the SIM800L power like this:

| SIM800L pin | Connects to |
| --- | --- |
| VCC | External 3.7V to 4.2V supply |
| GND | External supply ground and ESP32 GND |

## Optional Speaker

For the speaker test in the sketch, connect a small speaker or amplifier input
to the SIM800L speaker output:

| SIM800L pin | Connects to |
| --- | --- |
| SPK_P | Speaker positive / amp input positive |
| SPK_N | Speaker negative / amp input negative |

## Notes

- The OLED address in the code is `0x3C`. If your screen does not start, try
  changing it to `0x3D`.
- Keep the SIM800L power wires short and thick if possible.
- Always connect grounds together before testing serial communication.
- Insert a valid SIM card and attach the antenna before trying to call.
