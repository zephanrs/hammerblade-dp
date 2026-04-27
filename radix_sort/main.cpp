// Host driver for radix_sort. Mirrors sw/1d/main.cpp's structure for the
// build/launch pipeline. The sort experiment runs on a single pod (pod 0):
// init still happens for every pod (don't change init in case it breaks
// other things), but only pod 0 gets buffers, DMA, kernel enqueue, and
// validation. Each kernel invocation sorts NUM_ARR distinct SIZE-element
// arrays so per-sort timing is the average over a ~20s run, with
// average-case (random, non-presorted) input on every iteration.
//
// Validation checks that each output array is non-decreasing — cheaper
// than CPU-sorting an oracle for billion-element runs. rand() returns
// non-negative ints so unsigned-radix == signed-ascending here.

#include <bsg_manycore_errno.h>
#include <bsg_manycore_cuda.h>
#include <cstdlib>
#include <time.h>
#include <cstring>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <bsg_manycore_regression.h>
#include <bsg_manycore.h>
#include <cstdint>
#include <vector>
#include <algorithm>
#include "../common/host_bench.hpp"

#define ALLOC_NAME "default_allocator"

#ifndef SIZE
#define SIZE 16384
#endif

#ifndef NUM_ARR
#define NUM_ARR 1
#endif

static void print_first_values(const char *label, const int *values, int n_total) {
  const int n = std::min(16, n_total);
  printf("%s first %d:", label, n);
  for (int i = 0; i < n; i++) {
    printf(" %d", values[i]);
  }
  printf("\n");
}

int radix_sort_multipod(int argc, char ** argv) {
  // command line — matches sw/1d's pattern (argv[1] = bin path).
  const char *bin_path = argv[1];

  printf("size=%d\n", SIZE);
  printf("num_arr=%d\n", NUM_ARR);
  fflush(stdout);

  // Generate NUM_ARR distinct random arrays of SIZE ints each, contiguous
  // in one packed buffer. No CPU oracle: the device output is checked for
  // non-decreasing order rather than equality with a host sort.
  const size_t total = (size_t)NUM_ARR * (size_t)SIZE;
  srand(42);
  std::vector<int> host_in(total);
  for (size_t i = 0; i < total; i++) {
    host_in[i] = rand();
  }
  print_first_values("host input arr0", host_in.data(), SIZE);

  // Initialize device.
  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "radix_sort_multipod", HB_MC_DEVICE_ID));

  eva_t d_a, d_b;
  std::vector<eva_t> d_a_by_pod(NUM_POD_X * NUM_POD_Y);
  std::vector<eva_t> d_b_by_pod(NUM_POD_X * NUM_POD_Y);

  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod) {
    printf("Loading program for pod %d\n", pod);
    fflush(stdout);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    // Single-pod sort experiment: only pod 0 runs the kernel.
    if (pod != 0) continue;

    // Allocate: A holds NUM_ARR packed input arrays (each sorted in place
    // by the kernel into the same slot); B is a single SIZE-int scratch
    // buffer reused across iterations for the ping-pong.
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, total * sizeof(int), &d_a));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, SIZE  * sizeof(int), &d_b));
    if (pod >= d_a_by_pod.size()) {
      d_a_by_pod.resize(pod + 1);
      d_b_by_pod.resize(pod + 1);
    }
    d_a_by_pod[pod] = d_a;
    d_b_by_pod[pod] = d_b;
    printf("Device buffers: pod %d A=%u (bytes=%zu) B=%u (bytes=%zu)\n",
           pod, (unsigned)d_a, total * sizeof(int),
           (unsigned)d_b, (size_t)SIZE * sizeof(int));

    // DMA the entire packed input (NUM_ARR * SIZE ints) into device A.
    printf("Transferring data: pod %d\n", pod);
    fflush(stdout);
    std::vector<hb_mc_dma_htod_t> htod_job;
    htod_job.push_back({d_a, host_in.data(), (uint32_t)(total * sizeof(int))});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod_job.data(), htod_job.size()));

    // Cuda args. The kernel walks A in NUM_ARR strides of SIZE ints; each
    // stride is sorted in place, using B as scratch for the ping-pong.
    hb_mc_dimension_t tg_dim   = {.x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = {.x = 1, .y = 1};
    #define CUDA_ARGC 4
    uint32_t cuda_argv[CUDA_ARGC] = {d_a, d_b, (uint32_t)SIZE, (uint32_t)NUM_ARR};

    printf("Enqueue Kernel: pod %d\n", pod);
    fflush(stdout);
    BSG_CUDA_CALL(hb_mc_kernel_enqueue(&device, grid_dim, tg_dim,
                                       "kernel", CUDA_ARGC, cuda_argv));
  }

  // Launch.
  printf("Launching all pods\n");
  fflush(stdout);
  timespec kernel_start = {};
  timespec kernel_end = {};
  hb_mc_manycore_trace_enable((&device)->mc);
  clock_gettime(CLOCK_MONOTONIC, &kernel_start);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  clock_gettime(CLOCK_MONOTONIC, &kernel_end);
  hb_mc_manycore_trace_disable((&device)->mc);
  print_kernel_launch_time(kernel_start, kernel_end);
  fflush(stdout);

  // Read back NUM_ARR * SIZE sorted ints; validate each chunk is
  // non-decreasing (signed compare; rand() returns non-negative).
  std::vector<int> result(total);
  bool fail = false;
  hb_mc_device_foreach_pod_id(&device, pod) {
    if (pod != 0) continue;
    printf("Reading results: pod %d\n", pod);
    fflush(stdout);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));

    std::vector<hb_mc_dma_dtoh_t> dtoh_job;
    std::fill(result.begin(), result.end(), 0);
    dtoh_job.push_back({d_a_by_pod[pod], result.data(), (uint32_t)(total * sizeof(int))});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&device, dtoh_job.data(), dtoh_job.size()));

    int bad_arr = -1, bad_idx = -1;
    for (int a = 0; a < NUM_ARR && bad_arr < 0; a++) {
      const size_t base = (size_t)a * SIZE;
      for (int i = 1; i < SIZE; i++) {
        if (result[base + i] < result[base + i - 1]) {
          bad_arr = a;
          bad_idx = i;
          break;
        }
      }
    }
    if (bad_arr >= 0) {
      const size_t base = (size_t)bad_arr * SIZE;
      printf("Out-of-order pod %d: arr=%d i=%d, result[i-1]=%d > result[i]=%d\n",
             pod, bad_arr, bad_idx,
             result[base + bad_idx - 1], result[base + bad_idx]);
      print_first_values("result arr", result.data() + base, SIZE);
      fail = true;
    } else {
      printf("correct pod %d (all %d arrays non-decreasing)\n", pod, NUM_ARR);
      print_first_values("result arr0", result.data(), SIZE);
    }
  }

  BSG_CUDA_CALL(hb_mc_device_finish(&device));
  return fail ? HB_MC_FAIL : HB_MC_SUCCESS;
}

declare_program_main("radix_sort_multipod", radix_sort_multipod);
