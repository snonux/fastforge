# =============================================
# FastForge – Pebble Intermittent Fasting App
# Justfile for Fedora Linux + Pebble SDK 4.9+ (2026)
# =============================================

# Default emulator: gabbro = Pebble Round 2 (260×260 round display).
# Use 'just dev-emery' / 'just debug-dev-emery' for the Pebble Time 2
# rectangular display (200×228). These are the only two supported devices.
emulator := "gabbro"
emulator_rect := "emery"

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

# Install to the gabbro emulator (Pebble Round 2)
install:
    pebble install --emulator {{emulator}}

# Build + install on Pebble Round 2 (gabbro) — primary target
# Kills any stale emulator first so install never races against the boot animation.
dev:
    -pebble kill
    pebble build && pebble install --emulator {{emulator}}

# Build + install on Pebble Time 2 (emery, rectangular)
dev-emery:
    -pebble kill
    pebble build && pebble install --emulator {{emulator_rect}}

# Show live logs (run this in a **second** terminal)
logs:
    pebble logs --emulator {{emulator}}

# Show live logs for the Pebble Time 2 (emery) emulator
logs-emery:
    pebble logs --emulator {{emulator_rect}}

# Take a screenshot of the emulator
screenshot:
    pebble screenshot --emulator {{emulator}}

# Take a screenshot of the Pebble Time 2 (emery) emulator
screenshot-emery:
    pebble screenshot --emulator {{emulator_rect}}

# ─────────────────────────────────────────────
# Emulator control
# ─────────────────────────────────────────────

# Kill / stop the running emulator
kill:
    pebble kill

# ─────────────────────────────────────────────
# Emulator recovery
# ─────────────────────────────────────────────

# Reset the gabbro SPI flash to factory state.
# Use this when 'just dev' hangs on the Pebble boot screen — it means the
# flash was corrupted (e.g. by killing multiple concurrent qemu processes).
reset-flash:
    -pebble kill
    bunzip2 -k -c ~/.pebble-sdk/SDKs/4.9.148/sdk-core/pebble/gabbro/qemu/qemu_spi_flash.bin.bz2 > ~/.pebble-sdk/4.9.148/gabbro/qemu_spi_flash.bin
    @echo "Flash reset to factory state. Run 'just dev' to reinstall."

# Reset the emery SPI flash to factory state.
reset-flash-emery:
    -pebble kill
    bunzip2 -k -c ~/.pebble-sdk/SDKs/4.9.148/sdk-core/pebble/emery/qemu/qemu_spi_flash.bin.bz2 > ~/.pebble-sdk/4.9.148/emery/qemu_spi_flash.bin
    @echo "Flash reset to factory state. Run 'just dev-emery' to reinstall."

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

# Run host unit tests (TZ=UTC for deterministic localtime/streak checks)
test:
    make -C tests test

# Build with DEBUG=1 compile flag
debug-build:
    DEBUG=1 pebble build

# Build + install debug app on Pebble Round 2 (gabbro)
# Kills any stale emulator first (same reason as dev).
debug-dev:
    -pebble kill
    DEBUG=1 pebble build && pebble install --emulator {{emulator}}

# Build + install debug app on Pebble Time 2 (emery)
debug-dev-emery:
    -pebble kill
    DEBUG=1 pebble build && pebble install --emulator {{emulator_rect}}

# ─────────────────────────────────────────────
# Debug / Testing helpers
# ─────────────────────────────────────────────

# Build, install, and remind you to open logs
dev-with-logs:
    @echo "Building and installing..."
    just dev
    @echo ""
    @echo "Now open a second terminal and run:   just logs"

# Show current SDK version
sdk-version:
    pebble --version

# Help / reminder
help:
    @echo "FastForge development commands:"
    @echo "  just dev              → build + install on Round 2/gabbro (primary)"
    @echo "  just dev-emery        → build + install on Time 2/emery (rectangular)"
    @echo "  just logs             → live logs for gabbro (second terminal)"
    @echo "  just logs-emery       → live logs for emery (second terminal)"
    @echo "  just kill             → stop the emulator"
    @echo "  just clean            → remove build files"
    @echo "  just rebuild          → clean + dev"
    @echo ""
    @echo "Run 'just' to see all available commands."
