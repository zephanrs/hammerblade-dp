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

#ifndef OPS_PER_ELEM
#define OPS_PER_ELEM 1
#endif

#ifndef N_ELEMS
#define N_ELEMS 1048576
#endif

#ifndef REPEAT
#define REPEAT 1
#endif

int roofline_multipod(int argc, char** argv) {
  const char* bin_path = argv[1];

  const int n_elems      = N_ELEMS;
  const int ops_per_elem = OPS_PER_ELEM;
  const int repeat       = REPEAT;

  // Arithmetic intensity and expected traffic (printed for the run script to parse).
  // OI = 2*ops_per_elem / 8  ops/byte  (mul + add per iter; 4B read + 4B write per elem)
  const double oi         = 2.0 * ops_per_elem / 8.0;
  const double dram_bytes = (double)n_elems * repeat * 8.0;  // read + write per elem
  const double total_ops  = (double)n_elems * repeat * 2.0 * ops_per_elem;

  printf("n_elems=%d\n", n_elems);
  printf("ops_per_elem=%d\n", ops_per_elem);
  printf("repeat=%d\n", repeat);
  printf("arith_intensity_ops_per_byte=%.4f\n", oi);
  printf("total_dram_bytes=%.0f\n", dram_bytes);
  printf("total_ops=%.0f\n", total_ops);

  // Allocate host buffers.
  int* h_input  = (int*)malloc(n_elems * sizeof(int));
  int* h_output = (int*)malloc(n_elems * sizeof(int));
  // Distinct, sequential data so the systolic kernel's value-change
  // wakeup never sees two batches with the same slot[BATCH-1] value.
  for (int i = 0; i < n_elems; i++) h_input[i] = i;

  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "roofline_multipod", HB_MC_DEVICE_ID));

  eva_t d_input, d_output;

  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod) {
    printf("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    BSG_CUDA_CALL(hb_mc_device_malloc(&device, n_elems * sizeof(int), &d_input));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, n_elems * sizeof(int), &d_output));

    std::vector<hb_mc_dma_htod_t> htod;
    htod.push_back({d_input, h_input, (uint32_t)(n_elems * sizeof(int))});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod.data(), htod.size()));

    hb_mc_dimension_t tg_dim   = {.x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = {.x = 1, .y = 1};
    #define CUDA_ARGC 3
    uint32_t cuda_argv[CUDA_ARGC] = {d_input, d_output, (uint32_t)pod};
    BSG_CUDA_CALL(hb_mc_kernel_enqueue(&device, grid_dim, tg_dim, "kernel", CUDA_ARGC, cuda_argv));
  }

  printf("Launching all pods\n");
  timespec t0 = {}, t1 = {};
  hb_mc_manycore_trace_enable((&device)->mc);
  clock_gettime(CLOCK_MONOTONIC, &t0);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  clock_gettime(CLOCK_MONOTONIC, &t1);
  hb_mc_manycore_trace_disable((&device)->mc);
  print_kernel_launch_time(t0, t1);

  const double elapsed = elapsed_seconds(t0, t1);
  printf("achieved_bw_GB_s=%.4f\n",  dram_bytes / elapsed / 1e9);
  printf("achieved_gops_s=%.4f\n",   total_ops  / elapsed / 1e9);

  BSG_CUDA_CALL(hb_mc_device_finish(&device));
  free(h_input);
  free(h_output);
  return HB_MC_SUCCESS;
}

declare_program_main("roofline_multipod", roofline_multipod);
