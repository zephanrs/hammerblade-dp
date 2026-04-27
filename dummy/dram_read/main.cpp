#include <bsg_manycore_errno.h>
#include <bsg_manycore_cuda.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <time.h>
#include <vector>
#include <bsg_manycore_regression.h>
#include <bsg_manycore.h>
#include "../../common/host_bench.hpp"

#define ALLOC_NAME "default_allocator"

#ifndef N_ELEMS
#define N_ELEMS 65536  // 256 KB
#endif

#ifndef REPEAT
#define REPEAT 1
#endif

int dram_read_multipod(int argc, char** argv) {
  const char* bin_path = argv[1];

  const int n_elems = N_ELEMS;
  const int repeat  = REPEAT;
  const double dram_bytes = (double)n_elems * repeat * 4.0;  // reads only

  printf("n_elems=%d\n", n_elems);
  printf("array_kb=%d\n", (n_elems * 4) / 1024);
  printf("repeat=%d\n", repeat);
  printf("total_dram_bytes=%.0f\n", dram_bytes);

  int* h_input = (int*)malloc(n_elems * sizeof(int));
  for (int i = 0; i < n_elems; i++) h_input[i] = i;

  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "dram_read_multipod", HB_MC_DEVICE_ID));

  eva_t d_input;

  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod) {
    printf("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    BSG_CUDA_CALL(hb_mc_device_malloc(&device, n_elems * sizeof(int), &d_input));
    std::vector<hb_mc_dma_htod_t> htod;
    htod.push_back({d_input, h_input, (uint32_t)(n_elems * sizeof(int))});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod.data(), htod.size()));

    hb_mc_dimension_t tg_dim   = {.x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = {.x = 1, .y = 1};
    #define CUDA_ARGC 2
    uint32_t cuda_argv[CUDA_ARGC] = {d_input, (uint32_t)pod};
    BSG_CUDA_CALL(hb_mc_kernel_enqueue(&device, grid_dim, tg_dim, "kernel",
                                       CUDA_ARGC, cuda_argv));
  }

  printf("Launching all pods\n");
  fflush(stdout);
  timespec t0 = {}, t1 = {};
  hb_mc_manycore_trace_enable((&device)->mc);
  clock_gettime(CLOCK_MONOTONIC, &t0);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  clock_gettime(CLOCK_MONOTONIC, &t1);
  hb_mc_manycore_trace_disable((&device)->mc);
  print_kernel_launch_time(t0, t1);

  const double elapsed = elapsed_seconds(t0, t1);
  printf("achieved_bw_GB_s=%.4f\n", dram_bytes / elapsed / 1e9);

  BSG_CUDA_CALL(hb_mc_device_finish(&device));
  free(h_input);
  return HB_MC_SUCCESS;
}

declare_program_main("dram_read_multipod", dram_read_multipod);
