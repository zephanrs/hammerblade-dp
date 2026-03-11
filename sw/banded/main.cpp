#include <bsg_manycore_errno.h>
#include <bsg_manycore_cuda.h>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <bsg_manycore_regression.h>
#include <bsg_manycore.h>
#include <vector>

#define ALLOC_NAME "default_allocator"

#ifndef COL
#define COL 1
#endif

#ifndef BAND_SIZE
#if defined(bsg_tiles_Y)
#define BAND_SIZE (2 * bsg_tiles_Y)
#else
#define BAND_SIZE 16
#endif
#endif

static void read_seq_token(FILE* file, char* token, const char* filename) {
  if (fscanf(file, "%63s", token) != 1) {
    fprintf(stderr, "Failed to read sequence token from %s\n", filename);
    exit(EXIT_FAILURE);
  }
}

void read_seq(const char* filename, uint8_t* seq, int num_seq, int seq_len) {
  FILE* file = fopen(filename, "r");
  if (file == nullptr) {
    fprintf(stderr, "Failed to open %s\n", filename);
    exit(EXIT_FAILURE);
  }

  const int chunks_per_seq = (seq_len + 31) / 32;
  for (int seq_idx = 0; seq_idx < num_seq; seq_idx++) {
    const int seq_offset = seq_idx * seq_len;
    for (int chunk_idx = 0; chunk_idx < chunks_per_seq; chunk_idx++) {
      char temp_seq[64];
      read_seq_token(file, temp_seq, filename); // skip line number;
      read_seq_token(file, temp_seq, filename);

      const int dst_offset = seq_offset + (chunk_idx * 32);
      const int copy_len = std::min(32, seq_len - (chunk_idx * 32));
      for (int j = 0; j < copy_len; j++) {
        seq[dst_offset + j] = temp_seq[j];
      }
    }
  }
  fclose(file);
}

static int banded_sw_reference(const uint8_t* query, const uint8_t* ref, int seq_len) {
  std::vector<int> prev(seq_len, 0);
  int starter_diag = 0;
  int maxv = 0;

  for (int row = 0; row < seq_len; row++) {
    const int start_col = row;
    const int end_col = std::min(seq_len, row + BAND_SIZE);
    int left = 0;
    int diag = starter_diag;
    int next_seed = 0;

    for (int col = start_col; col < end_col; col++) {
      const int up = prev[col];
      const int match = (query[row] == ref[col]) ? 1 : -1;
      const int score_diag = diag + match;
      const int score_up = up - 1;
      const int score_left = left - 1;
      const int value = std::max(0, std::max(score_diag, std::max(score_up, score_left)));

      if (col == start_col) {
        next_seed = value;
      }

      prev[col] = value;
      diag = up;
      left = value;

      if (value > maxv) {
        maxv = value;
      }
    }

    starter_diag = next_seed;
  }

  return maxv;
}

// Host main;
int sw_banded_multipod(int argc, char ** argv) {
  (void)argc;

  // command line;
  const char *bin_path = argv[1];
  const char *query_path = argv[2];
  const char *ref_path = argv[3];

  // parameters;
  const int num_seq = NUM_SEQ; // per pod;
  const int seq_len = SEQ_LEN;
  printf("num_seq=%d\n", num_seq);
  printf("seq_len=%d\n", seq_len);
  printf("band_size=%d\n", BAND_SIZE);
  printf("col=%d\n", COL);
  printf("divisibility_assumption=none\n");
  
  // prepare inputs;
  std::vector<uint8_t> query(num_seq * seq_len);
  std::vector<uint8_t> ref(num_seq * seq_len);
  read_seq(query_path, query.data(), num_seq, seq_len);
  read_seq(ref_path, ref.data(), num_seq, seq_len);
 
  // initialize device; 
  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "sw_banded_multipod", HB_MC_DEVICE_ID));

  eva_t d_query;
  eva_t d_ref;
  eva_t d_output;

  const hb_mc_pod_id_t pod_count = static_cast<hb_mc_pod_id_t>(device.pod_count);
  for (hb_mc_pod_id_t pod = 0; pod < pod_count; ++pod) {
    printf("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    // Allocate memory on device;
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*(seq_len+1)*sizeof(uint8_t), &d_query));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*(seq_len+1)*sizeof(uint8_t), &d_ref));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*sizeof(int), &d_output));
   
    // DMA transfer;
    printf("Transferring data: pod %d\n", pod);
    std::vector<hb_mc_dma_htod_t> htod_job;
    htod_job.push_back({d_query, query.data(), static_cast<uint32_t>(num_seq * seq_len * sizeof(uint8_t))});
    htod_job.push_back({d_ref, ref.data(), static_cast<uint32_t>(num_seq * seq_len * sizeof(uint8_t))});
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
  hb_mc_manycore_trace_enable((&device)->mc);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  hb_mc_manycore_trace_disable((&device)->mc);


  // Read from device;
  std::vector<int> actual_output(num_seq);
  std::vector<int> expected_output(num_seq);

  bool fail = false;
  for (hb_mc_pod_id_t pod = 0; pod < pod_count; ++pod) {
    printf("Reading results: pods %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));

    // clear buf;
    std::fill(actual_output.begin(), actual_output.end(), 0);

    // DMA transfer; device -> host;
    std::vector<hb_mc_dma_dtoh_t> dtoh_job;
    dtoh_job.push_back({d_output, actual_output.data(), static_cast<uint32_t>(num_seq * sizeof(int))});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&device, dtoh_job.data(), dtoh_job.size()));

    for (int i = 0; i < num_seq; i++) {
      expected_output[i] = banded_sw_reference(&query[seq_len * i], &ref[seq_len * i], seq_len);
    }

    // validate;
    for (int i = 0; i < num_seq; i++) {
      const int actual = actual_output[i];
      const int expected = expected_output[i];
      if (actual != expected) {
        fail = true;
        printf("Mismatch: i=%d, actual=%d, expected=%d\n", i, actual, expected);
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


declare_program_main("sw_banded_multipod", sw_banded_multipod);
