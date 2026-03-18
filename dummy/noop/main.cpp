#include <bsg_manycore_errno.h>
#include <bsg_manycore_cuda.h>
#include <cstdint>
#include <cstdio>
#include <time.h>
#include <bsg_manycore_regression.h>
#include <bsg_manycore.h>

#include "../../common/host_bench.hpp"

#define ALLOC_NAME "default_allocator"

int dummy_overhead(int argc, char** argv) {
  const char* bin_path = argv[1];
  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "dummy_overhead", HB_MC_DEVICE_ID));

  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod) {
    std::printf("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    hb_mc_dimension_t tg_dim = {.x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = {.x = 1, .y = 1};
    #define CUDA_ARGC 1
    uint32_t cuda_argv[CUDA_ARGC] = {static_cast<uint32_t>(pod)};

    std::printf("Enqueue Kernel: pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_kernel_enqueue(&device, grid_dim, tg_dim, "kernel", CUDA_ARGC, cuda_argv));
  }

  std::printf("Launching all pods\n");
  timespec kernel_start = {};
  timespec kernel_end = {};
  hb_mc_manycore_trace_enable((&device)->mc);
  clock_gettime(CLOCK_MONOTONIC, &kernel_start);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  clock_gettime(CLOCK_MONOTONIC, &kernel_end);
  hb_mc_manycore_trace_disable((&device)->mc);
  print_kernel_launch_time(kernel_start, kernel_end);

  BSG_CUDA_CALL(hb_mc_device_finish(&device));
  return HB_MC_SUCCESS;
}

declare_program_main("dummy_overhead", dummy_overhead);
