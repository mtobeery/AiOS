#ifndef TRUST_MIND_H
#define TRUST_MIND_H

#include <Uefi.h>
#include "kernel_shared.h"

EFI_STATUS Trust_Reset(void);
UINT64 Trust_GetCurrentScore(void);
void Trust_AdjustScore(UINTN id, INTN delta);

EFI_STATUS Trust_InitPhase761_BootstrapTrustMind(KERNEL_CONTEXT *ctx);
EFI_STATUS Trust_InitPhase767_TrustCollapsePreventer(KERNEL_CONTEXT *ctx);

#endif // TRUST_MIND_H
