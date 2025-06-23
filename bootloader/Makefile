CC=x86_64-elf-gcc
CFLAGS=-ffreestanding -fpic -fshort-wchar -mno-red-zone -Wall -I../include -I/usr/include/efi -I/usr/include/efi/x86_64
LDFLAGS=-nostdlib -T /usr/lib/gnuefi/elf_x86_64_efi.lds -shared -Bsymbolic -L/usr/lib -lefi -lgnuefi

OBJS=main.o identity.o sha256.o

all: BOOTX64.EFI

BOOTX64.EFI: $(OBJS)
	$(CC) $(CFLAGS) -o BOOTX64.so $(OBJS) $(LDFLAGS)
	 objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel -j .rela -j .reloc -j .eh_frame --target=efi-app-x86_64 BOOTX64.so BOOTX64.EFI

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	 rm -f $(OBJS) BOOTX64.so BOOTX64.EFI
