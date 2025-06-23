ARCH := x86_64
CROSS_COMPILE ?= x86_64-linux-gnu-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy

EFIINC := /usr/include/efi
EFILIB := /usr/lib

CFLAGS := -ffreestanding -fpic -fshort-wchar -mno-red-zone -fno-stack-protector -I$(EFIINC) -I$(EFIINC)/$(ARCH) -Wall -Wextra -DEFI_FUNCTION_WRAPPER
LDFLAGS := -shared -Bsymbolic  -nostdlib -znocombreloc -T /usr/lib/elf_$(ARCH)_efi.lds
LIBS := -L$(EFILIB) /usr/lib/crt0-efi-$(ARCH).o -lefi -lgnuefi

TARGET := BOOTX64.EFI
OBJS := main.o

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc -j .eh_frame --target=efi-app-$(ARCH) $@ $@

clean:
	rm -f $(TARGET) $(OBJS)
