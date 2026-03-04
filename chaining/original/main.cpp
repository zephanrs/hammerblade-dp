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
#include <cmath>

#define ALLOC_NAME "default_allocator"

// Host main;
int sw_multipod(int argc, char ** argv) {
  int r = 0;
  
  // command line;
  const char *bin_path = argv[1];

  // parameters;
  int chain_len = CHAIN_LEN;
  int lookback = LOOKBACK;
  printf("chain_len=%d\n", chain_len);
  printf("lookback=%d\n", lookback);
  
  // prepare inputs;
  int* loc_query = (int*) malloc(chain_len*sizeof(int));
  int* loc_ref = (int*) malloc(chain_len*sizeof(int));
  
  // randomly generate query and ref
  int current_ref = 0;
  for (int i = 0; i < chain_len; i++) {
      current_ref += (rand() % 10) + 1;
      loc_ref[i] = current_ref;
      loc_query[i] = (rand() % (current_ref + chain_len));
  }

  // initialize device; 
  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "sw_multipod", HB_MC_DEVICE_ID));

  eva_t d_loc_query;
  eva_t d_loc_ref;
  eva_t d_scores;

  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod)
  {
    printf("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    // Allocate memory on device;
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, chain_len*sizeof(int), &d_loc_query));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, chain_len*sizeof(int), &d_loc_ref));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, chain_len*sizeof(int), &d_scores));
   
    // DMA transfer;
    printf("Transferring data: pod %d\n", pod);
    std::vector<hb_mc_dma_htod_t> htod_job;
    htod_job.push_back({d_loc_query, loc_query, chain_len*sizeof(int)});
    htod_job.push_back({d_loc_ref, loc_ref, chain_len*sizeof(int)});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod_job.data(), htod_job.size()));

    // Cuda args;
    hb_mc_dimension_t tg_dim = { .x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = { .x = 1, .y = 1};
    #define CUDA_ARGC 4
    uint32_t cuda_argv[CUDA_ARGC] = {d_loc_query, d_loc_ref, d_scores, pod};

    // Enqueue kernel;
    printf("Enqueue Kernel: pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_kernel_enqueue (&device, grid_dim, tg_dim, "kernel", CUDA_ARGC, cuda_argv));
  }

  // Launch pod;
  printf("Launching all pods\n");
  hb_mc_manycore_trace_enable((&device)->mc);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  hb_mc_manycore_trace_disable((&device)->mc);

  // Read from device;
  int* actual_scores = (int*) malloc(chain_len*sizeof(int));

  bool fail = false;
  hb_mc_device_foreach_pod_id(&device, pod) {
    printf("Reading results: pods %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));

    // clear buf;
    for (int i = 0; i < chain_len; i++) {
      actual_scores[i] = 0;
    }

    // DMA transfer; device -> host;
    std::vector<hb_mc_dma_dtoh_t> dtoh_job;
    dtoh_job.push_back({d_scores, actual_scores, chain_len*sizeof(int)});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&device, dtoh_job.data(), dtoh_job.size()));

    // cpu reference;
    int* cpu_scores = (int*) malloc(chain_len * sizeof(int));
    
    // constants;
    const int k = 15;
    const float beta_mul = 0.01f;

    // cpu reference loop;
    for (int i = 0; i < chain_len; i++) {
        int max_sc = k; // default score if no predecessors
        
        // search window: [max(0, i - lookback), i - 1]
        int start_j = (i - lookback > 0) ? (i - lookback) : 0;
        
        for (int j = start_j; j < i; j++) {
            int dx = loc_ref[i] - loc_ref[j];
            int dy = loc_query[i] - loc_query[j];
            
            // validity check: 
            // - y_j > y_i (dy < 0) implies invalid (Beta = infinity)
            // - dx should be > 0 (strictly increasing x for valid chain step, or >=0)
            if (dx <= 0 || dy <= 0) continue; 
            
            // alpha = min(dy, dx, k)
            int alpha = dx < dy ? dx : dy;
            if (k < alpha) alpha = k;
            
            // beta = 0.01 * k * |dy - dx|
            int l = std::abs(dy - dx);
            int beta = (int)(beta_mul * k * l);
            
            int score = cpu_scores[j] + alpha - beta;
            if (score > max_sc) {
                max_sc = score;
            }
        }
        cpu_scores[i] = max_sc;
    }

    // validate;
    for (int i = 0; i < chain_len; i++) {
      int actual = actual_scores[i];
      int expected = cpu_scores[i];
      if (actual != expected) {
        fail = true;
        printf("Mismatch: i=%d, actual=%d, expected=%d\n", i, actual, expected);
      }
    }
    
    free(cpu_scores);
  }

  // Finish;
  BSG_CUDA_CALL(hb_mc_device_finish(&device));
  if (fail) {
    return HB_MC_FAIL;
  } else {
    return HB_MC_SUCCESS;
  }
}

declare_program_main("sw_multipod", sw_multipod);

