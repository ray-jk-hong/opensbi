/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Lrzx Inc.
 *
 * Authors:
 *   Zaiguang Hong <zaiguang.hong@riscv-computing.com>
 */
#include <sbi/sbi_error.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_trap.h>

static int sbi_rvd_get_csr(unsigned long csr, unsigned long *value)
{
	return 0;
}

static int sbi_rvd_get_all_csrs(unsigned long count, unsigned long *values)
{
	return 0;
}

static int sbi_ecall_rvd_handler(unsigned long extid, unsigned long funcid,
					struct sbi_trap_regs *regs,
					struct sbi_ecall_return *out)
{
	int ret = SBI_ENOTSUPP;

	switch (funcid) {
	case SBI_EXT_RVD_GET_ALL_CSRS:
		ret = sbi_rvd_get_all_csrs(regs->a0, &out->value);
		break;
	case SBI_EXT_RVD_GET_CSR:
		ret = sbi_rvd_get_csr(regs->a0, &out->value);
		break;
	default:
		break;
	}

	return ret;
}

struct sbi_ecall_extension ecall_rvd;

static int sbi_ecall_rvd_register_extensions(void)
{
	return sbi_ecall_register_extension(&ecall_rvd);
}

struct sbi_ecall_extension ecall_rvd = {
	.name			= "rvd",
	.extid_start		= SBI_EXT_RVD,
	.extid_end		= SBI_EXT_RVD,
	.register_extensions	= sbi_ecall_rvd_register_extensions,
	.handle			= sbi_ecall_rvd_handler,
};
