# Yoto Player V3 teardown

Teardown notes for the full-size Yoto Player 3rd generation, plus Arduino sketches for it. The main one, [`yoto_v3_closeup`](firmware/yoto_v3_closeup/), plays WAV files in stereo through both original speakers and runs with the case closed: WiFi updates, a web page for the SD card, safe mode and power off. Smaller sketches test the amps, battery and sensor chips. It's tested on real V3 hardware and uses the datasheet power-up sequence, with no Yoto firmware or DSP blobs.

**The key finding:** the V3 uses two AW88194 smart amps at I2C 0x34 and 0x37. They stay in reset, and invisible to an I2C scan, until IO expander pin IOX.2.4 goes high. Stock firmware does that at boot. Custom firmware has to do it too.

[**Video**](https://github.com/user-attachments/assets/d955e8e3-7148-4fcc-8168-46cc7206af2c)




![Yoto Player V3 main board with chips and connectors labeled](photos/06-board-component-side.jpg)

## What's here

| Path | What it is |
| --- | --- |
| [`firmware/yoto_v3_closeup/`](firmware/yoto_v3_closeup/) | **Start here.** Stereo WAV player with both knobs, WiFi updates, a web page (log, SD files, battery), safe mode, power off, and start-up and shutdown sounds. `yoto_base.h` is reusable for your own sketches |
| [`firmware/yoto_v3_amp_test/`](firmware/yoto_v3_amp_test/) | Arduino sketch that wakes both amps and beeps |
| [`firmware/yoto_v3_stereo/`](firmware/yoto_v3_stereo/) | True stereo: each amp plays its own channel, with a left-to-right sweep |
| [`firmware/yoto_v3_left_right/`](firmware/yoto_v3_left_right/) | Beeps each amp alone, to tell left from right |
| [`firmware/yoto_v3_bus_survey/`](firmware/yoto_v3_bus_survey/) | Read-only I2C survey: gauge, accelerometer, register dumps, light sensor and GPIO39 |
| [`firmware/yoto_v3_battery_watch/`](firmware/yoto_v3_battery_watch/) | Wakes the fuel gauge and prints battery voltage and charger status |
| [`docs/disassembly.md`](docs/disassembly.md) | Opening the player without breaking it |
| [`docs/download-mode.md`](docs/download-mode.md) | Wiring, download mode, backup and restore |
| [`docs/hardware-reference.md`](docs/hardware-reference.md) | I2C, GPIO, IO expander, flash, connector and chip maps |
| [`photos/`](photos/) | Teardown and board photos |

## Waking the speakers

1. Write the stock IO expander values, outputs before directions. This sets IOX.2.4 high and releases the amps. It also sets the power hold, IOX.3.1.

   | Chip | Output p0 (0x02) | Output p1 (0x03) | Direction p0 (0x06) | Direction p1 (0x07) |
   | --- | --- | --- | --- | --- |
   | 0x20 | 0xFF | 0xFF | 0xFE | 0xFF |
   | 0x21 | 0x5F | 0xDF | 0x00 | 0xD4 |

2. Wait at least 5 ms. 0x34 and 0x37 should now answer a scan. Register 0x00 on each reads 0x1806.
3. Start I2S: BCLK GPIO5, LRCLK GPIO18, data GPIO19, MCLK GPIO0. Keep clocks running at least 5 ms, since the amp's PLL locks to BCLK by default.
4. Write 0x05 (I2SCTRL). `0x0C08` gives mono (L+R)/2, standard I2S, 16-bit, BCLK 32fs, 48 kHz. For stereo, write `0x0408` (left channel) to 0x34 and `0x0808` (right channel) to 0x37. Channel select is bits 11:10: 1 left, 2 right, 3 mono mix.
5. In 0x04 (SYSCTRL), set I2SEN (bit 6) and clear PWDN (bit 0), keeping AMPPD (bit 1) set. Poll 0x01 until bit 0 (PLL locked) is 1.
6. In 0x04, clear AMPPD. Poll 0x01 until bit 8 (switching) is 1.
7. In 0x08 (PWMCTRL), clear HMUTE (bit 0). Hardware mute is on by default.
8. Set volume in the upper byte of 0x0F: bits 7:4 × 6 dB, bits 3:0 × 0.5 dB, down from 0 dB.

Amp registers are 16-bit, sent high byte first after an 8-bit register address. Use read-modify-write on 0x04 to keep reserved bits. If it stays silent, register 0x01 is the place to look: PLL locked (bit 0), clocks stable (bit 4), no PLL reference (bit 5), switching (bit 8), boost started (bit 9).

## Test results

Run Nov 1, 2026, powered from USB-C with the original speakers attached and the battery unplugged. Volume −24 dB, 30% sine at 480 Hz, 48 kHz 16-bit, mono mix on both amps. The tone was clearly audible from both speakers.


| Check | Result |
| --- | --- |
| I2C scan after expander setup | 0x08 0x09 0x18 0x1A 0x20 0x21 0x34 0x37 0x51 0x62 0x65 0x75 |
| Chip ID, both amps | 0x1806 |
| Status after PLL lock | 0x4011: locked, clocks stable, undervoltage set while boost is off |
| Status after amp on | 0x0311: locked, clocks stable, switching, boost started, undervoltage cleared |
| After three beeps | Unchanged |

The board reset with a brownout (`E BOD`) when USB-C was unplugged. That's expected with no battery attached. A boot message about an invalid core dump image is harmless, because the stock partition table has no core dump partition.

### Nov 9, 2026

| Test | Result |
| --- | --- |
| Left/right (each amp alone) | 0x34 plays the left speaker, 0x37 the right, as you face the front |
| Stereo (each amp on its own channel) | Left-only, right-only and split tones all came from the correct speakers, and the left-to-right sweep was smooth. The ESP32's first I2S slot is left |
| Battery attached | Runs on battery alone. Same scan minus 0x08 (USB-C chip). 0x64 never answers |
| Fuel gauge | CW2015 at 0x62, asleep until woken. Battery read 3.57 V |
| Charger | SGM41513 at 0x1A charges the battery from USB-C at 500 mA |
| Accelerometer | LIS2DH12 at 0x18 confirmed (WHO_AM_I 0x33) |
| Light sensor | GPIO36 swings with light |

The first stereo version stuttered during the sweep: computing double-precision sine and cosine for every sample was too slow at 48 kHz. A sine lookup table and per-chunk pan gains fixed it. Keep per-sample audio maths simple on the ESP32.

### Nov 11–12, 2026: running with the case closed

| Test | Result |
| --- | --- |
| WiFi | Joins a 2.4 GHz network at −52 dBm. With the external antenna unplugged the same network read −88 dBm and every login failed (reason 2), even with the right password |
| Wireless update | Works from Arduino IDE's network port |
| Web page | Upload to the SD card, download, delete, and play WAV files in stereo |
| Buttons | All read 0 when pressed: left knob IOX.0.5, right knob IOX.0.4, power IOX.1.3 |
| Power off on battery | Releasing the power hold (IOX.3.1) alone didn't cut power. It works when the other expander pins are made inputs first, as Yoto's firmware does |
| USB-C while off | Plugging in switches the player on, and it charges while running |
| Knobs | Both work, decoded from pin-change interrupts at 4 counts per click: left on GPIO26/13, right on GPIO27/4 |
| Static between files | With no audio playing, the amps (running without Yoto's DSP setup) hiss audibly. Muting them (PWMCTRL 0x08 bit 0) 1.5 s after the last sound, and unmuting just before the next, removes it with no clipped starts |

Details in [`docs/hardware-reference.md`](docs/hardware-reference.md).

## Running the sketches

Back up first: see [`docs/download-mode.md`](docs/download-mode.md). The sketches need the Arduino IDE with esp32 core 3.x (tested with 3.3.12), board **ESP32 Wrover Module**. Enter download mode, then upload. Do a full power cycle afterward.

### The close-up firmware

1. In Tools, set Partition Scheme to **Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)**. Wireless updates need it. Keep it for every later upload.
2. Make sure the Wi-Fi antenna cable is plugged into the ESP32 module.
3. Upload by cable once. With no `/wifi.txt` on the SD card, the player starts a hotspot called **CAL-Yoto-Setup**. Join it, open `http://192.168.4.1`, and enter your 2.4 GHz network.
4. After that, open `http://cal-yoto.local`. Later uploads go over WiFi: pick the **cal-yoto** network port in Arduino IDE.
5. Optional: copy the sounds in [`sd-card/`](firmware/yoto_v3_closeup/sd-card/) to the top folder of the SD card, from the web page or a card reader.

| Action | How |
| --- | --- |
| Safe mode (WiFi, updates and web page only) | Hold a knob while switching on, or **Restart in safe mode** on the web page |
| Volume | Turn the left knob (clockwise louder). A tick plays at the new level when nothing's playing |
| Pause / resume | Press the left knob |
| Next / previous file | Turn the right knob. It moves through the WAVs in the folder of the last file played, or `/music` if nothing has played yet |
| Play / stop | Press the right knob |
| Power off | Hold the power button 3 s, wait for the shutdown sound, let go. Only on battery: with USB-C in, the board stays powered |
| Charging mode | If plugging in USB-C switched it on, the amps and sounds stay off until you press a knob or the power button (not yet tested) |
| See what happened last time | `cal_log_prev.txt` on the SD card, and the "Last restart" line at the top of the log |

Every sketch built on `yoto_base.h` keeps WiFi updates and safe mode, so the case can stay closed.

### The test sketches

Volume starts low on purpose. Raise `VOLUME_DB_DOWN` in small steps. Results go to Serial at 115200 and to a log file on the SD card, because my programming wires and speakers couldn't stay connected together.

## How this relates to open-yoto

[open-yoto](https://github.com/tensorfish/open-yoto) got there first on the amp, and it's well worth reading. It's replacement firmware for the Yoto Mini, it has a working AW88194 driver, and its config #05 lists the same IOX.2.4 and 0x34/0x37 details as the V3-E. It doesn't support full-size players.

| | open-yoto (as of its Sept 2026 code) | This repo |
| --- | --- | --- |
| Tested on | Yoto Mini | Full-size V3 (V3-E config) |
| Amps started | One, at 0x34 | Both, 0x34 and 0x37 |
| Method | Loads Yoto's register table and DSP firmware | Datasheet sequence only, no DSP load |
| Output | Full audio player | Stereo WAV player, plus test tones |
| Physical teardown | Links to Adafruit | Connectors, chips, header, fuses |

Yoto's stock firmware loads DSP firmware and tuning into the amp, and this sketch doesn't. I don't know how the amps behave at high volume without it, so go up slowly.

## Why 0x64 isn't the amp here

Adafruit's guide lists the AW88194 at 0x64. Yoto's V3-E config puts a CW2215B battery gauge at that address, and the AW88194 datasheet only allows 0x34–0x37. On this board nothing answers at 0x64 at all: the gauge fitted is a CW2015 at 0x62.

## Still unknown

- Why playback stopped once while I was raising the volume on battery. The firmware now logs the restart reason to catch it next time.
- What 0x75 is.
- Where the GPIO39 temperature sensor is. It isn't the speaker harness black wire or the battery's third wire, and it reads at the top of the ADC range, so it may need enabling.
- How stock firmware raises the charger's 500 mA input limit.
- The p0–p3 port order across the two expanders.
- Why IOX.0.0, a Qi interrupt, is set as an output.
- What's on the Qi board. It and the night light board are glued in, so I left them in place.

If you have a V3 and can answer any of these, please open an issue. Results from other V3 boards are especially useful, since I've only seen one.

## Related work

- [Hacking Yoto Music Players](https://learn.adafruit.com/hacking-the-yoto-music-players) (Adafruit): the Mini-focused guide this started from
- [open-yoto](https://github.com/tensorfish/open-yoto): replacement firmware for the Yoto Mini, with a working amp driver and detailed reverse-engineering docs
- [Yoto-Mini-ESPHome](https://github.com/Scags104/Yoto-Mini-ESPHome): ESPHome config for the Mini
- [AW88194 product page](https://www.awinic.com/en/productDetail/AW88194CSR) (Awinic): the datasheet is marked confidential, so it isn't reposted here
- [FCC internal photos](https://fccid.io/2AS6W-PRP3XX01755/Internal-Photos/Internal-photos-6603750)

## Notes

Independent project, not affiliated with or endorsed by Yoto. No Yoto firmware is included here. You do this at your own risk: it can brick your player and will void any warranty.

Code is MIT ([`LICENSE`](LICENSE)). Docs and photos are CC BY 4.0 ([`LICENSE-docs.md`](LICENSE-docs.md)).

By iamearlgrey (Zack).
