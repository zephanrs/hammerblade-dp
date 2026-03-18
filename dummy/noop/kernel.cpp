#include <bsg_manycore.h>
#include <bsg_cuda_lite_barrier.h>

extern "C" int kernel(int pod_id)
{
  (void)pod_id;

  bsg_barrier_tile_group_init();
  bsg_barrier_tile_group_sync();
  bsg_cuda_print_stat_kernel_start();
  bsg_fence();
  bsg_barrier_tile_group_sync();
  bsg_fence();
  bsg_cuda_print_stat_kernel_end();
  return 0;
}
