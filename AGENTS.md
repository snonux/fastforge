**✅ Here is the complete, clean `AGENTS.md` file** — ready to copy and save.

```markdown
# FastForge – AGENTS.md
**Pebble Intermittent Fasting Tracker Development Guide**  
**Fedora Linux + Rebble Pebble SDK 4.9+ (April 2026)**

This document contains **everything** an agent (or you after a reboot) needs to know to work on FastForge.

---

## 1. One-Time Initial Setup (already completed)

```bash
# 1. System dependencies
sudo dnf update
sudo dnf install -y python3-pip nodejs SDL-devel dtc uv just

# 2. Install pebble-tool via uv
uv tool install pebble-tool --python 3.13

# 3. Install latest SDK
pebble sdk install latest
```

---

## 2. After Every Reboot – Restore uv Environment

After rebooting your laptop, the `pebble` command is usually not in PATH. Run this **every time** you open a new terminal:

```bash
# Restore uv tool path
export PATH="$HOME/.local/bin:$PATH"

# Verify
pebble --version
```

**Make it permanent** (recommended):
```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

---

## 3. Justfile Usage (Recommended Workflow)

We use `just` as the command runner.

### All available commands

```bash
just                  # Show all available commands
just dev              # ← MOST USED: build + install to emulator
just logs             # Live logs (open in a second terminal)
just kill             # Stop the emulator
just clean            # Remove all build artifacts
just rebuild          # Clean + full rebuild + install
just quick            # Quick rebuild + install
just screenshot       # Take a screenshot of the emulator
just sdk-version      # Show current Pebble SDK version
```

**Daily workflow after reboot:**

1. Run the uv environment commands above
2. `cd ~/g/fastforge`
3. `just dev`
4. Open **second terminal** → `just logs`

---

## 4. Full Daily Development Loop

```bash
# 1. Restore environment
export PATH="$HOME/.local/bin:$PATH"

# 2. Go to project
cd ~/g/fastforge

# 3. Edit code (src/c/fastforge.c)

# 4. Build + test
just dev

# 5. Watch logs in another terminal
just logs
```

---

## 5. What to Commit to Git

**Only commit these files:**

- `README.md`
- `AGENTS.md`
- `PLAN.md`
- `Justfile`
- `package.json`
- `wscript`
- `src/c/fastforge.c`
- `.gitignore`

**Never commit:**
- `build/` folder
- `*.pbw`, `*.elf`, `*.bin`, `*.o`, etc.
- Any `.lock-waf*`, `.wafpickle*`, `config.log`

---

## 6. Troubleshooting

- `pebble: command not found` → run the PATH export from section 2
- Emulator doesn’t start → `just kill` then `just dev`
- Build fails → `just clean` then `just rebuild`
- Want to change app version → edit `package.json` → `just dev`

---

**Last updated:** April 6, 2026  
**Maintained for:** All agents working on FastForge

```

---

**How to save it:**

1. Copy everything above (from `# FastForge – AGENTS.md` to the end)
2. Paste into a new file named **`AGENTS.md`** in your `fastforge` folder
3. Save it

You now have a single, complete reference file.

Would you like me to also create the full `main.c` skeleton for FastForge next? Just say **“give me the full FastForge main.c”**.
