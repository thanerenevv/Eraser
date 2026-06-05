#
# pre-build script: strip LVGL's ARM Helium/NEON assembly blend files.
#
# These .S files #include a C header before their `#if ... __ARM_FEATURE_MVE`
# guard, so the assembler is fed C typedefs and fails to build on the Xtensa
# (ESP32) toolchain even though the SIMD code itself is disabled. The portable
# C fallback is always compiled, so removing the .S files is safe.
#
import glob
import os

Import("env")  # noqa: F821

libdeps = env.subst("$PROJECT_LIBDEPS_DIR")  # noqa: F821

patterns = [
    "**/lvgl/src/draw/sw/blend/helium/*.S",
    "**/lvgl/src/draw/sw/blend/neon/*.S",
]

removed = 0
for pat in patterns:
    for path in glob.glob(os.path.join(libdeps, pat), recursive=True):
        try:
            os.remove(path)
            removed += 1
            print("strip_lvgl_asm: removed %s" % os.path.relpath(path, libdeps))
        except OSError as e:
            print("strip_lvgl_asm: could not remove %s (%s)" % (path, e))

if removed == 0:
    print("strip_lvgl_asm: no Helium/NEON asm files found (already clean)")
