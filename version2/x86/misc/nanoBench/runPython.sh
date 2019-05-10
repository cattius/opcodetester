#!/bin/bash

if [ "$EUID" -ne 0 ]; then
    echo "Error: nanoBench requires root privileges" 1>&2
    echo "Try \"sudo ./nanoBench-asm.sh ...\"" 1>&2
    exit 1
fi

if ! command -v rdmsr &>/dev/null; then
    echo "Error: nanoBench requires msr-tools"
    echo "Install with \"sudo apt install msr-tools\""
    exit 1
fi

prev_rdpmc=$(cat /sys/bus/event_source/devices/cpu/rdpmc)
echo 2 > /sys/bus/event_source/devices/cpu/rdpmc || exit

modprobe --first-time msr &>/dev/null
msr_prev_loaded=$?

# (Temporarily) disable watchdogs, see https://github.com/obilaniu/libpfc
! modprobe --first-time -r iTCO_wdt &>/dev/null
iTCO_wdt_prev_loaded=$?

! modprobe --first-time -r iTCO_vendor_support &>/dev/null
iTCO_vendor_support_prev_loaded=$?

prev_nmi_watchdog=$(cat /proc/sys/kernel/nmi_watchdog)
echo 0 > /proc/sys/kernel/nmi_watchdog

python testOpcodeFile.py ../../directedSearchOpcodeMapBlanks.txt

echo $prev_rdpmc > /sys/bus/event_source/devices/cpu/rdpmc
echo $prev_nmi_watchdog > /proc/sys/kernel/nmi_watchdog

if [[ $msr_prev_loaded == 0 ]]; then
    modprobe -r msr
fi

if [[ $iTCO_wdt_prev_loaded != 0 ]]; then
    modprobe iTCO_wdt &>/dev/null
fi

if [[ $iTCO_vendor_support_prev_loaded != 0 ]]; then
    modprobe iTCO_vendor_support &>/dev/null
fi
