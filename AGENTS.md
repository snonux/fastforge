# FastForge - AGENTS.md
Pebble Intermittent Fasting Tracker Development Guide
Fedora Linux + Rebble Pebble SDK 4.9+ (April 2026)

This file is the primary workflow reference for FastForge contributors.

## 1. One-Time Initial Setup

```bash
sudo dnf update
sudo dnf install -y python3-pip nodejs SDL-devel dtc uv just
uv tool install pebble-tool --python 3.13
export PATH="$HOME/.local/bin:$PATH"
pebble sdk install latest
```

## 2. After Every Reboot - Restore uv Tool PATH

```bash
export PATH="$HOME/.local/bin:$PATH"
pebble --version
```

Optional permanent fix:

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

## 3. Justfile Workflow

Available commands:

- `just`: list commands
- `just dev`: build + install to emulator
- `just logs`: live logs (second terminal)
- `just kill`: stop emulator
- `just clean`: remove build artifacts
- `just rebuild`: clean + full rebuild + install
- `just quick`: quick rebuild + install
- `just screenshot`: screenshot of emulator
- `just sdk-version`: print Pebble SDK/tool version
- `just test`: run host unit tests (expects `TZ=UTC` for deterministic date/streak results)

Daily loop after reboot:

1. Restore PATH (`export PATH="$HOME/.local/bin:$PATH"`).
2. `cd /path/to/fastforge`
3. `just dev`
4. In a second terminal run `just logs`

## 4. Headless Basalt Testing

When `just dev` / `just logs` are not enough for runtime verification, use a
single headless basalt emulator and drive it through the QEMU monitor.

### Clean emulator reset

If the emulator hangs on the Pebble boot screen or `pebble install` gets stuck
at `Waiting for the firmware to boot`, reset the persisted basalt flash:

```bash
pebble kill
mv ~/.pebble-sdk/4.9.148/basalt/qemu_spi_flash.bin \
   ~/.pebble-sdk/4.9.148/basalt/qemu_spi_flash.bin.bak.$(date +%s)
```

Then start a fresh live session:

```bash
pebble install -vv --emulator basalt --logs
```

Important:

- Wait for `Firmware booted.`
- Wait for `App install succeeded.`
- The verbose output prints the QEMU monitor port, for example:
  `-monitor tcp::46951,server,nowait`

### Capture screenshots from the monitor

Use the monitor port from the install output:

```bash
python - <<'PY'
import socket, time
s = socket.create_connection(('127.0.0.1', 46951), timeout=2)
s.sendall(b'screendump /tmp/fastforge.ppm\n')
time.sleep(0.4)
s.close()
PY

tesseract /tmp/fastforge.ppm stdout --psm 6
```

That gives you a real basalt screen capture plus OCR for quick verification.

### Send button input through the monitor

Use Pebble button semantics via QEMU `sendkey`:

- `up`: top button
- `down`: bottom button
- `s`: select button
- `q`: back button

Example:

```bash
python - <<'PY'
import socket, time
MON = ('127.0.0.1', 46951)

def cmd(text, wait=0.25):
    s = socket.create_connection(MON, timeout=2)
    s.sendall((text + '\n').encode())
    time.sleep(wait)
    s.close()

cmd('sendkey down', 0.5)
cmd('sendkey s', 0.8)
cmd('screendump /tmp/after_select.ppm', 0.3)
PY
```

### Proven runtime checks

This workflow was used successfully to verify:

- main menu
- preset submenu
- current timer
- history screen
- statistics screen
- settings screen
- backup export logs

## 5. Commit Policy

Commit these project files when changed:

- `README.md`
- `AGENTS.md`
- `PLAN.md`
- `Justfile`
- `package.json`
- `wscript`
- `src/c/fastforge.c`
- `src/c/fastforge_core.c`
- `src/c/fastforge_history.c`
- `src/c/fastforge_internal.h`
- `.gitignore`

Do not commit generated artifacts:

- `build/`
- `*.pbw`, `*.elf`, `*.bin`, `*.o`, `*.map`
- `.lock-waf*`, `.wafpickle*`, `config.log`

## 6. Troubleshooting

- `pebble: command not found`: restore PATH from section 2.
- Emulator does not start: run `just kill` then `just dev`.
- Build fails: run `just clean` then `just rebuild`.
- Change app version: update `package.json`, then run `just dev`.
- `Waiting for the firmware to boot` forever: reset
  `~/.pebble-sdk/4.9.148/basalt/qemu_spi_flash.bin` as described in section 4.
- `pebble logs --emulator basalt` and `pebble install --emulator basalt` each
  spawn their own emulator instance. For headless runtime testing, prefer one
  `pebble install -vv --emulator basalt --logs` session and use its monitor
  port for screenshots/input.
- Repeated hot reinstalls may show a `pypkjs` / `geventwebsocket` traceback in
  the toolchain logs. In the verified workflow the app still installed and ran,
  so treat that as a tool-side warning unless the watch UI actually breaks.

Last updated: April 12, 2026 (follow-up fix)
Maintained for: FastForge agents
