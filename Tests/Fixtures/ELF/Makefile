# ELF Fixture Generator
# Creates binary fixtures for testing ELF loading

CC=clang
CFLAGS=-target aarch64-linux-gnu -static -nostdlib -Wl,--build-id=none

all: static_minimal_aarch64_ok.elf static_bad_magic.bin static_wrong_machine.bin

static_minimal_aarch64_ok.elf: minimal_entry.S
	$(CC) $(CFLAGS) -o $@ $< 

static_bad_magic.bin: static_minimal_aarch64_ok.elf
	@echo "Creating bad magic fixture..."
	@head -c 4 $< > $@
	@echo "XXXX" | head -c 4 | dd of=$@ bs=1 seek=4 conv=notrunc 2>/dev/null
	@tail -c +9 $< >> $@

static_wrong_machine.bin: static_minimal_aarch64_ok.elf
	@echo "Creating wrong machine fixture..."
	@cp $< $@
	@echo -e '\x3c\x00' | dd of=$@ bs=1 seek=18 conv=notrunc 2>/dev/null

clean:
	rm -f *.elf *.bin

.PHONY: all clean
