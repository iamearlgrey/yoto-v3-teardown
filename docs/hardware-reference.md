# Yoto Player V3 hardware reference

Everything here comes from one board. Treat it as a good starting point, and check your own.

Source tags:

- **[Board]** seen, measured, or read on this V3
- **[Config]** from the V3-E board config in the stock firmware
- **[Datasheet]** from the Awinic AW88194/AW88194A and SGMicro SGM41513 datasheets
- **[Inferred]** reasoned from the above, not directly confirmed

![Main board, component side](../photos/06-board-component-side.jpg)

![Main board, display side](../photos/04-board-display-side.jpg)

## Board identification

| Item | Value | Source |
| --- | --- | --- |
| Product | Yoto Player, 3rd generation (V3) | [Board] |
| Main board silkscreen | YOTO_V3_M7A, dated 2023-6-19 | [Board] |
| Audio module | Separate small PCB soldered to the main board, labeled "Player V3 Audio V3". Designator U801 covers the whole module | [Board], FCC photos |
| MCU | ESP32-WROVER-E N8R8 (8 MB flash, 8 MB PSRAM), wafer revision 3.1 | [Board] |
| Matching firmware config | V3-E | [Config] |

**V3-E, not V3.** The firmware holds configs for several similarly named models. The V3-E entry is the one whose parts match this board: PI4IOE5V6416 expanders (V3 lists ET6416), an SGM41513 charger at 0x1A (V3 lists SGM41511), and the pins and amp addresses. Other entries describe single-amp models and an older player with an ES8388 codec at 0x10. GPIO assignments differ between entries, so read only from V3-E.

**One exception: the battery gauge.** V3-E lists a CW2215B at 0x64, but this board has a CW2015 at 0x62 and nothing at 0x64, which is the plain V3 entry's gauge. The fitted battery is also the plain V3 entry's part. So the board mixes the two entries. [Board]

## Security fuses

| Fuse | Value | Meaning |
| --- | --- | --- |
| FLASH_CRYPT_CNT | 0 | Flash encryption off |
| ABS_DONE_0 / ABS_DONE_1 | False | Secure boot off |
| BLOCK1–3 keys | All zeros | No keys burned |
| UART_DOWNLOAD_DIS | False | Serial download allowed |
| JTAG_DISABLE | False | JTAG available |

## I2C bus

SDA GPIO21, SCL GPIO25. [Config]

| Address | Chip | Function | Source |
| --- | --- | --- | --- |
| 0x08 | HUSB238 | USB-C power delivery | Scan, [Config] |
| 0x09 | ES8156 | Headphone DAC | Scan, [Config] |
| 0x18 | LIS2DH12 (U302) | Accelerometer | Scan, [Board]. WHO_AM_I (register 0x0F) reads 0x33 |
| 0x1A | SGM41513 (U100) | Battery charger | Scan, [Config], [Board]. Part ID register 0x0B reads 0x00, which is the plain SGM41513 |
| 0x20 | PI4IOE5V6416 | IO expander, ports p0–p1 | Scan, [Board], [Config] |
| 0x21 | PI4IOE5V6416 | IO expander, ports p2–p3 | Scan, [Board], [Config] |
| 0x34 | AW88194 | Speaker amp, **left** | Scan after wake-up, [Config], confirmed by ear [Board] |
| 0x37 | AW88194 | Speaker amp, **right** | Scan after wake-up, [Config], confirmed by ear [Board] |
| 0x51 | IT8563 / PCF8563-type | Real-time clock | Scan, [Config], [Board]. Seconds register counts. No backup cell: the "lost power" flag is set and the time resets at every power-up |
| 0x62 | CW2015 (U105) | Battery fuel gauge | Scan, [Board]. Asleep at power-up (mode register 0x0A = 0xC0). Writing 0x00 to 0x0A wakes it, and the cell voltage then reads correctly |
| 0x64 | Nothing | | Never answered, on USB-C or battery. The V3-E config's CW2215B isn't fitted |
| 0x65 | AW2028H (U301) | Night light LED driver | Scan, [Board]. Register 0x00 reads 0xB1, the AW2028H's chip ID, and the night light works with the datasheet registers |
| 0x75 | Unknown | | Scan. About nine registers (0x09 onward read 0xFF), static, same on USB-C and battery |

