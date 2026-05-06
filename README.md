# HarbingerApp

A Windows desktop application for controlling a Bluetooth-connected turret system over RFCOMM.

## Features

- Automatic Bluetooth device discovery and connection to "Harbinger" hardware
- 2D aim pad with live heading/elevation feedback
- Master / Turret / Gun arm interlock controls
- Capacitor charge voltage target slider
- Hold-to-fire button (600 ms hold required)
- Live telemetry: compass heading, elevation angle, dual motor velocity & acceleration
- Event log for protocol messages
- **System light/dark mode** — all UI elements and the aim pad follow the OS color scheme automatically

## Building

Requires **Qt 6.5+** (tested on Qt 6.11.0) and a MinGW 64-bit toolchain on Windows.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Set `CMAKE_PREFIX_PATH` in `CMakeLists.txt` to your Qt installation if needed.

## Project Structure

```
HarbingerApp/
├── main.cpp            # Entry point
├── mainwindow.h/.cpp   # Main application window
├── protocol.h          # Wire-format constants and packed structs
├── btdevice.h          # BtDevice data structure
├── scanworker.h        # Bluetooth device scan (worker thread)
├── ioworker.h          # Bluetooth I/O loop (worker thread)
├── theme.h             # System light/dark color helpers + Qt stylesheet
└── widgets/
    ├── aimwidget.h         # Drag-to-aim 2D pad
    ├── compasswidget.h     # Heading compass rose
    ├── elevationwidget.h   # Elevation bar gauge
    ├── bimotorwidget.h     # Dual-bar motor velocity/acceleration display
    └── holdfirebutton.h    # Hold-to-fire circular button
```

## License

MIT — see [LICENSE](LICENSE).
