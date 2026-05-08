/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Lrzx Inc.
 *
 * Authors:
 *   Zaiguang Hong <zaiguang.hong@riscv-computing.com>
 */
#include <sbi/sbi_domain.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_rvd.h>
#include <sbi/sbi_trap.h>

static int sbi_ecall_rvd_handler(unsigned long extid, unsigned long funcid,
				 struct sbi_trap_regs *regs,
				 struct sbi_ecall_return *out)
{
	int ret;
	ulong val;
	unsigned long tgt_hart = regs->a0;
	int csrno = (int)regs->a1;

	if (funcid != SBI_EXT_RVD_REMOTE_CSR_READ)
		return SBI_ENOTSUPP;

	if (!sbi_domain_is_assigned_hart(
		    sbi_domain_thishart_ptr(),
		    sbi_hartid_to_hartindex((u32)tgt_hart)))
		return SBI_EINVAL;

	ret = sbi_rvd_request((int)tgt_hart, csrno, &val);
	if (ret)
		return ret;

	out->value = val;
	return SBI_OK;
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
