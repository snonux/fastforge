# =============================================
# FastForge – Pebble Intermittent Fasting App
# Justfile for Fedora Linux + Pebble SDK 4.9+ (2026)
# =============================================

# Default: show all available commands
default:
    @just --list

# Aliases (shortcuts)
alias b := build
alias i := install
alias d := dev
alias l := logs
alias s := screenshot
alias c := clean
alias k := kill

# ─────────────────────────────────────────────
# Core development commands
# ─────────────────────────────────────────────

# Build the app
build:
    pebble build

# Install to the basalt emulator (this also starts the emulator if it's not running)
install:
    pebble install --emulator basalt

# Build + Install (the command you will use most often)
dev:
    pebble build && pebble install --emulator basalt

# Show live logs (run this in a **second** terminal)
logs:
    pebble logs

# Take a screenshot of the emulator
screenshot:
    pebble screenshot

# ─────────────────────────────────────────────
# Emulator control
# ─────────────────────────────────────────────

# Kill / stop the running emulator (this is the fixed command)
kill:
    pebble kill

# ─────────────────────────────────────────────
# Maintenance commands
# ─────────────────────────────────────────────

# Clean all build artifacts
clean:
    rm -rf build/ *.pbw *.elf *.bin *.map *.o config.log .lock-waf* .wafpickle*

# Full clean + rebuild + install
rebuild:
    just clean
    just dev

# Quick rebuild + install
quick:
    just build && just install

# ─────────────────────────────────────────────
# Debug / Testing helpers
# ─────────────────────────────────────────────

# Build, install, and remind you to open logs
dev-with-logs:
    @echo "✅ Building and installing..."
    just dev
    @echo ""
    @echo "🚀 Now open a second terminal and run:   just logs"

# Show current SDK version
sdk-version:
    pebble --version

# Help / reminder
help:
    @echo "FastForge development commands:"
    @echo "  just dev          → build + install (most used)"
    @echo "  just logs         → live logs (second terminal)"
    @echo "  just kill         → stop the emulator"
    @echo "  just clean        → remove build files"
    @echo "  just rebuild      → clean + dev"
    @echo ""
    @echo "Run 'just' to see all available commands."
