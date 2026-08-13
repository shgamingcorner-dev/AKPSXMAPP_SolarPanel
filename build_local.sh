#!/usr/bin/env bash
# Local ARM build script for AKPS Solar Panel firmware.
# Builds with arm-none-eabi-gcc + mbed-os 6.16 + cmake/ninja (no Keil Cloud).
#
# Usage: bash build_local.sh
# Output: cmake_build/NUCLEO_F103RB/develop/GCC_ARM/mbed-os-example-blinky.bin/.hex
#
# NOTE: requires mbed-os/ clone in repo root (gitignored):
#   git clone --depth 1 --branch mbed-os-6.16.0 https://github.com/ARMmbed/mbed-os.git mbed-os
set -e

export PATH="/c/Program Files/CMake/bin:/c/Users/shgam/AppData/Local/Microsoft/WinGet/Packages/Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe:/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin:$PATH"
NINJA="C:/Users/shgam/AppData/Local/Microsoft/WinGet/Packages/Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe/ninja.exe"
MBED_TOOLS="/c/Users/shgam/AppData/Roaming/Python/Python314/Scripts/mbed-tools.exe"

# 1. Generate mbed config
"$MBED_TOOLS" configure -m NUCLEO_F103RB -t GCC_ARM --app-config mbed_app.json 2>/dev/null

# 2. CMake configure
cmake -S . -B cmake_build/NUCLEO_F103RB/develop/GCC_ARM -G Ninja \
    -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -DMBED_TARGET=NUCLEO_F103RB \
    -DCMAKE_TOOLCHAIN_FILE=mbed-os/tools/cmake/toolchains/GCC_ARM.cmake \
    -DBUILD_TESTING=OFF \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY

# 3. Patch ninja files (CMake 4.4 Windows host leaks ARM-incompatible flags)
python - <<'EOF'
import re
p = r'cmake_build/NUCLEO_F103RB/develop/GCC_ARM/CMakeFiles/rules.ninja'
c = open(p, encoding='utf-8').read()
c = c.replace(' -Wl,--out-implib,$TARGET_IMPLIB -Wl,--major-image-version,0,--minor-image-version,0', '')
open(p, 'w', encoding='utf-8', newline='\n').write(c)
p2 = r'cmake_build/NUCLEO_F103RB/develop/GCC_ARM/build.ninja'
c = open(p2, encoding='utf-8').read()
c = c.replace('  LINK_LIBRARIES = -lkernel32 -luser32 -lgdi32 -lwinspool -lshell32 -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32', '  LINK_LIBRARIES = ')
open(p2, 'w', encoding='utf-8', newline='\n').write(c)
print("Patched ninja files")
EOF

# 4. Build (the post-build objcopy may fail if PATH isn't inherited by nested
#    cmd.exe — that's OK, we generate .bin/.hex manually in step 5)
"$NINJA" -C cmake_build/NUCLEO_F103RB/develop/GCC_ARM || true

# 5. Post-build (objcopy may not be on PATH in the nested cmd)
ARM="/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin"
B=cmake_build/NUCLEO_F103RB/develop/GCC_ARM
if [ ! -f "$B/mbed-os-example-blinky.bin" ]; then
    "$ARM/arm-none-eabi-objcopy" -O binary "$B/mbed-os-example-blinky.elf" "$B/mbed-os-example-blinky.bin"
    "$ARM/arm-none-eabi-objcopy" -O ihex "$B/mbed-os-example-blinky.elf" "$B/mbed-os-example-blinky.hex"
fi

echo ""
echo "=== BUILD COMPLETE ==="
ls -la "$B/mbed-os-example-blinky.bin" "$B/mbed-os-example-blinky.hex"
echo "BIN size: $(stat -c%s "$B/mbed-os-example-blinky.bin") bytes (flash = 131072)"
