#ifndef _CPU_RUNTIME_H
#define _CPU_RUNTIME_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

extern "C" {
    // parallel runtime functions
    void parallel_for(int num_threads, int lower, int upper, void *args, uint64_t arg_size, void *fun);
}

#endif
