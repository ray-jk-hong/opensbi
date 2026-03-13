/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 LYNX
 *
 */

#include <platform_override.h>
#include <libfdt.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_system.h>
#include <sbi/sbi_console.h>
#include <sbi_utils/fdt/fdt_driver.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/i2c/fdt_i2c.h>

static int boot_hartid = -1;

static bool lynx_p100_cold_boot_allowed(u32 hartid)
{
	if (boot_hartid == -1) {
		return generic_cold_boot_allowed(hartid);
	}
	return boot_hartid == hartid;
}

static int lynx_p100_platform_init(const void *fdt, int nodeoff, const struct fdt_match *match)
{
	const fdt32_t *val;
	int len, coff;

	coff = fdt_path_offset(fdt, "/chosen");
	if (coff > -1) {
		val = fdt_getprop(fdt, coff, "boot-hart-id", &len);
		if (val && len >= sizeof(fdt32_t))
			boot_hartid = (u32) fdt32_to_cpu(*val);
	}
	generic_platform_ops.cold_boot_allowed = lynx_p100_cold_boot_allowed;
	return 0;
}

static const struct fdt_match lynx_p100_match[] = {
	{ .compatible = "lynx,p100" },
	{ },
};

const struct fdt_driver lynx_p100 = {
	.match_table = lynx_p100_match,
	.init = lynx_p100_platform_init,
};
