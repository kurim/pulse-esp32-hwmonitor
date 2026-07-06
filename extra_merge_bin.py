# PlatformIO Post-Build-Hook: erzeugt neben den einzeln zu flashenden
# Dateien (bootloader.bin/partitions.bin/boot_app0.bin/firmware.bin,
# jede an ihrem eigenen Offset) zusaetzlich ein einzelnes zusammengeführtes
# Image "merged-<env>.bin", das sich bei Offset 0x0 in einem Rutsch flashen
# laesst - z.B. per ESP Web Tools/esptool-js im Browser oder
# "esptool.py write_flash 0x0 merged-<env>.bin". Analog zu "idf.py merge-bin"
# im esp-idf-Branch.
#
# FLASH_EXTRA_IMAGES liefert PlatformIO/pioarduino selbst (bootloader.bin,
# partitions.bin, ggf. boot_app0.bin je Chip/Framework) inkl. der jeweils
# richtigen Offsets, damit muss hier nichts chip-/framework-spezifisches
# hartkodiert werden. Nur der App-Offset (0x20000) ist projektspezifisch -
# muss zum "ota_0"-Eintrag in partitions.csv passen.
Import("env")

APP_OFFSET = "0x20000"


def merge_bin(source, target, env):
    firmware = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    merged = env.subst("$BUILD_DIR/merged-$PIOENV.bin")
    chip = env.get("BOARD_MCU", "esp32")
    flash_size = env.BoardConfig().get("upload.flash_size", "4MB")

    cmd = [
        "esptool.py", "--chip", chip,
        "merge_bin", "-o", merged,
        "--flash_mode", "dio",
        "--flash_freq", "40m",
        "--flash_size", flash_size,
        "--fill-flash-size", flash_size,
    ]
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        cmd += [offset, path]
    cmd += [APP_OFFSET, firmware]

    print(f"Erzeuge zusammengefuehrtes Web-Flash-Image: {merged}")
    env.Execute(" ".join(cmd))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