0x08 answers only when USB-C is plugged in, since the HUSB238 is powered from the cable. Everything else answers on battery alone. [Board]

Not on the bus: the Qi receiver (CV8013N, no I2C address in the config) and the NFC reader (UART).

## Charger and battery

From the SGM41513 datasheet and status registers read on this board. [Board], [Datasheet]

| Condition | Charger status (0x1A, registers 0x08 and 0x09) |
| --- | --- |
| USB-C only, battery unplugged | USB host input, 500 mA limit, input good. Thermistor input reads "cold" |
| Battery only | No input. Thermistor normal |
| Battery and USB-C | Fast charging. Thermistor normal |

- With no firmware controlling it, the charger runs in its standalone mode: charges to 4.208 V, 16-hour safety timer, and limits itself to 500 mA from USB. A full charge is slow. Stock firmware probably raises the limit.
- The "cold" reading with the battery unplugged means the battery's yellow wire (its thermistor) feeds the charger's TS input, SGM41513 pin 11. [Inferred]; continuity not yet checked.
- Battery read 3.57 V resting and 3.64 V on charge through the CW2015.
- The gauge's charge percentage reads 0, likely because no battery profile is loaded. Go by voltage.

## GPIO

From the V3-E config. Nearly every usable pin is taken.

| GPIO | Function |
| --- | --- |
| 0 | I2S MCLK, also the BOOT strapping pin |
| 1 / 3 | UART0 TX / RX, programming header |
| 2 | SD card D0 |
| 4 | Right encoder B |
| 5 | I2S BCLK |
| 12 | Unused in config. Strapping pin: never pull high at boot |
| 13 | Left encoder B |
| 14 | SD card CLK |
| 15 | SD card CMD |
| 16 / 17 | Used internally by PSRAM on WROVER modules |
| 18 | I2S LRCLK |
| 19 | I2S data out |
| 21 | I2C SDA |
| 22 | SPI MOSI, display |
| 23 | SPI SCLK, display |
| 25 | I2C SCL |
| 26 | Left encoder A (config also lists SPI MISO here, so a display driver shouldn't use MISO). Working with internal pull-ups, 4 counts per click [Board] |
| 27 | Right encoder A. Working with internal pull-ups, 4 counts per click [Board] |
| 32 / 33 | NFC reader UART RX / TX |
| 34 | IO expander interrupt, input only |
| 35 | Qi receiver temperature sensor, ADC |
| 36 | Ambient light sensor D300, ADC. Confirmed by continuity and in firmware: about 500 mV covered, about 2600 mV in bright light [Board] |
| 39 | Temperature sensor, ADC. Location unknown: not the speaker black wire or the battery's third wire. Reads a constant 3108 mV, the top of the ADC range, on USB-C and battery, so nothing is pulling it down. The sensor may need enabling [Board] |

Test points [Board], shown in [this photo](../photos/09-programming-header-detail.jpg):

| Test point | Signal |
| --- | --- |
| TP27 | GPIO0, BOOT |
| TP15 | GPIO1, UART TX |
| TP8 | GPIO3, UART RX |
| Unlabeled, below P200 | GPIO5, I2S BCLK |
| Unlabeled, below P200 | GPIO18, I2S LRCLK |

## IO expander

The config names expander pins IOX.\<port\>.\<bit\>, counting four 8-bit ports across two chips: p0–p1 on 0x20 and p2–p3 on 0x21. The port-to-chip order is [Inferred], and the speaker test is consistent with it.

| Pin | Function | Direction |
| --- | --- | --- |
| IOX.0.0 | Qi I2C interrupt | Output per config, unexpected |
| IOX.0.1 | RTC interrupt | Input |
| IOX.0.4 | Right encoder push, reads 0 when pressed [Board] | Input |
| IOX.0.5 | Left encoder push, reads 0 when pressed [Board] | Input |
| IOX.0.6 | Battery gauge alert | Input |
| IOX.0.7 | Qi status, active low | Input |
| IOX.1.0 | USB VBUS status, active low | Input |
| IOX.1.1 | Headphone detect | Input |
| IOX.1.2 | Tilt indicator | Input |
| IOX.1.3 | Power button, reads 0 when pressed [Board] | Input |
| IOX.1.4 | Charger status | Input |
| IOX.2.0–2.3 | Display LED driver chip selects | Output, high |
| IOX.2.4 | **Speaker amp control** | Output, high |
| IOX.2.5 | Power enable | Output, low |
| IOX.2.6 | Qi charge enable, active low | Output, high |
| IOX.2.7 | USB charge enable, active low | Output, low |
| IOX.3.0 | Level converter enable | Output, high |
| IOX.3.1 | Power hold, the power latch | Output, high |
| IOX.3.3 | Output power enable | Output, high |
| IOX.3.5 | Qi 5 W enable | Output, low |

Stock startup values (write outputs before directions so pins don't glitch):

| Chip | Output p0 (0x02) | Output p1 (0x03) | Direction p0 (0x06) | Direction p1 (0x07) |
| --- | --- | --- | --- | --- |
| 0x20 | 0xFF | 0xFF | 0xFE | 0xFF |
| 0x21 | 0x5F | 0xDF | 0x00 | 0xD4 |

In direction registers, 1 = input and 0 = output. Pull-ups are off. Input registers are 0x00 (p0) and 0x01 (p1) on each chip.

### Power off

Tested on battery, Nov 12. [Board]

- Clearing IOX.3.1 (the power hold) by itself does **not** cut power. Three tries, the board stayed on.
- This works: power down the amps, wait for the power button to be released, set every expander pin to input except IOX.3.0 (level converter) and IOX.3.1 (direction registers 0x20: 0xFF 0xFF, 0x21: 0xFF 0xFC), then clear IOX.3.1 (0x21 output register 0x03 = 0xDD). This follows the order in open-yoto's notes on Yoto's shutdown code ("Setting IOX pins to inputs … except for VIN_HOLD", then "Unlatching VIN_HOLD").
- Yoto's firmware only releases the hold on battery. With USB-C in, the board stays powered whatever the firmware does.

Which of the newly released outputs was keeping the board on is untested. [Inferred]

## Speaker amps: boost and idle

Tested Nov 12. [Board], register meanings [Datasheet]

| Register | At power-up | Notes |
| --- | --- | --- |
| 0x60 | 0x0402 | Boost control, not changed |
| 0x61 BSTCTRL2 | 0x0126 | Bits 6:4 BST_MODE = 2, listed as reserved. Documented: 0 transparent, 1 force boost, 5 smart boost 1, 6 smart boost 2 |
| 0x62 BSTVOUT | 0x2E38 | Bits 3:0 maximum boost voltage, not changed |

- In mode 2 and in force boost, the speakers hiss in quiet passages. Smart boost 2 was the quietest by ear, so the close-up firmware writes it before switching each amp on. Off (transparent) and smart boost 1 weren't compared.
- From about −9 dB, SYSST bit 13 (smart boost status) sets and stays set after the volume comes back down. HMUTE (PWMCTRL 0x08 bit 0) doesn't silence the hiss then; one amp even flagged clipping (SYSST bits 7 and 2) while muted.
- Powering the amplifier stage down (SYSCTRL 0x04 bit 1, AMPPD) when idle removes it. To wake: clear AMPPD, wait for SYSST bit 8 (switching), then clear HMUTE. This takes a few milliseconds and doesn't clip the start of a sound.

Yoto's firmware loads DSP firmware and tuning into the amps, and probably sets the boost mode too. None of this is needed for clean audio at desk volumes.

## Night light

AW2028H at 0x65, LED board on P303. Tested Nov 12. [Board], registers [Datasheet]

| Register | Value used | Meaning |
| --- | --- | --- |
| 0x00 | 0x55 to reset; reads 0xB1 | Reset / chip ID |
| 0x01 GCR | 0x01 | Chip enable |
| 0x03 IMAX | 0x01 | 18 mA maximum per channel (chip default). Options: 9, 18, 37, 75 mA. The LED ratings are unknown, so I stayed at the default |
| 0x04–0x06 LCFG1–3 | 0x00 | Manual mode (no hardware patterns) |
| 0x07 LEDEN | 0x07 | All three outputs on |
| 0x08 LEDCTR | 0x03 | Independent channels, linear dimming |
| 0x10–0x12 ILED1–3 | Brightness | Current, as a fraction of IMAX |
| 0x1C–0x1E PWM1–3 | Colour | Duty cycle per channel |

- LED1 is red, LED2 green, LED3 blue. No extra enable pin is needed.
- The LEDs lean green: yellow comes out lime. By eye against a white wall, white needed red 100 %, green 33 %, blue 27 %, with a 2.2 gamma curve. Your LEDs may differ.
- Yoto's firmware drives it in sync RGB mode instead (open-yoto's notes: "SYNC RGB mode, setting PWM1 to max (0xFF) and ILED1/2/3" for colour).

## Display

16×16 full-colour LED matrix on the reverse of the main board: four HT16D35A drivers (U400, U500, U600, U700), 768 LEDs. Tested Nov 12. [Board], commands [Datasheet]

**Wiring.** 3-wire SPI, write only: data GPIO22, clock GPIO23, mode 0. 1 MHz with about 3 µs between bytes worked. Chip selects are IOX.2.0–2.3 (expander 0x21, output register 0x02, bits 0–3, active low). GPIO26 is listed as MISO in Yoto's config but is the left knob, so leave it alone.

**Bands.** Chip *c* (select bit *c*) lights rows 4*c* to 4*c*+3, all 16 columns.

**Start-up, per chip** (each command in its own chip-select frame):

| Command | Data | Meaning |
| --- | --- | --- |
| 0xCC | | Software reset |
| 0x80 0x00 | 252 × 0x00 | Clear display RAM |
| 0x41 | 0xFF | COM outputs on |
| 0x42 | 0xFF 0xFF 0xFF 0x0F | ROW outputs on |
| 0x43 | 0x00 | No blinking |
| 0x31 | 0x00 | Gray (PWM) mode: 64 levels per LED |
| 0x32 | 0x07 | Scan all COM lines |
| 0x33, 0x34, 0x38 | 0x00 | Defaults |
| 0x36 | 0x07 | Constant current, a safe middle setting |
| 0x37 | 0–63 | Global brightness (16 used) |
| 0x35 | 0x02, then 0x03 | Oscillator on, then display on |

**Memory layout.** Write 0x80, a start address, then the data; each byte is one LED's brightness (0–63). The board doesn't follow the datasheet's "COM × 0x20 + ROW" layout. The LEDs sit in runs of 24 addresses (8 pixels × red, green, blue), one run every 28 addresses starting at 0x1C. The last 4 addresses of each 28 aren't connected, and 0x00–0x1B is dark.

For pixel (*x*, *y*), *x* 0–15 left to right and *y* 0–15 top to bottom:

```
chip = y / 4
j    = (y % 4) * 2 + (x / 8)
run  = (j == 0) ? 8 : j
red  = 28 * run + (x % 8) * 3      // green = red + 1, blue = red + 2
```

- Filling all 252 addresses as if they were the datasheet layout gives a stair-step of red, blue, yellow and dark pixels: the unconnected bytes push everything out of line.
- open-yoto drives this display on/off only; gray mode works and gives smooth fades.
- The LEDs' safe maximum current and their colour balance are untested.

## Storage

**microSD** (GPIO2/14/15), about 171 MB used on my card. It holds cached card audio in randomly named folders, plus card data, icons, app settings, and a resume record. Settings include schedules, brightness, volume caps, and a 1-hour auto shutdown.

**Flash layout** (8 MB), from the dump's partition table:

| Offset | Size | Name | Type |
| --- | --- | --- | --- |
| 0x001000 | 28 KB | bootloader | Boot |
| 0x008000 | 4 KB | partition table | Table |
| 0x009000 | 192 KB | nvs | Settings |
| 0x039000 | 8 KB | otadata | Firmware slot selection |
| 0x03B000 | 4 KB | phy_init | Radio calibration |
| 0x040000 | 2560 KB | factory | Firmware |
| 0x2C0000 | 2560 KB | ota_0 | Firmware update slot |
| 0x540000 | 2560 KB | ota_1 | Firmware update slot |

## Connectors

Everything inside uses plug-in cables, so the player comes apart and goes back together without soldering.

| Connector | Wires | Goes to | Source |
| --- | --- | --- | --- |
| Left and right speakers, bottom edge | 3 each: red, black, white | 8 Ω, 5 W speakers. Red and white reach the speaker, reading 8.2 Ω and 6.7 Ω. Black stops inside the foam tape, reads open to red and white, and connects to ground on the board, so it's likely a shield | [Board], shield [Inferred] |
| Battery, bottom right | 3 | A&S 21700 cell, 3.7 V, 4500 mAh, with a small PCB on top. Yellow is the third wire. It reads 11.36 kΩ to black at room temperature, which fits a 10 kΩ thermistor. It doesn't connect to GPIO39 or ground on the board. The charger's behavior shows it goes to the SGM41513 thermistor input | [Board], thermistor [Inferred] |
| Encoders, top left and top right | 4 each | The two dials | [Board] |
| NFC, left edge | 2 | NFC antenna board under the card slot | [Board] |
| P201, bottom | 7 | Qi receiver at the base | [Board] |
| P303, bottom | 6 | Night-light LEDs around the rear edge | [Board] |
| ESP32 antenna socket | Coax | External Wi-Fi antenna cable, routed under a panel hook. Easy to leave unplugged after a teardown: without it, my router read −88 dBm instead of −52 dBm and WiFi logins failed. A socket usually means the -IE (external antenna) module variant; the can label isn't rechecked yet | [Board] |

The V3-E config lists a different battery (EVE-LJDX90X-4900) than the fitted A&S cell, which is the plain V3 entry's part. The CW2015 gauge is also the plain V3 entry's, so this board mixes parts from both entries. Everything else tested from the V3-E entry has worked, including the amps, expanders and I2S pins. [Board]

## Chips

| Designator | Marking | Part | Function | Status |
| --- | --- | --- | --- | --- |
| U200 | ESP32-WROVER-E | ESP32 module | Main processor | Confirmed |
| U201, U205 | PI4IOE5V6416 | PI4IOE5V6416 | IO expanders | Confirmed |
| U206 | SGM3002 XMS 2146N | SGMicro part | Likely a voltage regulator | Inferred |
| Likely U223 | 8563 | IT8563 / PCF8563-type | Real-time clock | Part confirmed, designator crowded |
| U400, U500, U600, U700 | HT16D35A | HT16D35A | Display LED drivers, 16×16 grid on the reverse of the board | Confirmed |
| U801 | "Player V3 Audio V3" | Audio module | Holds both amps and boost inductors | Confirmed |
| On U801, no designators | UV25 | AW88194ACSR | Speaker amps | Confirmed |
| U800 | ES8156 | ES8156 | Headphone DAC | Confirmed |
| U803 | UM3208 | UM3208 | Level translator | Confirmed |
| U900 | CR95HF | CR95HF | NFC reader | Confirmed |
| U301, second use of this label | HY 27.120 | 27.12 MHz crystal, very likely | Metal can with four corner pads, beside U900. The silkscreen appears to reuse U301, which is also on the AW2028H | Very likely |
| U103 | HUSB238 | HUSB238 | USB-C power delivery | Confirmed |
| U101 | ASCM | Unidentified, likely a switching regulator | 6 pins, near the top of the board, beside L102 and U206. L102 isn't directly on the 3.3 V rail and has a diode-like path to the battery connector. Exact function unknown | Inferred |
| U100, silkscreen hidden | SGM41513 | SGM41513 | Battery charger (0x1A) | Confirmed by part ID register |
| U300 | | microSD socket | Storage | Confirmed |
| D300 | | Light sensor, part unidentified | Ambient light sensor on GPIO36, seen through an opening in the LED frame, with R307, R308 and C306 beside it | Confirmed by continuity |
| U302 | 4D5, with data matrix | LIS2DH12 | Accelerometer (0x18), next to TP40. About 2 mm square, package matches the datasheet, pin 1 connects to SCL | Confirmed by WHO_AM_I |
| U301 | 28H DB | AW2028H | Night light LED driver (0x65), on the main board below the 6-pin connector. 8 pins: 3 on two sides, 1 on the others | Confirmed by chip ID and in firmware |
| Night light strip | Not removed | Glued in place | Rear LED strip around the back of the sound module. The driver is on the main board | Observed |
| Qi board | Not removed, glued and screwed in place | CV8013N, likely | Wireless charging receiver on the bottom of the sound chamber, with a ring of magnets around the coil | Inferred |
| U105 | 2015CH BDLR23 | CW2015CHBD | Battery fuel gauge (0x62), on the opposite side of the board near the battery connector | Confirmed in firmware |
| U105A | Empty | Not fitted | 3×3 pad grid beside U105. Fits a WLCSP-9, the package listed for the CW2215B, so likely an alternate gauge footprint | Inferred |
