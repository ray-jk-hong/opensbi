/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Lrzx Inc.
 *
 * Authors:
 *   Zaiguang Hong <zaiguang.hong@riscv-computing.com>
 */
#ifndef __SBI_RVD_H__
#define __SBI_RVD_H__

#include <sbi/sbi_types.h>
#include <sbi/sbi_scratch.h>

/**
 * Read CSR @csrno on physical hart @hartid (blocking; uses IPI + reply fifo).
 * @value: output XLEN value.
 */
int sbi_rvd_request(int hartid, int csrno, ulong *value);
int sbi_rvd_init(struct sbi_scratch *scratch, bool cold_boot);

#endif