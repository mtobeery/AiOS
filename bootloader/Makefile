
TARGET = AiOSLoader
ARCH = x86_64
EFIINC = /usr/include/efi
EFILIB = /usr/lib

CC = x86_64-linux-gnu-gcc
CFLAGS = -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall -Wextra -O2 \
    -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFILIB) -DEFI_FUNCTION_WRAPPER
LDFLAGS = -nostdlib -znocombreloc -T /usr/lib/elf_$(ARCH)_efi.lds \
    --defsym=EFI_SUBSYSTEM=0xa --oformat=efi-app-$(ARCH)

OBJCOPY = objcopy

all: $(TARGET).efi

$(TARGET).efi: main.o
	ld $(LDFLAGS) -o $(TARGET).efi main.o
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	    -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) $(TARGET).efi $(TARGET)_final.efi

main.o: main.c loader_structs.h
	$(CC) $(CFLAGS) -c main.c -o main.o

clean:
	rm -f *.o *.efi *_final.efi
