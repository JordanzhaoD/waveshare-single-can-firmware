# Dashboard Guide

[Project Home](../) | [Documentation](index.md) | [Build & Flash](building.md) | [Plugin System](plugins.md) | [Release Notes](../CHANGELOG.md)

The dashboard is available on ESP32 builds that include `ESP32_DASHBOARD`. It runs from the device itself and is intended for local management at `http://100.100.1.1/` while connected to the device hotspot.
![Dashboard](img/dashboard.png)

## Dashboard Surfaces

The dashboard is organized around three usage surfaces:

- **驾驶状态 / 驾驶状态中心**: the default driving-first home page. It now uses a cockpit shell, ambient grid, large FSD state, vehicle attitude visual, safety strip, CAN health, hardware mode, drive style, speed strategy, runtime, firmware version, and grouped runtime metrics without exposing dense debug tables.
- **现场遥控**: the mobile layout for hotspot use from a phone. It uses a native-app-style card with one primary long-press FSD action, status cards short enough for one-handed use, and secondary risky actions kept out of the first screen.
- **CAN 诊断**: the engineering mode for live frames, recording, controller status, debug logs, and last-write checks. It uses an engineering-console hierarchy with KPI rail, frame filter, error-priority table, live timeline, recorder, controller, and debug surfaces.

## CAN Tools

- **CAN Sniffer**: live frame view with filtering by ID or known frame name
- **Wire / DBC ID toggle**: switch between on-wire 11-bit IDs and prefixed DBC-style IDs
- **CAN Recorder**: capture up to 2000 frames and export them as CSV
- **CAN Controller**: inspect per-mux RX/TX/error counters and controller error flags
- **Live Log**: view firmware log output directly in the dashboard

## Connectivity And Updates

- **WiFi Hotspot**: change AP name and password, optionally hide the SSID, and keep the values across reboots and firmware updates
- **WiFi Internet**: scan for networks, connect as STA, and optionally store a static IP/gateway/mask/DNS configuration
- **Firmware Update**: check GitHub releases, switch between stable and beta channel, enable auto-update on boot, or upload a local `.bin` manually
- **CAN Pins**: override TWAI TX/RX GPIO pins at runtime for supported boards; settings are stored in NVS
- **Settings Backup**: export and restore AP, WiFi, CAN pin, update, and plugin replay settings as JSON

## Plugins

- **Plugins card**: install from URL, upload a `.json`, or paste JSON directly when offline
- **Plugin list**: inspect rules, enable or disable plugins, remove them, and spot priority overlaps between enabled plugins
- **Plugin Editor**: create plugins without hand-writing JSON, preview the result live, load an installed plugin back into the editor, download the generated file, and add a quick rule from shorthand such as `0x7FF mux=2 byte[5] = 0x4C`
- **Rule Test**: wait for a matching live CAN frame, apply one editor rule to that frame, then send the result repeatedly with a chosen count and interval
- **Plugin Replay**: set how many modified GTW 2047 (`0x7FF`) plugin frames are sent for each observed GTW frame
- **Periodic emit plugins**: use `emit_periodic` rules to keep the last modified GTW mux 3 value on the bus, optionally with GTW UDS silent-mode keep-alives
- Plugin-based overrides such as nag suppression and Summon unlock can live here instead of on the main Features card
- Dashboard builds only inject automatically through enabled plugins; built-in vehicle handlers stay observational
- Dashboard cards can be collapsed individually with `Hide` / `Show` to keep the page shorter on mobile

## Persistence Notes

- WiFi hotspot settings, WiFi internet settings, update flags, CAN pins, and several runtime defaults are stored in NVS
- Installed plugins live on SPIFFS, start disabled after install, and restore their enabled or disabled state on boot
- On AtomS3 Mini builds, the built-in button can toggle injection and that state is also persisted

## UI Copy Notes

The dashboard uses the refined Task 4 Chinese copy for the redesigned UI, including labels such as `驾驶状态`, `硬件模式`, `驾驶风格`, `速度策略`, `CAN 诊断`, and `FSD 防护`. Technical acronyms such as FSD, CAN, HW, NVS, OTA, TX/RX, EFLG, DBC, and GPIO are intentionally kept in English because they are standard protocol or firmware terms.
