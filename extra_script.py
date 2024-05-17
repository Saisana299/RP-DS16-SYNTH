Import("env") # type: ignore
# Custom HEX from ELF
env.AddPostAction( # type: ignore
    ".pio/build/pico/firmware.elf",
    env.VerboseAction(" ".join([ # type: ignore
        "$OBJCOPY", "-O", "ihex", "-R", ".eeprom",
        ".pio/build/pico/firmware.elf", ".pio/build/pico/firmware.hex"
    ]), "Building .pio\\build\\pico\\firmware.hex")
)