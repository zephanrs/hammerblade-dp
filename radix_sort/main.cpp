// Host driver for radix_sort. Mirrors sw/1d/main.cpp's structure exactly so
// the build/launch pipeline behaves identically — same C++ flow, same argv
// handling, same kernel name, same enqueue convention. Different from
// sw/1d only in WHAT it does (sort vs SW) and the validation step.

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

#ifndef REPEAT
#define REPEAT 1
#endif

static void print_first_values(const char *label, const std::vector<int> &values) {
  const int n = std::min<int>(16, values.size());
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
  printf("repeat=%d\n", REPEAT);
  fflush(stdout);

  // Generate the input on the host once; every pod gets a copy.
  srand(42);
  std::vector<int> host_in(SIZE);
  std::vector<int> host_expected(SIZE);
  for (int i = 0; i < SIZE; i++) {
    host_in[i] = rand();
    host_expected[i] = host_in[i];
  }
  std::sort(host_expected.begin(), host_expected.end());
  print_first_values("host input", host_in);
  print_first_values("host expected", host_expected);

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

    // Allocate device buffers (every pod gets its own pair).
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, SIZE * sizeof(int), &d_a));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, SIZE * sizeof(int), &d_b));
    if (pod >= d_a_by_pod.size()) {
      d_a_by_pod.resize(pod + 1);
      d_b_by_pod.resize(pod + 1);
    }
    d_a_by_pod[pod] = d_a;
    d_b_by_pod[pod] = d_b;
    printf("Device buffers: pod %d A=%u B=%u\n",
           pod, (unsigned)d_a, (unsigned)d_b);

    // DMA host_in to device A buffer.
    printf("Transferring data: pod %d\n", pod);
    fflush(stdout);
    std::vector<hb_mc_dma_htod_t> htod_job;
    htod_job.push_back({d_a, host_in.data(), (uint32_t)(SIZE * sizeof(int))});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod_job.data(), htod_job.size()));

    // Cuda args. The kernel reads A, sorts in place, leaves result in A.
    hb_mc_dimension_t tg_dim   = {.x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = {.x = 1, .y = 1};
    #define CUDA_ARGC 3
    uint32_t cuda_argv[CUDA_ARGC] = {d_a, d_b, (uint32_t)SIZE};

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

  // Read back and validate per pod.
  std::vector<int> result(SIZE);
  bool fail = false;
  hb_mc_device_foreach_pod_id(&device, pod) {
    printf("Reading results: pod %d\n", pod);
    fflush(stdout);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));

    std::vector<hb_mc_dma_dtoh_t> dtoh_job;
    std::fill(result.begin(), result.end(), 0);
    dtoh_job.push_back({d_a_by_pod[pod], result.data(), (uint32_t)(SIZE * sizeof(int))});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&device, dtoh_job.data(), dtoh_job.size()));

    bool pod_fail = false;
    int first_mismatch = -1;
    for (int i = 0; i < SIZE; i++) {
      if (result[i] != host_expected[i]) {
        first_mismatch = i;
        pod_fail = true;
        break;
      }
    }
    if (pod_fail) {
      printf("Mismatch pod %d: i=%d, actual=%d, expected=%d\n",
             pod, first_mismatch, result[first_mismatch],
             host_expected[first_mismatch]);
      print_first_values("actual", result);
      print_first_values("expected", host_expected);
      fail = true;
    } else {
      printf("correct pod %d\n", pod);
    }
  }

  BSG_CUDA_CALL(hb_mc_device_finish(&device));
  return fail ? HB_MC_FAIL : HB_MC_SUCCESS;
}

declare_program_main("radix_sort_multipod", radix_sort_multipod);
