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
#include <map>
#include "../../common/host_bench.hpp"
#include "../common/test_input.hpp"

#define ALLOC_NAME "default_allocator"

#ifndef ACTIVE_COMPUTE_GROUPS
#define ACTIVE_COMPUTE_GROUPS bsg_tiles_X
#endif

#ifndef CORES_PER_GROUP
#define CORES_PER_GROUP bsg_tiles_Y
#endif

// When POD_UNIQUE_DATA is defined, each pod receives a different slice of the
// input (NUM_POD_X slices of num_seq sequences each).  Otherwise all pods get
// identical data (tests repeated DRAM accesses to the same addresses).
#ifndef POD_UNIQUE_DATA
#define POD_UNIQUE_DATA 0
#endif

// Host main;
int sw_multipod(int argc, char ** argv) {
  int r = 0;
  
  // command line;
  const char *bin_path = argv[1];
  const char *query_path = argv[2];
  const char *ref_path = argv[3];

  // parameters;
  const int num_seq = NUM_SEQ;
  const int total_num_seq = total_output_count(num_seq);
  const int seq_len = SEQ_LEN;
  const int num_pods = NUM_POD_X;
  const int cores_per_group = CORES_PER_GROUP;
  const int num_groups = (bsg_tiles_X * bsg_tiles_Y) / cores_per_group;
  printf("num_seq=%d\n", num_seq);
  printf("total_num_seq=%d\n", total_num_seq);
  printf("repeat_factor=%d\n", kInputRepeatFactor);
  printf("cores_per_group=%d\n", cores_per_group);
  printf("num_groups=%d\n", num_groups);
  printf("pod_unique_data=%d\n", POD_UNIQUE_DATA);
  printf("max_seq_len=%d\n", seq_len);
  printf("min_seq_len=%d\n", VAR_LEN_MIN);

  // For POD_UNIQUE_DATA, each pod gets a different slice of num_seq sequences.
  // For shared data, all pods get identical data (tests same-address DRAM patterns).
  const int host_seq = POD_UNIQUE_DATA ? num_pods * num_seq : num_seq;
  uint8_t* query = (uint8_t*) malloc(host_seq*seq_len*sizeof(uint8_t));
  uint8_t* ref   = (uint8_t*) malloc(host_seq*seq_len*sizeof(uint8_t));
  int* qry_lens  = (int*) malloc(host_seq*sizeof(int));
  int* ref_lens  = (int*) malloc(host_seq*sizeof(int));
  prepare_sw_inputs(query_path, ref_path, query, ref, qry_lens, ref_lens, host_seq, seq_len);
  sort_sw_inputs_by_length(query, ref, qry_lens, ref_lens, host_seq, seq_len);
 
  // initialize device; 
  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "sw_multipod", HB_MC_DEVICE_ID));

  eva_t d_query;
  eva_t d_ref;
  eva_t d_output;

  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod)
  {
    printf("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    // Allocate memory on device;
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*(seq_len+1)*sizeof(uint8_t), &d_query));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*(seq_len+1)*sizeof(uint8_t), &d_ref));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, total_num_seq*sizeof(int), &d_output));
   
    // DMA transfer; with POD_UNIQUE_DATA each pod gets its own slice.
    printf("Transferring data: pod %d\n", pod);
    const int pod_seq_offset = POD_UNIQUE_DATA ? pod * num_seq : 0;
    uint8_t* pod_query = query + pod_seq_offset * seq_len;
    uint8_t* pod_ref   = ref   + pod_seq_offset * seq_len;
    std::vector<hb_mc_dma_htod_t> htod_job;
    htod_job.push_back({d_query, pod_query, (uint32_t)(num_seq*seq_len*sizeof(uint8_t))});
    htod_job.push_back({d_ref,   pod_ref,   (uint32_t)(num_seq*seq_len*sizeof(uint8_t))});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod_job.data(), htod_job.size()));

    // Cuda args;
    hb_mc_dimension_t tg_dim = { .x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = { .x = 1, .y = 1};
    #define CUDA_ARGC 4
    uint32_t cuda_argv[CUDA_ARGC] = {d_query, d_ref, d_output, pod};


    // Enqueue kernel;
    printf("Enqueue Kernel: pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_kernel_enqueue (&device, grid_dim, tg_dim, "kernel", CUDA_ARGC, cuda_argv));
  }


  // Launch pod;
  printf("Launching all pods\n");
  timespec kernel_start = {};
  timespec kernel_end = {};
  hb_mc_manycore_trace_enable((&device)->mc);
  clock_gettime(CLOCK_MONOTONIC, &kernel_start);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  clock_gettime(CLOCK_MONOTONIC, &kernel_end);
  hb_mc_manycore_trace_disable((&device)->mc);
  print_kernel_launch_time(kernel_start, kernel_end);


  // Read from device;
  int* actual_output = (int*) malloc(total_num_seq*sizeof(int));

  bool fail = false;
  hb_mc_device_foreach_pod_id(&device, pod) {
    printf("Reading results: pods %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));

    // clear buf;
    for (int i = 0; i < total_num_seq; i++) {
      actual_output[i] = 0;
    }

    // DMA transfer; device -> host;
    std::vector<hb_mc_dma_dtoh_t> dtoh_job;
    dtoh_job.push_back({d_output, actual_output, total_num_seq*sizeof(int)});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&device, dtoh_job.data(), dtoh_job.size()));

    const int pod_seq_off = POD_UNIQUE_DATA ? pod * num_seq : 0;
    const int expected = sw_reference_score(
        &query[pod_seq_off * seq_len], qry_lens[pod_seq_off],
        &ref[pod_seq_off * seq_len],   ref_lens[pod_seq_off]);
    const int actual = actual_output[0];
    if (actual != expected) {
      fail = true;
      printf("Mismatch pod %d: i=0, actual=%d, expected=%d\n", pod, actual, expected);
    } else {
      printf("correct pod %d: first sequence score=%d\n", pod, expected);
    }
  }


  // Finish;
  BSG_CUDA_CALL(hb_mc_device_finish(&device));
  free(qry_lens);
  free(ref_lens);
  if (fail) {
    return HB_MC_FAIL;
  } else {
    return HB_MC_SUCCESS;
  }
}


declare_program_main("sw_multipod", sw_multipod);
