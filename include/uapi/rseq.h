#ifndef _CUTEOS_UAPI_RSEQ_H
#define _CUTEOS_UAPI_RSEQ_H

#define RSEQ_CPU_ID_UNINITIALIZED ((unsigned int)-1)
#define RSEQ_CPU_ID_REGISTRATION_FAILED ((unsigned int)-2)

#define RSEQ_FLAG_UNREGISTER (1 << 0)

#define RSEQ_CS_FLAG_NO_RESTART_ON_PREEMPT (1U << 0)
#define RSEQ_CS_FLAG_NO_RESTART_ON_SIGNAL  (1U << 1)
#define RSEQ_CS_FLAG_NO_RESTART_ON_MIGRATE (1U << 2)

struct rseq_cs {
	unsigned int version;
	unsigned int flags;
	unsigned long start_ip;
	unsigned long post_commit_offset;
	unsigned long abort_ip;
} __attribute__((aligned(4 * sizeof(unsigned long))));

struct rseq {
	unsigned int cpu_id_start;
	unsigned int cpu_id;
	unsigned long rseq_cs;
	unsigned int flags;
	unsigned int node_id;
	unsigned int mm_cid;
	char end[];
} __attribute__((aligned(4 * sizeof(unsigned long))));

#define RSEQ_OFFSETOF(t, d) __builtin_offsetof(t, d)

_Static_assert(sizeof(struct rseq_cs) == 32, "rseq_cs ABI size mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq_cs, version) == 0,
	       "rseq_cs version offset mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq_cs, flags) == 4,
	       "rseq_cs flags offset mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq_cs, start_ip) == 8,
	       "rseq_cs start_ip offset mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq_cs, post_commit_offset) == 16,
	       "rseq_cs post_commit_offset offset mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq_cs, abort_ip) == 24,
	       "rseq_cs abort_ip offset mismatch");
_Static_assert(sizeof(struct rseq) == 32, "rseq ABI size mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq, cpu_id_start) == 0,
	       "rseq cpu_id_start offset mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq, cpu_id) == 4,
	       "rseq cpu_id offset mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq, rseq_cs) == 8,
	       "rseq rseq_cs offset mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq, flags) == 16,
	       "rseq flags offset mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq, node_id) == 20,
	       "rseq node_id offset mismatch");
_Static_assert(RSEQ_OFFSETOF(struct rseq, mm_cid) == 24,
	       "rseq mm_cid offset mismatch");

#undef RSEQ_OFFSETOF

#endif
