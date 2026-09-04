#ifndef SHIM_ATOMIC_H
#define SHIM_ATOMIC_H
#define barrier()  __asm__ __volatile__("" ::: "memory")
#define smp_mb()   __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define smp_rmb()  __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define smp_wmb()  __atomic_thread_fence(__ATOMIC_RELEASE)
#define qatomic_or(ptr, n) __atomic_fetch_or((ptr), (n), __ATOMIC_SEQ_CST)
#endif
