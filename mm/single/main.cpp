#include <bsg_manycore_errno.h>
#include <bsg_manycore_cuda.h>
#include <cstdlib>
#include <cstdio>
#include <bsg_manycore_regression.h>
#include <bsg_manycore.h>
#include "elem.hpp"
#include <cstdint>
#include <vector>

#define ALLOC_NAME "default_allocator"

// A and B hold small integers, in both the i32 and f32 builds. Every product
// and partial sum is then an integer bounded by MAT_K * VALUE_RANGE^2: far
// inside int32, and far below 2^24 so single precision also represents the
// whole computation exactly. The comparison below is therefore exact for both
// dtypes, and for f32 it stays exact regardless of whether the compiler
// contracted a*b+c into fmadd.s or of the order the kernel accumulates in.
// Switch to fractional operands and a relative tolerance when the point is to
// exercise floating-point rounding rather than to validate the mapping.
#define VALUE_RANGE 8

static void fill_random(elem_t* buf, int count) {
  for (int i = 0; i < count; i++) {
    buf[i] = (elem_t)((rand() % (2 * VALUE_RANGE)) - VALUE_RANGE);
  }
}

// Host main;
int mm_single(int argc, char ** argv) {

  // command line;
  const char *bin_path = argv[1];

  // parameters;
  int m = MAT_M;
  int n = MAT_N;
  int k = MAT_K;
  bsg_pr_test_info("m=%d\n", m);
  bsg_pr_test_info("n=%d\n", n);
  bsg_pr_test_info("k=%d\n", k);
  bsg_pr_test_info("dtype=%s\n", ELEM_NAME);

  // prepare inputs;
  elem_t* A = (elem_t*) malloc(m * k * sizeof(elem_t));
  elem_t* B = (elem_t*) malloc(k * n * sizeof(elem_t));
  srand(1);
  fill_random(A, m * k);
  fill_random(B, k * n);

  // cpu reference; deliberately i-j-k, so it does not share a loop order with
  // the kernel and an index mistake cannot cancel out.
  elem_t* expected = (elem_t*) malloc(m * n * sizeof(elem_t));
  for (int i = 0; i < m; i++) {
    for (int j = 0; j < n; j++) {
      elem_t acc = (elem_t)0;
      for (int p = 0; p < k; p++) {
        acc += A[(i * k) + p] * B[(p * n) + j];
      }
      expected[(i * n) + j] = acc;
    }
  }

  // initialize device;
  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "mm_single", HB_MC_DEVICE_ID));

  eva_t d_A;
  eva_t d_B;
  eva_t d_C;

  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod)
  {
    bsg_pr_test_info("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    // Allocate memory on device;
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, m * k * sizeof(elem_t), &d_A));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, k * n * sizeof(elem_t), &d_B));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, m * n * sizeof(elem_t), &d_C));

    // DMA transfer;
    bsg_pr_test_info("Transferring data: pod %d\n", pod);
    std::vector<hb_mc_dma_htod_t> htod_job;
    htod_job.push_back({d_A, A, m * k * sizeof(elem_t)});
    htod_job.push_back({d_B, B, k * n * sizeof(elem_t)});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod_job.data(), htod_job.size()));

    // Cuda args;
    hb_mc_dimension_t tg_dim = { .x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = { .x = 1, .y = 1};
    #define CUDA_ARGC 4
    uint32_t cuda_argv[CUDA_ARGC] = {d_A, d_B, d_C, pod};

    // Enqueue kernel;
    bsg_pr_test_info("Enqueue Kernel: pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_kernel_enqueue (&device, grid_dim, tg_dim, "kernel", CUDA_ARGC, cuda_argv));
  }

  // Launch pod;
  bsg_pr_test_info("Launching all pods\n");
  hb_mc_manycore_trace_enable((&device)->mc);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  hb_mc_manycore_trace_disable((&device)->mc);

  // Read from device;
  elem_t* actual = (elem_t*) malloc(m * n * sizeof(elem_t));

  bool fail = false;
  hb_mc_device_foreach_pod_id(&device, pod) {
    bsg_pr_test_info("Reading results: pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));

    // clear buf;
    for (int i = 0; i < m * n; i++) {
      actual[i] = (elem_t)0;
    }

    // DMA transfer; device -> host;
    std::vector<hb_mc_dma_dtoh_t> dtoh_job;
    dtoh_job.push_back({d_C, actual, m * n * sizeof(elem_t)});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&device, dtoh_job.data(), dtoh_job.size()));

    // validate;
    int mismatches = 0;
    for (int i = 0; i < m * n; i++) {
      if (actual[i] != expected[i]) {
        fail = true;
        // only print the first few so a broken run stays readable;
        if (mismatches < 8) {
          bsg_pr_test_info("Mismatch: row=%d, col=%d, actual=" ELEM_FMT
                           ", expected=" ELEM_FMT "\n",
                           i / n, i % n, actual[i], expected[i]);
        }
        mismatches++;
      }
    }
    if (mismatches > 0) {
      bsg_pr_test_info("pod %d: %d/%d elements wrong\n", pod, mismatches, m * n);
    }
  }

  // Finish;
  BSG_CUDA_CALL(hb_mc_device_finish(&device));
  if (fail) {
    return HB_MC_FAIL;
  } else {
    return HB_MC_SUCCESS;
  }
}


declare_program_main("mm_single", mm_single);
