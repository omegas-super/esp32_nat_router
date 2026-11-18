# ESP32-WROOM Custom Build — Hardware Guide

Configuration reference for running this firmware on a plain **ESP32-WROOM**
(ESP32-D0WD-V3, 4 MB flash) with an OLED display, a five-button front panel,
a multi-antenna RF switch and a battery pack.

Unlike the C3/C5/C6/S3 targets, the original ESP32 has strict pin rules. The
defaults below avoid every pin that would break boot or crash the chip.

## Pin rules for ESP32-WROOM

| GPIO | Status | Why |
|------|--------|-----|
| 6–11 | **Never use** | SPI flash bus — using these bricks the boot |
| 34–39 | **Input only** | No output, no internal pull-up |
| 0, 2, 12, 15 | Strapping | Must be at a specific level at boot |
| 1, 3 | UART0 | Serial console (leave for flashing/logs) |

Safe for both input-with-pull-up and output: **4, 13, 14, 16, 17, 18, 19, 21,
22, 23, 25, 26, 27, 32, 33**.

## Default pin map

These values are **confirmed on real hardware** (ESP32-D0WD-V3, MAC
78:ee:4c:01:62:f4) — the OLED displayed correctly and all five buttons
registered presses on the serial log.

| Function | GPIO | Notes |
|----------|------|-------|
| OLED SDA | 21 | I2C data — confirmed working |
| OLED SCL | 22 | I2C clock — confirmed working |
| Button UP | 32 | to GND, internal pull-up — confirmed |
| Button DOWN | 33 | to GND, internal pull-up — confirmed |
| Button LEFT | 25 | to GND, internal pull-up — confirmed |
| Button RIGHT | 26 | to GND, internal pull-up — confirmed |
| Button SELECT | 27 | to GND, internal pull-up — confirmed |
| Antenna select bit 0 | 16 | RF switch select |
| Antenna select bit 1 | 17 | second select pin (2-bit mux, 4 antennas) |
| Antenna enable | 4 | optional, driven low to power the switch |
| Status LED | 2 | onboard LED (strapping, output-only use) |

Every pin above is configuration, not a compile-time constant — change any of
them from the console or the web UI without rebuilding.

## First-time setup

Buttons are wired to GND and read with the internal pull-up, so a press pulls
the pin low. Configure the panel once, then restart:

```bash
set_buttons 32 33 25 26 27
```

Enable the display (it is off by default so boards without one allocate
nothing):

```bash
set_oled enable
set_oled_gpio 21 22
```

Then restart. The console prints the I2C pins it is using on boot, and the
display shows the status screen.

## Button behaviour

| Button | Action |
|--------|--------|
| Up / Down | Previous / next screen (or antenna index on the antenna screen) |
| Left / Right | Cycle screens |
| Select | Apply the highlighted antenna (antenna screen only) |
| Select (hold 1.5 s) | Restart the router (system screen only) |

Screens: **status** (SSIDs, IP, RSSI, clients) → **clients** → **antenna** →
**system** (heap, uptime, temperature).

Every press is logged to the console, which is how the pin map was verified:

```
I (20084) button_ui: press down -> screen clients cursor 0
```

At startup the task reports the idle level of each pin. All five should read
`H`; an `L` means that pin is shorted to GND or its pull-up is missing:

```
I (944) button_ui: idle levels (H=ok L=stuck): H H H H H
```

### Debounce tuning

The panel polls every 10 ms and accepts a change after 20 ms of stable level.
The first version used 20 ms polling and a 40 ms debounce, which was too
strict for these hand-soldered buttons: a normal press only held contact long
enough to be sampled occasionally, so the buttons appeared to need a hard
press. Shortening both fixed it without introducing false triggers.

## Three antennas

The original firmware supported a single select pin: 0 = on-board,
1 = external. This build adds an optional **second select pin**, turning the
switch into a 2-bit mux addressing up to four antennas:

```bash
set_antenna 16 0 4      # bit 0 on GPIO16, enable on GPIO4
set_antenna2 17         # bit 1 on GPIO17  -> 4 antennas
```

Antenna N is driven as `(bit1, bit0)` on `(GPIO17, GPIO16)`:

| Antenna | GPIO16 | GPIO17 |
|---------|--------|--------|
| 0 | 0 | 0 |
| 1 | 1 | 0 |
| 2 | 0 | 1 |
| 3 | 1 | 1 |

With `set_antenna2 off` the original binary behaviour is restored and only
antennas 0 and 1 are reachable.

If your three antennas are wired through splitters rather than a switch, no
firmware can select between them — an unswitched 3-way split loses roughly
5 dB per port and there is nothing to drive.

## Battery

**Do not wire a LiPo in parallel with the USB 5 V rail.** The two supplies
fight each other; under sustained load the rail sags and the CP210x drops off
the USB bus, which shows up as `PermissionError` or packet corruption during
flashing and reading.

Use a power-path circuit (TP4056 with protection, or a load-sharing IC) so
USB and battery never back-feed. This was confirmed on the bench: every 4 MB
flash read failed with the battery connected and succeeded the moment it was
disconnected.

## Building

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32
idf.py build
```

The app must fit the 1536 KiB `ota_0` partition. Check with:

```bash
idf.py size-components
```

## Flashing

```bash
idf.py -p COM10 flash monitor
```

The OLED and button features are disabled by default, so a stock WROOM board
flashed with this build behaves exactly like upstream until you configure
them.
