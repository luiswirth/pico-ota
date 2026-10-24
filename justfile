host := "inkpico.local"

build:
    cmake -S . -B build -G Ninja
    ninja -C build

# Puts the bootloader on the Pico over USB; the app in the slot stays.
boot: build
    picotool load -f -x build/boot.uf2

# Puts an app on the Pico over Wi-Fi.
flash app: build
    tools/ota flash {{ host }} build/examples/{{ app }}/{{ app }}.bin

status:
    tools/ota status {{ host }}

monitor:
    tio $(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)

# Everything CI runs.
ci:
    nix fmt -- --ci
    just build
