# Roto360 reimaged

A rotating-dial watchface for Pebble, redrawn from scratch for the Pebble Round 2's 260x260 color screen. It also runs on the Pebble Time, Time Round and Time 2.

![Roto360 reimaged on Pebble Round 2](store/screenshots/gabbro-1.png) ![Roto360 reimaged on Pebble Time 2](store/screenshots/emery-1.png)

This is an independent remake inspired by [Roto360](https://apps.repebble.com/roto360_5640028f39a14af5f100000f) by MicroByte. It shares no code, images or fonts with the original, and the original's author has not reviewed or endorsed it. The original has no published license, so nothing here grants rights to it, and its terms do not apply to this project.

## How it reads

The time sits under the blue shield at the top of the dial. The outer ring holds the hours and the inner ring holds the minutes in steps of five, and both rings turn so the current hour and minute stay under the shield. The minute box shows the exact minute. The center shows the weekday, battery level and date.

- Follows the watch's 12-hour or 24-hour setting. In 12-hour mode the hour ring reads 01 to 12.
- Battery turns red at 20% or below. While charging it shows a green `+`, for example `+80%`.
- Round watches draw circular rings. Rectangular watches draw a rounded-square bezel with the numbers running along the edges.

The rotated numerals come from a high-resolution atlas of Bebas Neue that the watch samples and antialiases as it draws, instead of scaling a small bitmap. That keeps them sharp at every angle on the larger screen.

| Watch | Platform | Shape |
|---|---|---|
| Pebble Round 2 | `gabbro` | round, 260x260 |
| Pebble Time Round | `chalk` | round, 180x180 |
| Pebble Time 2 | `emery` | rectangular, 200x228 |
| Pebble Time, Time Steel | `basalt` | rectangular, 144x168 |

The face redraws each minute and when the battery state changes. It has no phone companion, settings page or network access.

## Build and install

You need the [Pebble SDK](https://developer.repebble.com/sdk.md). The project CI uses Pebble CLI 5.0.40 with SDK 4.33.1. On Windows, use WSL.

```text
cd app
pebble build
pebble install --emulator gabbro
```

To install on a watch, enable the Developer Connection in the Pebble phone app and run `pebble install --phone <phone IP>`. Every CI run uploads a ready-built `Roto360Reimaged-<version>.pbw` as an artifact, and published releases attach the same file.

The face uses its own app UUID, so it installs next to the original Roto360 instead of replacing it.

## Tests

```text
npm test
```

This needs Node.js 22 or later and GCC or Clang. It builds the dial logic natively and checks the package settings, digit atlas and store images.

The tools in `tools/` need Python 3 and Pillow:

- `python3 tools/gen_digits.py` renders `app/resources/data/digits.bin` from the font. Add `--check` to confirm the committed atlas matches.
- `python3 tools/make_store_assets.py store/screenshots store` rebuilds the store banners and icons.

## Project structure

- `app/src/c/main.c`: layout, rings, shield and center text.
- `app/src/c/dial.c`: time, label and ring geometry logic with no SDK dependency.
- `app/src/c/numerals.c`: draws rotated, antialiased digits from the atlas.
- `store/`: emulator screenshots, banners and icons for the Pebble appstore.
- `tests/`: Node.js tests and the native dial test.

## Contributing and security

See [CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md). Contributor and AI-agent guidance lives in [.github/skills/pebble-development/SKILL.md](.github/skills/pebble-development/SKILL.md), which links to the official [Pebble AI agent guide](https://developer.repebble.com/guides/ai-agents.md).

## License

The code, drawings and store art are under the [Apache License 2.0](LICENSE). Bebas Neue is under the [SIL Open Font License 1.1](app/resources/fonts/OFL.txt); see [NOTICE](NOTICE).
