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
#define N_ELEMS 1048576
#endif

#ifndef REPEAT
#define REPEAT 1
#endif

int vvadd_multipod(int argc, char** argv) {
  const char* bin_path = argv[1];

  const int n_elems = N_ELEMS;
  const int repeat  = REPEAT;
  // 2 reads + 1 write per element = 12 bytes DRAM traffic.
  const double dram_bytes = (double)n_elems * repeat * 12.0;

  printf("n_elems=%d\n", n_elems);
  printf("array_kb=%d\n", (n_elems * 4) / 1024);
  printf("repeat=%d\n", repeat);
  printf("total_dram_bytes=%.0f\n", dram_bytes);

  int* h_A = (int*)malloc(n_elems * sizeof(int));
  int* h_B = (int*)malloc(n_elems * sizeof(int));
  for (int i = 0; i < n_elems; i++) { h_A[i] = i; h_B[i] = -i; }

  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "vvadd_multipod", HB_MC_DEVICE_ID));

  eva_t d_A, d_B, d_C;

  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod) {
    printf("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    BSG_CUDA_CALL(hb_mc_device_malloc(&device, n_elems * sizeof(int), &d_A));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, n_elems * sizeof(int), &d_B));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, n_elems * sizeof(int), &d_C));

    std::vector<hb_mc_dma_htod_t> htod;
    htod.push_back({d_A, h_A, (uint32_t)(n_elems * sizeof(int))});
    htod.push_back({d_B, h_B, (uint32_t)(n_elems * sizeof(int))});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod.data(), htod.size()));

    hb_mc_dimension_t tg_dim   = {.x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = {.x = 1, .y = 1};
    #define CUDA_ARGC 4
    uint32_t cuda_argv[CUDA_ARGC] = {d_A, d_B, d_C, (uint32_t)pod};
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
  free(h_A);
  free(h_B);
  return HB_MC_SUCCESS;
}

declare_program_main("vvadd_multipod", vvadd_multipod);
