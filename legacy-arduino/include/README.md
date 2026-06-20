# Legacy Arduino Build

This in-repo PlatformIO project keeps the Arduino-only board targets that do not run ESP-IDF:

- `feather_rp2040_can`
- `feather_m4_can`

Build from this directory with:

```bash
pio run -e feather_rp2040_can
pio run -e feather_m4_can
```

The firmware source is shared with the repository root. The root project owns ESP32-family ESP-IDF builds.
