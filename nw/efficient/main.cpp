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
#include "../../sw/common/test_input.hpp"

#define ALLOC_NAME "default_allocator"

static int compute_segment_score(const uint8_t* query, int query_len,
                                 const uint8_t* ref, int ref_len) {
  std::vector<int> prev(ref_len + 1);
  std::vector<int> curr(ref_len + 1);

  for (int r = 0; r <= ref_len; r++) {
    prev[r] = -r;
  }

  for (int q = 1; q <= query_len; q++) {
    curr[0] = -q;
    for (int r = 1; r <= ref_len; r++) {
      int match = (query[q - 1] == ref[r - 1]) ? 1 : -1;
      int score_diag = prev[r - 1] + match;
      int score_up = prev[r] - 1;
      int score_left = curr[r - 1] - 1;
      curr[r] = std::max(score_diag, std::max(score_up, score_left));
    }
    prev.swap(curr);
  }

  return prev[ref_len];
}

static inline int table_index(int stride, int query_idx, int ref_idx) {
  return query_idx * stride + ref_idx;
}

static void fill_forward_scores(const uint8_t* query,
                                const uint8_t* ref,
                                int seq_len,
                                std::vector<int>* scores) {
  const int stride = seq_len + 1;
  scores->assign(stride * stride, 0);

  for (int query_idx = 0; query_idx <= seq_len; query_idx++) {
    (*scores)[table_index(stride, query_idx, 0)] = -query_idx;
  }
  for (int ref_idx = 0; ref_idx <= seq_len; ref_idx++) {
    (*scores)[table_index(stride, 0, ref_idx)] = -ref_idx;
  }

  for (int query_idx = 1; query_idx <= seq_len; query_idx++) {
    for (int ref_idx = 1; ref_idx <= seq_len; ref_idx++) {
      const int match = (query[query_idx - 1] == ref[ref_idx - 1]) ? 1 : -1;
      const int score_diag =
        (*scores)[table_index(stride, query_idx - 1, ref_idx - 1)] + match;
      const int score_up =
        (*scores)[table_index(stride, query_idx - 1, ref_idx)] - 1;
      const int score_left =
        (*scores)[table_index(stride, query_idx, ref_idx - 1)] - 1;

      int best = score_diag;
      if (score_up > best) {
        best = score_up;
      }
      if (score_left > best) {
        best = score_left;
      }

      (*scores)[table_index(stride, query_idx, ref_idx)] = best;
    }
  }
}

static void fill_suffix_scores(const uint8_t* query,
                               const uint8_t* ref,
                               int seq_len,
                               std::vector<int>* scores) {
  const int stride = seq_len + 1;
  scores->assign(stride * stride, 0);

  for (int query_idx = seq_len; query_idx >= 0; query_idx--) {
    (*scores)[table_index(stride, query_idx, seq_len)] = -(seq_len - query_idx);
  }
  for (int ref_idx = seq_len; ref_idx >= 0; ref_idx--) {
    (*scores)[table_index(stride, seq_len, ref_idx)] = -(seq_len - ref_idx);
  }

  for (int query_idx = seq_len - 1; query_idx >= 0; query_idx--) {
    for (int ref_idx = seq_len - 1; ref_idx >= 0; ref_idx--) {
      const int match = (query[query_idx] == ref[ref_idx]) ? 1 : -1;
      const int score_diag =
        (*scores)[table_index(stride, query_idx + 1, ref_idx + 1)] + match;
      const int score_up =
        (*scores)[table_index(stride, query_idx + 1, ref_idx)] - 1;
      const int score_left =
        (*scores)[table_index(stride, query_idx, ref_idx + 1)] - 1;

      int best = score_diag;
      if (score_up > best) {
        best = score_up;
      }
      if (score_left > best) {
        best = score_left;
      }

      (*scores)[table_index(stride, query_idx, ref_idx)] = best;
    }
  }
}

struct path_check_t {
  bool valid;
  int bad_ref;
  int bad_query;
  int point_count;
  int score;
  const char* error;
};

static path_check_t validate_exported_path(const uint8_t* query,
                                           const uint8_t* ref,
                                           int seq_len,
                                           const int* path,
                                           const std::vector<int>& forward_scores,
                                           const std::vector<int>& suffix_scores) {
  path_check_t result = {true, -1, -1, 0, 0, ""};
  const int stride = seq_len + 1;
  const int optimal_score = forward_scores[table_index(stride, seq_len, seq_len)];
  int prev_query = 0;
  int prev_ref = 0;

  for (int ref_idx = 0; ref_idx < seq_len; ref_idx++) {
    int query_idx = path[ref_idx];
    if (query_idx == -1) {
      continue;
    }

    if (query_idx < 0 || query_idx > seq_len || query_idx < prev_query) {
      result.valid = false;
      result.bad_ref = ref_idx;
      result.bad_query = query_idx;
      result.error = "checkpoint is out of range or not monotonic";
      return result;
    }

    if (forward_scores[table_index(stride, query_idx, ref_idx)] +
          suffix_scores[table_index(stride, query_idx, ref_idx)] != optimal_score) {
      result.valid = false;
      result.bad_ref = ref_idx;
      result.bad_query = query_idx;
      result.error = "checkpoint is not on an optimal full-matrix path";
      return result;
    }

    result.score += compute_segment_score(
      query + prev_query,
      query_idx - prev_query,
      ref + prev_ref,
      ref_idx - prev_ref
    );
    if (result.score != forward_scores[table_index(stride, query_idx, ref_idx)]) {
      result.valid = false;
      result.bad_ref = ref_idx;
      result.bad_query = query_idx;
      result.error = "checkpoint chain does not trace through the dp matrix";
      return result;
    }

    result.point_count++;
    prev_query = query_idx;
    prev_ref = ref_idx;
  }

  result.score += compute_segment_score(
      query + prev_query,
      seq_len - prev_query,
      ref + prev_ref,
      seq_len - prev_ref
  );
  if (result.score != optimal_score) {
    result.valid = false;
    result.bad_ref = seq_len;
    result.bad_query = seq_len;
    result.error = "traced alignment score is not globally optimal";
  }

  return result;
}

