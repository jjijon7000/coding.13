/*
 * OSTEP Chapter 19 (TLB) homework answers
 *
 * Q1: how precise is gettimeofday(), and how long must an operation take?
 *     gettimeofday() reports microseconds, so its resolution is about
 *     1 us. one page access takes only a few ns, so a single access can't
 *     be timed. to time it, repeat the access loop many times so the total
 *     runs for a long time (ideally ~1 second, at least a few ms). then the
 *     ~1 us timer error is tiny compared to the total, and dividing by the
 *     number of accesses gives the average cost per access.
 *
 * Q2: the program is tlb.c. usage: ./tlb <num_pages> <num_trials>
 *     it allocates num_pages pages, touches one int per page (stepping by
 *     page_size / sizeof(int)) for num_trials trials, and prints the
 *     average nanoseconds per access. example: ./tlb 1024 100000
 *
 * Q5: how to keep the compiler from removing the main loop?
 *     the loop only increments array values that nothing else reads, so
 *     the optimizer could delete it. this program avoids that by declaring
 *     the array pointer volatile (every access must really happen) and by
 *     printing a checksum of the array afterward so the result is used.
 *     compiling with -O0 would also work.
 *
 * Q6: how to run on just one CPU, and what happens if you don't?
 *     pin the process to one CPU with sched_setaffinity() (used here), or
 *     run it with "taskset -c 0 ./tlb 1024 100000". without pinning, the
 *     scheduler may move the process between CPUs. each CPU has its own
 *     TLB, so after a move the new CPU's TLB is cold and the program takes
 *     extra misses, making the timings noisy and inflated.
 *
 * Q7: does initialization affect the timing, and what can be done?
 *     yes. if the array isn't touched before timing, the first access to
 *     each page causes a page fault and the OS must allocate and zero the
 *     page (demand zeroing). that costs far more than a TLB miss and would
 *     swamp the measurement. to counter it, this program writes to every
 *     page before timing starts and does one warm-up pass over the pages,
 *     so only TLB behavior is measured in the timed loop.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/time.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <num_pages> <num_trials>\n", argv[0]);
        return 1;
    }

    long num_pages = atol(argv[1]);
    long num_trials = atol(argv[2]);
    if (num_pages <= 0 || num_trials <= 0) {
        fprintf(stderr, "error: num_pages and num_trials must be positive\n");
        return 1;
    }

    /* Pin to CPU 0 so timings are not inflated by cold TLBs on other CPUs */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        perror("sched_setaffinity");
        return 1;
    }

    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        fprintf(stderr, "error: could not get page size\n");
        return 1;
    }

    long jump = page_size / (long)sizeof(int);
    size_t nints = (size_t)num_pages * (size_t)jump;

    /* volatile so the compiler cannot optimize away the timed loop */
    volatile int *a = malloc(nints * sizeof(int));
    if (a == NULL) {
        perror("malloc");
        return 1;
    }

    /* Touch every page before timing (avoid page faults / demand zeroing) */
    for (size_t i = 0; i < nints; i++) {
        a[i] = 0;
    }

    /* Warm-up pass */
    for (long i = 0; i < num_pages * jump; i += jump) {
        a[i] += 1;
    }

    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (long t = 0; t < num_trials; t++) {
        for (long i = 0; i < num_pages * jump; i += jump) {
            a[i] += 1;
        }
    }

    gettimeofday(&end, NULL);

    double elapsed_us =
        (double)(end.tv_sec - start.tv_sec) * 1e6 +
        (double)(end.tv_usec - start.tv_usec);
    double elapsed_ns = elapsed_us * 1e3;
    double accesses = (double)num_pages * (double)num_trials;
    double ns_per_access = elapsed_ns / accesses;

    long checksum = 0;
    for (long i = 0; i < num_pages * jump; i += jump) {
        checksum += a[i];
    }

    printf("pages=%ld trials=%ld avg_ns_per_access=%.2f checksum=%ld\n",
           num_pages, num_trials, ns_per_access, checksum);

    free((void *)a);
    return 0;
}
