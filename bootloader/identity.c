#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "sha256.h"

void read_cpuid_and_ram(char *cpu_vendor, size_t vendor_len, uint64_t *ram)
{
#ifdef __x86_64__
    uint32_t eax, ebx, ecx, edx;
    eax = 0;
    __asm__ __volatile__("cpuid" : "=b"(ebx), "=d"(edx), "=c"(ecx) : "a"(eax));
    memcpy(cpu_vendor, &ebx, 4);
    memcpy(cpu_vendor + 4, &edx, 4);
    memcpy(cpu_vendor + 8, &ecx, 4);
    cpu_vendor[12] = '\0';
#else
    strncpy(cpu_vendor, "UNKNOWN", vendor_len);
#endif
    *ram = (uint64_t)sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
}

void scan_pci_for_gpu(char *gpu, size_t len)
{
    // Stub: return dummy GPU id
    strncpy(gpu, "GenericGPU", len);
}

void generate_ai_boot_dna(const char *cpu, uint64_t ram, const char *gpu)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s-%llu-%s-TPM", cpu, (unsigned long long)ram, gpu);
    uint8_t hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, (uint8_t*)buffer, strlen(buffer));
    sha256_final(&ctx, hash);
    printf("AI_BOOT_DNA: ");
    for(int i=0;i<SHA256_DIGEST_LENGTH;i++)
        printf("%02x", hash[i]);
    printf("\n");
}
