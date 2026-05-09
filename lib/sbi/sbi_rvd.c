/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Lrzx Inc.
 *
 * Authors:
 *   Zaiguang Hong <zaiguang.hong@riscv-computing.com>
 */
#include <sbi/riscv_asm.h>
#include <sbi/riscv_barrier.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_error.h>
#include <sbi/riscv_locks.h>
#include <sbi/sbi_fifo.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_heap.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/sbi_scratch.h>

#include <sbi/sbi_rvd.h>

#define RVD_QUE_DEPTH		4
static u32 rvd_event = SBI_IPI_EVENT_MAX; /* ipi event id */
static ulong rvd_hart_off;

#define rvd_printf(fmt, ...) \
	sbi_printf("[RVD] %s:%d: " fmt, __func__, __LINE__, ##__VA_ARGS__)

struct rvd_entity {
	u32 req_hartid;   /* physical hartid of caller (replies go to its deq) */
	u32 dst_hartid;   /* physical hartid where CSR should be read */
	int csrno;
	ulong value;
	ulong *value_out; /* caller's slot; valid until sync completes */
};

struct rvd_que {
	void *base; 	/* que base addr */
	u32 depth; 	/* que depth (entries) */
	struct sbi_fifo fifo;
};

struct rvd_hart {
	struct rvd_que enq;
	struct rvd_que deq;
};

static unsigned long rvd_csr_read(int csrno)
{
#define switchcase_csr_read(__csr_num, __val)		\
	case __csr_num:					\
		__val = csr_read(__csr_num);		\
		break;

	unsigned long ret = 0;

	switch (csrno) {
	switchcase_csr_read(CSR_CYCLE, ret);
	switchcase_csr_read(CSR_TIME, ret);
	switchcase_csr_read(CSR_INSTRET, ret);

	switchcase_csr_read(CSR_SSTATUS, ret);
	switchcase_csr_read(CSR_SIE, ret);
	switchcase_csr_read(CSR_STVEC, ret);
	switchcase_csr_read(CSR_SCOUNTEREN, ret);
	switchcase_csr_read(CSR_SENVCFG, ret);
	switchcase_csr_read(CSR_SSCRATCH, ret);
	switchcase_csr_read(CSR_SEPC, ret);
	switchcase_csr_read(CSR_SCAUSE, ret);
	switchcase_csr_read(CSR_STVAL, ret);
	switchcase_csr_read(CSR_SIP, ret);
	switchcase_csr_read(CSR_SATP, ret);

	switchcase_csr_read(CSR_MCYCLE, ret);
	switchcase_csr_read(CSR_MCAUSE, ret);
	switchcase_csr_read(CSR_MTVAL, ret);
	switchcase_csr_read(CSR_MIP, ret);
	switchcase_csr_read(CSR_MIE, ret);
	switchcase_csr_read(CSR_MCOUNTEREN, ret);
	switchcase_csr_read(CSR_MSCRATCH, ret);
	switchcase_csr_read(CSR_MVENDORID, ret);
	switchcase_csr_read(CSR_MARCHID, ret);
	switchcase_csr_read(CSR_MIMPID, ret);
	switchcase_csr_read(CSR_MHARTID, ret);
	switchcase_csr_read(CSR_MCONFIGPTR, ret);
	switchcase_csr_read(CSR_MSTATUS, ret);
	switchcase_csr_read(CSR_MISA, ret);
	switchcase_csr_read(CSR_MEDELEG, ret);
	switchcase_csr_read(CSR_MIDELEG, ret);
	switchcase_csr_read(CSR_MENVCFG, ret);

	default:
		break;
	};

#undef switchcase_csr_read
	return ret;
}

/* Work queue: other harts enqueue jobs we execute on this hart. */
static int rvd_work_enqueue(struct sbi_scratch *scratch, struct rvd_entity *entity)
{
	struct rvd_hart *hart = sbi_scratch_offset_ptr(scratch, rvd_hart_off);

	return sbi_fifo_enqueue(&hart->enq.fifo, entity, false);
}

static int rvd_work_dequeue(struct sbi_scratch *scratch, struct rvd_entity *entity)
{
	struct rvd_hart *hart = sbi_scratch_offset_ptr(scratch, rvd_hart_off);

	return sbi_fifo_dequeue(&hart->enq.fifo, entity);
}

/* Reply queue: results for requests issued by this hart (waited on in rvd_sync). */
static int rvd_reply_enqueue(struct sbi_scratch *scratch, struct rvd_entity *entity)
{
	struct rvd_hart *hart = sbi_scratch_offset_ptr(scratch, rvd_hart_off);

	return sbi_fifo_enqueue(&hart->deq.fifo, entity, false);
}

static int rvd_ipi_send(struct rvd_entity *entity)
{
	return sbi_ipi_send_many(1, entity->dst_hartid, rvd_event, entity);
}

static int _rvd_que_init(struct rvd_que *que)
{
	size_t size = (size_t)RVD_QUE_DEPTH * sizeof(struct rvd_entity);
	void *base;

	base = sbi_malloc(size);
	if (!base) {
		rvd_printf("fifo malloc fail. (size=%lu)\n", size);
		return SBI_ENOMEM;
	}
	sbi_fifo_init(&que->fifo, base, RVD_QUE_DEPTH,
		(u16)sizeof(struct rvd_entity));

	que->base = base;
	que->depth = RVD_QUE_DEPTH;

	return SBI_OK;
}

static void _rvd_que_uninit(struct rvd_que *que)
{
	if (!que->base)
		return;

	sbi_free(que->base);
	que->base = NULL;
	que->depth = 0;
}

static int rvd_que_init(struct sbi_scratch *scratch)
{
	ulong hartidx = scratch->hartindex;
	struct rvd_hart *hart;
	int ret;

	hart = sbi_scratch_offset_ptr(scratch, rvd_hart_off);
	ret = _rvd_que_init(&hart->enq);
	if (ret) {
		rvd_printf("enq init fail. (ret=%d; hartidx=%lu)\n",
			ret, hartidx);
		return ret;
	}
	ret = _rvd_que_init(&hart->deq);
	if (ret) {
		_rvd_que_uninit(&hart->enq);
		rvd_printf("deq init fail. (ret=%d; hartidx=%lu)\n",
			ret, hartidx);
		return ret;
	}
	return SBI_OK;
}

static inline void rvd_local_process(struct rvd_entity *entity)
{
	entity->value = rvd_csr_read(entity->csrno);
}

/**
 * Called on the requesting hart after sbi_ipi_send_many finishes sending
 * (including self-IPI BREAK path). Wait until one reply is present on
 * this hart's reply queue, then copy the CSR value to the caller's buffer.
 */
static void rvd_sync(struct sbi_scratch *scratch)
{
	struct rvd_hart *hart = sbi_scratch_offset_ptr(scratch, rvd_hart_off);
	struct rvd_entity reply;

	while (sbi_fifo_dequeue(&hart->deq.fifo, &reply) == SBI_ENOENT)
		cpu_relax();

	if (reply.value_out)
		*reply.value_out = reply.value;
}

static void rvd_process(struct sbi_scratch *scratch)
{
	struct rvd_entity work;
	struct sbi_scratch *req_scratch;

	/* Process every pending job delivered before this IPI was raised. */
	while (rvd_work_dequeue(scratch, &work) == SBI_OK) {
		u32 req_idx = sbi_hartid_to_hartindex(work.req_hartid);

		rvd_local_process(&work);

		if (req_idx == -1U) {
			rvd_printf("req hartid invalid. (req_hartid=%u)\n",
				   work.req_hartid);
			continue;
		}

		req_scratch = sbi_hartindex_to_scratch(req_idx);
		if (!req_scratch) {
			rvd_printf("req scratch null. (req_hartid=%u)\n",
				   work.req_hartid);
			continue;
		}

		if (rvd_reply_enqueue(req_scratch, &work)) {
			rvd_printf("reply enqueue fail. (req_hartid=%u)\n",
				   work.req_hartid);
		}
	}
}

static int rvd_update(struct sbi_scratch *scratch,
	struct sbi_scratch *remote_scratch,
	u32 remote_hartindex, void *data)
{
	struct rvd_entity *entity = (struct rvd_entity *)data;
	u32 curr_hartid = current_hartid();
	int ret;

	if (sbi_hartindex_to_hartid(remote_hartindex) == curr_hartid) {
		/*
		 * Same hart: no IPI. Fill value and stage one reply so rvd_sync
		 * still consumes a single deq item (matches remote path).
		 */
		rvd_local_process(entity);
		ret = rvd_reply_enqueue(scratch, entity);
		if (ret)
			return SBI_IPI_UPDATE_RETRY;
		return SBI_IPI_UPDATE_BREAK;
	}
	ret = rvd_work_enqueue(remote_scratch, entity);
	if (ret)
		return SBI_IPI_UPDATE_RETRY;
	return SBI_IPI_UPDATE_SUCCESS;
}

static struct sbi_ipi_event_ops rvd_ops = {
	.name = 	"IPI_RVD",
	.update = 	rvd_update,
	.sync = 	rvd_sync,
	.process = 	rvd_process,
};

static int rvd_warm_init(struct sbi_scratch *scratch)
{
	if (!rvd_event || !rvd_hart_off) {
		rvd_printf("rvd isn't boot. (hartidx=%lu)\n",
			scratch->hartindex);
		return SBI_ENOSPC;
	}

	return 0;
}

static int rvd_cold_init(struct sbi_scratch *scratch)
{
	ulong hartidx = scratch->hartindex;
	ulong hart_off;
	int ret;

	hart_off = sbi_scratch_alloc_offset(sizeof(struct rvd_hart));
	if (!hart_off) {
		rvd_printf("hart off alloc fail. (hartidx=%lu; size=%lu)\n",
			hartidx, (ulong)sizeof(struct rvd_hart));
		return SBI_ENOMEM;
	}

	ret = sbi_ipi_event_create(&rvd_ops);
	if (ret < 0) {
		sbi_scratch_free_offset(hart_off);
		rvd_printf("ipi create fail. (ret=%d; hartidx=%lu)\n",
			ret, hartidx);
		return ret;
	}
	rvd_event = ret;
	rvd_hart_off = hart_off;

	return 0;
}

int sbi_rvd_request(int hartid, int csrno, ulong *value)
{
	struct rvd_entity entity;
	int ret;

	entity.req_hartid = current_hartid();
	entity.dst_hartid = (u32)hartid;
	entity.csrno = csrno;
	entity.value = 0;
	entity.value_out = value;

	ret = rvd_ipi_send(&entity);
	if (ret) {
		rvd_printf("ipi send fail. (ret=%d; hartid=%d; csrno=0x%x)\n",
			   ret, hartid, (u32)csrno);
	}

	return ret;
}

int sbi_rvd_init(struct sbi_scratch *scratch, bool cold_boot)
{
	ulong hartidx = scratch->hartindex;
	int ret;

	ret = (cold_boot) ? rvd_cold_init(scratch) : rvd_warm_init(scratch);
	if (ret) {
		rvd_printf("init fail. (ret=%d; hartidx=%lu; cold_boot=%d)\n",
			ret, hartidx, cold_boot);
		return ret;
	}

	ret = rvd_que_init(scratch);
	if (ret) {
		rvd_printf("que init fail. (ret=%d; hartidx=%lu)\n",
			ret, hartidx);
	}

	return ret;
}
