#!/usr/bin/env bash
# Jython in Ghidra 11 fails on Chinese script paths. Use temporary ASCII paths.
set -euo pipefail
recovery_root=$(cd -- "$(dirname -- "$0")" && pwd -P)
ghidra_root=${GHIDRA_INSTALL_DIR:-/home/zhangnuo/.cache/codex-tools/ghidra/ghidra_11.0.3_PUBLIC}
recovery_tmp=$(mktemp -d /tmp/stm32-ghidra.XXXXXX)
trap 'rm -rf -- "$recovery_tmp"' EXIT
mkdir -p "$recovery_tmp/scripts" "$recovery_root/ghidra_project"
cp "$recovery_root/prepare_ghidra.py" "$recovery_root/export_ghidra.py" "$recovery_tmp/scripts/"
ln -s -- "$recovery_root" "$recovery_tmp/output"
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x08000000 \
    --start-address=0x080000ec --stop-address=0x080020ce \
    "$recovery_root/../STM32F103C8T6_backup.bin" > "$recovery_root/firmware_disassembly.txt"
"$ghidra_root/support/analyzeHeadless" "$recovery_root/ghidra_project" RemoteRecovery \
    -import "$recovery_root/../STM32F103C8T6_backup.bin" -overwrite \
    -loader BinaryLoader -loader-baseAddr 0x08000000 -processor ARM:LE:32:Cortex \
    -scriptPath "$recovery_tmp/scripts" \
    -preScript prepare_ghidra.py "$recovery_tmp/output" \
    -postScript export_ghidra.py "$recovery_tmp/output" \
    -max-cpu 2 -analysisTimeoutPerFile 120 > "$recovery_root/ghidra_analysis.log" 2>&1
echo "Ghidra analysis and C reference exported to $recovery_root"
