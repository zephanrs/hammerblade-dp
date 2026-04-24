#include <bsg_manycore_cuda.h>
#include <bsg_manycore_errno.h>
#include <bsg_manycore_loader.h>
#include <bsg_manycore_regression.h>
#include <bsg_manycore_tile.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define ALLOC_NAME "default_allocator"

#ifndef REPEAT
#define REPEAT 1
#endif

static double elapsed_seconds(struct timespec start, struct timespec end) {
  return (double)(end.tv_sec - start.tv_sec) +
         (double)(end.tv_nsec - start.tv_nsec) / 1e9;
}

static int cmp_int(const void *a, const void *b) {
  return (*(const int *)a - *(const int *)b);
}

/* Maximum pods the real cluster has. */
#define MAX_PODS 16

int kernel_radix_sort(int argc, char **argv) {
  char *bin_path, *test_name;
  struct arguments_path args = {NULL, NULL};
  argp_parse(&argp_path, argc, argv, 0, 0, &args);
  bin_path  = args.path;
  test_name = args.name;

  bsg_pr_test_info("Running kernel_radix_sort. SIZE=%d REPEAT=%d\n",
                   SIZE, REPEAT);
  srand(42);

  /* Prepare host input and expected output. */
  int *A_host = (int *)malloc(sizeof(int) * SIZE);
  int *A_expected = (int *)malloc(sizeof(int) * SIZE);
  for (int i = 0; i < SIZE; i++) {
    A_host[i] = rand();
    A_expected[i] = A_host[i];
  }
  qsort(A_expected, SIZE, sizeof(int), cmp_int);

  /* Initialize device. */
  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, test_name, HB_MC_DEVICE_ID));

  /* Per-pod device addresses so we can read back results. */
  eva_t A_device[MAX_PODS];
  eva_t B_device[MAX_PODS];
  memset(A_device, 0, sizeof(A_device));
  memset(B_device, 0, sizeof(B_device));

  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod) {
    bsg_pr_info("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    BSG_CUDA_CALL(hb_mc_device_malloc(&device, SIZE * sizeof(int), &A_device[pod]));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, SIZE * sizeof(int), &B_device[pod]));

    bsg_pr_info("pod %d: A=0x%x B=0x%x\n", pod, A_device[pod], B_device[pod]);

    hb_mc_dma_htod_t htod = {
      .d_addr = A_device[pod],
      .h_addr = (void *)A_host,
      .size   = (uint32_t)(SIZE * sizeof(int))
    };
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, &htod, 1));

    hb_mc_dimension_t tg_dim   = {.x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = {.x = 1, .y = 1};
    uint32_t cuda_argv[3] = {
      (uint32_t)A_device[pod],
      (uint32_t)B_device[pod],
      (uint32_t)SIZE
    };
    BSG_CUDA_CALL(hb_mc_kernel_enqueue(&device, grid_dim, tg_dim,
                                        "kernel_radix_sort", 3, cuda_argv));
  }

  /* Launch all pods simultaneously. */
  bsg_pr_info("Launching all pods\n");
  struct timespec t_start = {0, 0}, t_end = {0, 0};
  hb_mc_manycore_trace_enable((&device)->mc);
  clock_gettime(CLOCK_MONOTONIC, &t_start);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  clock_gettime(CLOCK_MONOTONIC, &t_end);
  hb_mc_manycore_trace_disable((&device)->mc);
  printf("kernel_launch_time_sec=%.9f\n", elapsed_seconds(t_start, t_end));

  /* Validate.  32-bit radix sort runs 8 passes (8 swaps).  After an even
   * number of swaps the sorted data is back in the original A buffer. */
  int *A_result = (int *)malloc(sizeof(int) * SIZE);
  bool fail = false;

  hb_mc_device_foreach_pod_id(&device, pod) {
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));

    hb_mc_dma_dtoh_t dtoh = {
      .d_addr = A_device[pod],
      .h_addr = (void *)A_result,
      .size   = (uint32_t)(SIZE * sizeof(int))
    };
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&device, &dtoh, 1));

    for (int i = 0; i < SIZE; i++) {
      if (A_result[i] != A_expected[i]) {
        printf("pod %d FAIL [%d]: expected=%d actual=%d\n",
               pod, i, A_expected[i], A_result[i]);
        fail = true;
        break;
      }
    }
    if (!fail) {
      printf("pod %d: sort PASSED\n", pod);
    }
  }

  BSG_CUDA_CALL(hb_mc_device_finish(&device));
  free(A_host);
  free(A_expected);
  free(A_result);
  return fail ? HB_MC_FAIL : HB_MC_SUCCESS;
}

declare_program_main("radix_sort", kernel_radix_sort);
