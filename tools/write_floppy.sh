#!/bin/bash

# Writes a floppy image (<disc>.img) to a 2HD disk in a USB floppy drive, then reads it back and compares
# Usage: tools/write_floppy.sh <image.img> [device]
# Linux formats the disk as 1.23 MB first with ufiformat (77 cylinders, 2 heads, 8 sectors of 1024 bytes). macOS
# can't, so there the disk must already have that format (format2hd on Windows, ufiformat on Linux or a Towns)
# Without a device it takes the only USB floppy drive on Linux, and the only disk of exactly 1.23 MB on macOS

set -e

IMAGE="$1"
DEVICE="$2"
SIZE=1261568

if [ -z "$IMAGE" ]; then
    echo "usage: $0 <image.img> [device]"
    exit 1
fi

if [ ! -f "$IMAGE" ] || [ "$(wc -c < "$IMAGE" | tr -d ' ')" != "$SIZE" ]; then
    echo "error: $IMAGE is not a 1.23 MB floppy image ($SIZE bytes)"
    exit 1
fi

if [ -n "$DEVICE" ]; then
    DEVICE="/dev/${DEVICE#/dev/}"
fi

SUDO=

if [ "$(id -u)" -ne 0 ]; then
    SUDO=sudo
fi

case "$(uname)" in
Linux)
    if ! command -v ufiformat > /dev/null; then
        echo "error: ufiformat not found, install it first (apt install ufiformat)"
        exit 1
    fi

    if ! ls /dev/sg* > /dev/null 2>&1; then
        $SUDO modprobe sg
    fi

    if [ -z "$DEVICE" ]; then
        DEVICES=$($SUDO ufiformat -i -q | awk '$1 ~ /^\/dev\/sd/ { print $1 }')

        if [ -z "$DEVICES" ]; then
            echo "error: no USB floppy drive found"
            exit 1
        fi

        if [ "$(echo "$DEVICES" | wc -l)" -ne 1 ]; then
            echo "error: more than one USB floppy drive, pass the one to use:" $DEVICES
            exit 1
        fi

        DEVICE="$DEVICES"
    fi

    NAME=$(cat "/sys/block/${DEVICE#/dev/}/device/vendor" "/sys/block/${DEVICE#/dev/}/device/model" 2>/dev/null | xargs)
    echo "Formats $DEVICE ($NAME) as 1.23 MB and writes $IMAGE to it"
    read -r -p "Insert the disk, then press Return to go on or Ctrl-C to stop "

    $SUDO umount "$DEVICE" 2>/dev/null || true
    $SUDO ufiformat -f 1232 -V "$DEVICE"
    echo "Writing $IMAGE"
    $SUDO dd if="$IMAGE" of="$DEVICE" bs=8k conv=fsync status=none
    echo "Reading it back"

    if ! $SUDO dd if="$DEVICE" bs=8k iflag=direct status=none | cmp - "$IMAGE"; then
        echo "error: the disk doesn't read back as $IMAGE"
        exit 1
    fi
    ;;
Darwin)
    if [ -z "$DEVICE" ]; then
        DEVICES=

        for disk in $(diskutil list external physical | awk '/^\/dev\/disk/ { print $1 }'); do
            if [ "$(diskutil info "$disk" | awk -F'[()]' '/Disk Size/ { split($2, a, " "); print a[1] }')" = "$SIZE" ]; then
                DEVICES="$DEVICES $disk"
            fi
        done

        if [ -z "$DEVICES" ]; then
            echo "error: no 1.23 MB disk found; macOS can't format one, so format it as 1.23 MB first with"
            echo "format2hd on Windows (/F:1.23) or ufiformat on Linux (-f 1232)"
            exit 1
        fi

        if [ "$(echo $DEVICES | wc -w)" -ne 1 ]; then
            echo "error: more than one 1.23 MB disk, pass the one to use:" $DEVICES
            exit 1
        fi

        DEVICE=$(echo $DEVICES)
    fi

    DEVICE="${DEVICE/rdisk/disk}"
    RAW="${DEVICE/disk/rdisk}"

    if [ "$(diskutil info "$DEVICE" | awk -F'[()]' '/Disk Size/ { split($2, a, " "); print a[1] }')" != "$SIZE" ]; then
        echo "error: $DEVICE is not a 1.23 MB disk ($SIZE bytes)"
        exit 1
    fi

    NAME=$(diskutil info "$DEVICE" | awk -F': *' '/Device \/ Media Name/ { print $2 }')
    echo "Writes $IMAGE to $DEVICE ($NAME)"
    read -r -p "Press Return to go on or Ctrl-C to stop "

    diskutil unmountDisk "$DEVICE"
    echo "Writing $IMAGE"
    $SUDO dd if="$IMAGE" of="$RAW" bs=8k
    echo "Reading it back"

    if ! $SUDO dd if="$RAW" bs=8k 2>/dev/null | cmp - "$IMAGE"; then
        echo "error: the disk doesn't read back as $IMAGE"
        exit 1
    fi

    diskutil eject "$DEVICE"
    ;;
*)
    echo "error: $(uname) isn't supported, use tools/write_floppy.cmd on Windows"
    exit 1
    ;;
esac

echo "Done"
