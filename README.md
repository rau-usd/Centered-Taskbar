<p align="center">
  <img src="assets/icon.png" width="72" alt="Centered-Taskbar icon">
</p>

<h1 align="center">Centered-Taskbar</h1>

<p align="center">
  A tiny tray app that centers your <b>Windows 10</b> taskbar icons — with a smooth animation, and almost no resource usage.
</p>

<p align="center">
  <a href="README.es.md">Español</a>
</p>

<!-- Tip: add a screenshot or a short GIF here, e.g. ![demo](docs/demo.gif) -->

---

## Features

- **Centers the taskbar buttons** on the screen, like Windows 11 does — on Windows 10.
- **Smooth animation** when apps are opened or closed: icons slide to their new position instead of jumping. Speed is adjustable (Off / Fast / Normal / Smooth).
- **Lightweight**: a single native C++ executable, no runtime, no installer, no administrator rights.
- **Low background usage**: when nothing changes it wakes up only every 1.5 seconds for a single measurement. The 60 FPS animation timer runs only for a fraction of a second after the taskbar changes.
- **Tray icon** with a small dark menu to toggle everything.
- **Run at startup** option (per-user, no UAC prompt).
- Restores the taskbar to its original layout when you disable the option or exit.

## Requirements

- Windows 10 (64-bit) with the classic taskbar.
- A **horizontal** taskbar (bottom or top). Vertical (left/right) taskbars are left untouched.

> **Windows 11 is not supported** — its taskbar is a different component.

## Installation

1. Download `Centered-Taskbar.exe` from the [Releases](../../releases) page.
2. Put it anywhere you like and run it. A tray icon appears and your taskbar icons move to the center.
3. *(Optional)* Right-click the tray icon → **Run at startup**.

There is nothing to install. To remove it, see [Uninstall](#uninstall).

## Usage

Click the tray icon (left or right button) to open the menu:

| Item | What it does |
|---|---|
| **Center taskbar** | Turns the centering on/off. Turning it off puts the icons back on the left. |
| **Animation speed** | `Off` (instant), `Fast`, `Normal` (default) or `Smooth`. Applies immediately. |
| **Run at startup** | Starts the app when you sign in to Windows. |
| **Exit** | Closes the app and restores the original taskbar layout. |

> **Tip:** hide the *Search* box and the *Task View* button (right-click the taskbar) so the icons can reach the exact center of the screen. The app never overlaps those elements or the notification area.

### Configuration file

Settings are stored in `%APPDATA%\Centered-Taskbar\config.ini` and are normally changed from the tray menu:

```ini
[CenteredTaskbar]
CenterTaskbar=1   ; 1 = on, 0 = off
AnimSpeed=2       ; 0 = Off, 1 = Fast, 2 = Normal, 3 = Smooth
```

## How it works

Windows 10 lays out the taskbar buttons inside a child window of Explorer (`MSTaskListWClass`) that always starts at the left. Centered-Taskbar:

1. Finds that window (`Shell_TrayWnd` → `ReBarWindow32` → `MSTaskSwWClass` → `MSTaskListWClass`).
2. Reads the position of every button with **UI Automation** (one cached call) and computes where the group of buttons has to be to sit in the middle of the screen, clamped so it never overlaps the tray area.
3. Moves that window with `SetWindowPos`, easing towards the target for a smooth slide.
4. Reacts to the shell: window created / destroyed / activated events trigger a short animation burst, and a WinEvent hook puts the list back immediately if Explorer repositions it, so you never see a jump to the left.
5. A slow poll (every 1.5 s, one measurement) catches anything else, such as buttons being dragged to reorder.

No code is injected into Explorer, and the app needs no elevated privileges.

## Limitations

- Windows 10 only; Windows 11 is not supported.
- Only the taskbar on the **primary monitor** is centered.
- Vertical taskbars are ignored.
- The method relies on **undocumented internals of Explorer** (window class names and layout). A future Windows update could break it.
- If the process is killed abruptly (for example from Task Manager) the icons stay where they were until Explorer re-lays out the taskbar. Relaunching the app and choosing **Exit**, or restarting Explorer, fixes it.
- Other taskbar-tweaking tools that also move the taskbar buttons may conflict with this one.

## Troubleshooting

- **Icons are not centered** — check that *Center taskbar* is ticked in the tray menu and that the taskbar is at the bottom or top of the screen.
- **Icons jump instead of sliding** — set *Animation speed* to *Normal* or *Smooth*.
- **Nothing happens after Explorer crashed/restarted** — the app re-attaches by itself within a couple of seconds; if it does not, exit it and run it again.

## Uninstall

1. Right-click the tray icon → untick **Run at startup**, then choose **Exit**.
2. Delete `Centered-Taskbar.exe`.
3. *(Optional)* Delete the folder `%APPDATA%\Centered-Taskbar`.

## Building from source

You need a MinGW-w64 toolchain (for example [w64devkit](https://github.com/skeeto/w64devkit), a portable zip — no installation required) with `g++` and `windres` in your `PATH`.

```bat
git clone https://github.com/<your-user>/Centered-Taskbar.git
cd Centered-Taskbar
build.bat
```

The executable is written to `dist\Centered-Taskbar.exe`. The script is just two commands, run from the repository root:

```bat
windres res/Centered-Taskbar.rc -O coff -o build/Centered-Taskbar.res
g++ -Os -s -municode -mwindows -static -ffunction-sections -fdata-sections -Wl,--gc-sections ^
  -o dist/Centered-Taskbar.exe src/Centered-Taskbar.cpp build/Centered-Taskbar.res ^
  -lole32 -loleaut32 -luuid -lshell32 -lgdi32 -luser32 -ladvapi32
```

A GitHub Actions workflow (`.github/workflows/build.yml`) builds the project on every push and, when you push a tag such as `v1.0.0`, attaches `Centered-Taskbar.exe` to a GitHub Release.

### Project layout

```
Centered-Taskbar/
├── src/Centered-Taskbar.cpp        # the whole application (Win32 + UI Automation)
├── res/
│   ├── Centered-Taskbar.rc         # icon, manifest, version info
│   ├── Centered-Taskbar.manifest
│   └── Centered-Taskbar.ico
├── assets/icon.png                 # icon used in this README
├── build.bat
└── .github/workflows/build.yml
```

## Credits

- Application icon: *Taskbar* icon by [Icons8](https://icons8.com).

## License

The source code is released under the [MIT License](LICENSE). The icon is a third-party asset by Icons8 and is covered by [Icons8's own license](https://icons8.com/license), not by the MIT license.