// Host main;
int nw_baseline_multipod(int argc, char ** argv) {
  int r = 0;
  
  // command line;
  const char *bin_path = argv[1];
  const char *query_path = argv[2];
  const char *ref_path = argv[3];

  // parameters;
  const int num_seq = NUM_SEQ;
  const int total_num_seq = total_output_count(num_seq);
  const int seq_len = SEQ_LEN;
  printf("num_seq=%d\n", num_seq);
  printf("total_num_seq=%d\n", total_num_seq);
  printf("repeat_factor=%d\n", kInputRepeatFactor);
  printf("seq_len=%d\n", seq_len);
  
  // prepare inputs;
  uint8_t* query = (uint8_t*) malloc(num_seq*seq_len*sizeof(uint8_t));
  uint8_t* ref = (uint8_t*) malloc(num_seq*seq_len*sizeof(uint8_t));
  prepare_fixed_length_inputs(query_path, ref_path, query, ref, num_seq, seq_len);
 
  // initialize device; 
  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "nw_baseline_multipod", HB_MC_DEVICE_ID));

  eva_t d_query;
  eva_t d_ref;
  eva_t d_output;
  eva_t d_path;

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
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, total_num_seq*seq_len*sizeof(int), &d_path));
   
    // DMA transfer;
    printf("Transferring data: pod %d\n", pod);
    std::vector<hb_mc_dma_htod_t> htod_job;
    htod_job.push_back({d_query, query, num_seq*seq_len*sizeof(uint8_t)});
    htod_job.push_back({d_ref, ref, num_seq*seq_len*sizeof(uint8_t)});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod_job.data(), htod_job.size()));

    // Cuda args;
    hb_mc_dimension_t tg_dim = { .x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = { .x = 1, .y = 1};
    #define CUDA_ARGC 5
    uint32_t cuda_argv[CUDA_ARGC] = {d_query, d_ref, d_output, d_path, pod};


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
  int* actual_path = (int*) malloc(total_num_seq*seq_len*sizeof(int));

  bool fail = false;
  hb_mc_device_foreach_pod_id(&device, pod) {
    printf("Reading results: pods %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));

    // clear buf;
    for (int i = 0; i < total_num_seq; i++) {
      actual_output[i] = 0;
    }
    for (int i = 0; i < total_num_seq * seq_len; i++) {
      actual_path[i] = 0;
    }

    // DMA transfer; device -> host;
    std::vector<hb_mc_dma_dtoh_t> dtoh_job;
    dtoh_job.push_back({d_output, actual_output, total_num_seq*sizeof(int)});
    dtoh_job.push_back({d_path, actual_path, total_num_seq*seq_len*sizeof(int)});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&device, dtoh_job.data(), dtoh_job.size()));

    std::vector<int> forward_scores;
    std::vector<int> suffix_scores;
    for (int i = 0; i < num_seq; i++) {
      fill_forward_scores(&query[seq_len * i], &ref[seq_len * i], seq_len, &forward_scores);
      fill_suffix_scores(&query[seq_len * i], &ref[seq_len * i], seq_len, &suffix_scores);

      int actual = actual_output[i];
      int expected = forward_scores[table_index(seq_len + 1, seq_len, seq_len)];
      if (actual != expected) {
        fail = true;
        printf("Mismatch: i=%d, actual=%d, expected=%d\n", i, actual, expected);
      }

      const path_check_t path_check = validate_exported_path(
        &query[seq_len * i],
        &ref[seq_len * i],
        seq_len,
        &actual_path[i * seq_len],
        forward_scores,
        suffix_scores
      );

      if (!path_check.valid) {
        fail = true;
        printf("Invalid path point: seq=%d, ref=%d, query=%d, reason=%s\n",
               i, path_check.bad_ref, path_check.bad_query, path_check.error);
      }

      if (path_check.score != expected) {
        fail = true;
        printf("Path score mismatch: seq=%d, actual=%d, expected=%d\n",
               i, path_check.score, expected);
      }
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


declare_program_main("nw_baseline_multipod", nw_baseline_multipod);
