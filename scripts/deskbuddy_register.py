#!/usr/bin/env python3
"""Register the deskbuddy-c3 board in main/Kconfig.projbuild and main/CMakeLists.txt.
Safe to run many times. Works on any recent xiaozhi-esp32 version."""
import re, sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
kconf = root / "main" / "Kconfig.projbuild"
cmake = root / "main" / "CMakeLists.txt"

k = kconf.read_text(encoding="utf-8")
if "BOARD_TYPE_DESKBUDDY_C3" not in k:
    anchor = re.search(r"\n    config BOARD_TYPE_SURFER_C3_1_14TFT\n.*?depends on IDF_TARGET_ESP32C3\n", k, re.S)
    if not anchor:
        sys.exit("Could not find board list anchor in Kconfig.projbuild")
    entry = ('    config BOARD_TYPE_DESKBUDDY_C3\n'
             '        bool "DeskBuddy C3 (INMP441 + MAX98357A + SH1106)"\n'
             '        depends on IDF_TARGET_ESP32C3\n')
    k = k[:anchor.end()] + entry + k[anchor.end():]
    i = k.index("choice DISPLAY_OLED_TYPE")
    j = k.index("    depends on ", i)
    k = k[:j] + "    depends on BOARD_TYPE_DESKBUDDY_C3 || " + k[j + len("    depends on "):]
    kconf.write_text(k, encoding="utf-8")
    print("Kconfig.projbuild: registered")
else:
    print("Kconfig.projbuild: already registered")

c = cmake.read_text(encoding="utf-8")
if "CONFIG_BOARD_TYPE_DESKBUDDY_C3" not in c:
    old = "elseif(CONFIG_BOARD_TYPE_SURFER_C3_1_14TFT)"
    if old not in c:
        sys.exit("Could not find board anchor in CMakeLists.txt")
    new = ('elseif(CONFIG_BOARD_TYPE_DESKBUDDY_C3)\n'
           '    set(BOARD_DIR "deskbuddy-c3")\n'
           '    set(BUILTIN_TEXT_FONT font_noto_sans_basic_14_1)\n'
           '    set(BUILTIN_ICON_FONT font_material_symbols_14_1)\n' + old)
    cmake.write_text(c.replace(old, new, 1), encoding="utf-8")
    print("CMakeLists.txt: registered")
else:
    print("CMakeLists.txt: already registered")
