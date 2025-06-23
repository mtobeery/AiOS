#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "sha256.h"
#include <unistd.h>
void read_cpuid_and_ram(char*,size_t,uint64_t*);
void scan_pci_for_gpu(char*,size_t);
void generate_ai_boot_dna(const char*,uint64_t,const char*);

static void print_hash(const uint8_t *hash)
{
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        printf("%02x", hash[i]);
    printf("\n");
}

static int compute_kernel_hash(const char *path, uint8_t out[SHA256_DIGEST_LENGTH])
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("open kernel");
        return -1;
    }
    SHA256_CTX ctx;
    sha256_init(&ctx);
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        sha256_update(&ctx, buf, n);
    fclose(f);
    sha256_final(&ctx, out);
    return 0;
}

static void extend_hash_to_tpm(const uint8_t *hash)
{
    // Stub: in real bootloader use EFI_TCG2 HashLogExtendEvent
    printf("[TPM] Extend PCR[4] with hash\n");
}

static int verify_signature(const char *path)
{
    // Stub: no real secure boot verification
    (void)path;
    return 0; // pretend valid
}

static void handle_verification_failure(void)
{
    printf("Verification failed\n");
    exit(1);
}

static int init_gop(void)
{
    // Stub: always fail to keep simple
    return 0;
}

static void render_test_rectangle(void)
{
    printf("Render rectangle in framebuffer (stub)\n");
}

static void fallback_text_mode(void)
{
    printf("Fallback to text mode\n");
}

int main(void)
{
    uint8_t kernel_hash[SHA256_DIGEST_LENGTH];
    if (compute_kernel_hash("kernel.elf", kernel_hash) == 0) {
        printf("kernel.elf SHA256: ");
        print_hash(kernel_hash);
        extend_hash_to_tpm(kernel_hash);
        if (verify_signature("kernel.elf") != 0)
            handle_verification_failure();
    } else {
        handle_verification_failure();
    }

    if (init_gop())
        render_test_rectangle();
    else
        fallback_text_mode();

    char cpu[13];
    uint64_t ram;
    read_cpuid_and_ram(cpu, sizeof(cpu), &ram);
    printf("CPU Vendor: %s RAM: %llu bytes\n", cpu, (unsigned long long)ram);

    char gpu[32];
    scan_pci_for_gpu(gpu, sizeof(gpu));
    printf("GPU ID: %s\n", gpu);

    generate_ai_boot_dna(cpu, ram, gpu);
    return 0;
}
