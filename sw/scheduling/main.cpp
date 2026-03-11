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
#include "scheduler.hpp"

#define ALLOC_NAME "default_allocator"

// the fasta reader treats each sequence as two whitespace-separated tokens.
static void read_seq_token(FILE* file, char* token, const char* filename) {
  if (fscanf(file, "%63s", token) != 1) {
    fprintf(stderr, "Failed to read sequence token from %s\n", filename);
    exit(EXIT_FAILURE);
  }
}

// the checked-in inputs are stored densely as fixed-width ascii strings.
void read_seq(const char* filename, uint8_t* seq, int num_seq) {
  FILE* file = fopen(filename, "r");
  if (file == nullptr) {
    fprintf(stderr, "Failed to open %s\n", filename);
    exit(EXIT_FAILURE);
  }
  for (int i = 0; i < num_seq; i++) {
    char temp_seq[64];
    read_seq_token(file, temp_seq, filename); // skip line number;
    read_seq_token(file, temp_seq, filename);
    for (int j = 0; j < 32; j++) {
      seq[(32*i)+j] = temp_seq[j]; 
    }
  } 
  fclose(file);
}

// Host main;
int sw_multipod(int argc, char ** argv) {
  int r = 0;
  
  // argv[1] is still the device binary path expected by the existing host flow.
  // command line;
  const char *bin_path = argv[1];
  const char *query_path = argv[2];
  const char *ref_path = argv[3];

  // these tests still use one uniform sequence length even though the kernel is dynamic.
  // parameters;
  int num_seq = NUM_SEQ; // per pod;
  int seq_len = SEQ_LEN;
  printf("num_seq=%d\n", num_seq);
  printf("seq_len=%d\n", seq_len);
  
  // sequence storage stays dense so validation can reuse the old indexing scheme.
  // prepare inputs;
  uint8_t* query = (uint8_t*) malloc(num_seq*seq_len*sizeof(uint8_t));
  uint8_t* ref = (uint8_t*) malloc(num_seq*seq_len*sizeof(uint8_t));
  // hacky way to get longer sequences:
  read_seq(query_path, query, num_seq * (seq_len / 32));
  read_seq(ref_path, ref, num_seq * (seq_len / 32));
 
  // explicit lengths are passed because the scheduler computes team sizes from them.
  int* qry_lens = (int*) malloc(num_seq*sizeof(int));
  int* ref_lens = (int*) malloc(num_seq*sizeof(int));
  for (int i = 0; i < num_seq; i++) {
    qry_lens[i] = seq_len;
    ref_lens[i] = seq_len;
  }

  // this is the initial dram snapshot of the shared scheduler state.
  sched_state_t sched_init = {};
  sched_init.lock = 0;
  sched_init.launch_head = 0;
  sched_init.wait_tail = 0;
  sched_init.pending_seq = 0;
  sched_init.remaining = team_size_for_length(ref_lens[0]);
  for (int i = 0; i < NUM_TILES; i++) {
    sched_init.queue[i].core_id = -1;
  }

  // initialize device;
  hb_mc_device_t device;
  BSG_CUDA_CALL(hb_mc_device_init(&device, "sw_multipod", HB_MC_DEVICE_ID));

  eva_t d_query;
  eva_t d_ref;
  eva_t d_qry_lens;
  eva_t d_ref_lens;
  eva_t d_sched;
  eva_t d_output;

  // each pod gets its own copy of the inputs and shared scheduler state.
  hb_mc_pod_id_t pod;
  hb_mc_device_foreach_pod_id(&device, pod)
  {
    printf("Loading program for pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));
    BSG_CUDA_CALL(hb_mc_device_program_init(&device, bin_path, ALLOC_NAME, 0));

    // the scheduler lives in dram because every tile updates it with atomics.
    // Allocate memory on device;
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*seq_len*sizeof(uint8_t), &d_query));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*seq_len*sizeof(uint8_t), &d_ref));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*sizeof(int), &d_qry_lens));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*sizeof(int), &d_ref_lens));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, sizeof(sched_state_t), &d_sched));
    BSG_CUDA_CALL(hb_mc_device_malloc(&device, num_seq*sizeof(int), &d_output));
   
    // data transfer copies both the biological inputs and the scheduler seed state.
    // DMA transfer;
    printf("Transferring data: pod %d\n", pod);
    std::vector<hb_mc_dma_htod_t> htod_job;
    htod_job.push_back({d_query, query, num_seq*seq_len*sizeof(uint8_t)});
    htod_job.push_back({d_ref, ref, num_seq*seq_len*sizeof(uint8_t)});
    htod_job.push_back({d_qry_lens, qry_lens, num_seq*sizeof(int)});
    htod_job.push_back({d_ref_lens, ref_lens, num_seq*sizeof(int)});
    htod_job.push_back({d_sched, &sched_init, sizeof(sched_state_t)});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_device(&device, htod_job.data(), htod_job.size()));

    // Cuda args;
    hb_mc_dimension_t tg_dim = { .x = bsg_tiles_X, .y = bsg_tiles_Y};
    hb_mc_dimension_t grid_dim = { .x = 1, .y = 1};
    #define CUDA_ARGC 8
    uint32_t cuda_argv[CUDA_ARGC] = {
      // the kernel interface is now explicit about lengths and scheduler state.
      d_query,
      d_ref,
      d_qry_lens,
      d_ref_lens,
      d_sched,
      (uint32_t)num_seq,
      d_output,
      (uint32_t)pod
    };


    // Enqueue kernel;
    printf("Enqueue Kernel: pod %d\n", pod);
    BSG_CUDA_CALL(hb_mc_kernel_enqueue (&device, grid_dim, tg_dim, "kernel", CUDA_ARGC, cuda_argv));
  }


  // Launch pod;
  printf("Launching all pods\n");
  hb_mc_manycore_trace_enable((&device)->mc);
  BSG_CUDA_CALL(hb_mc_device_pods_kernels_execute(&device));
  hb_mc_manycore_trace_disable((&device)->mc);


  // validation still compares against a plain cpu smith-waterman implementation.
  // Read from device;
  int* actual_output = (int*) malloc(num_seq*sizeof(int));

  bool fail = false;
  hb_mc_device_foreach_pod_id(&device, pod) {
    printf("Reading results: pods %d\n", pod);
    BSG_CUDA_CALL(hb_mc_device_set_default_pod(&device, pod));

    // clear buf;
    for (int i = 0; i < num_seq; i++) {
      actual_output[i] = 0;
    }

    // DMA transfer; device -> host;
    std::vector<hb_mc_dma_dtoh_t> dtoh_job;
    dtoh_job.push_back({d_output, actual_output, num_seq*sizeof(int)});
    BSG_CUDA_CALL(hb_mc_device_transfer_data_to_host(&device, dtoh_job.data(), dtoh_job.size()));

    // this reference path keeps the old dense layout so correctness stays obvious.
    int H[seq_len+1][seq_len+1];
    int m[num_seq];
    int mj = 0, mk=0;
    for (int i = 0; i < num_seq; i++) {
      memset(H, 0, sizeof(H));
      m[i] = 0;
      for (int j = 0; j < qry_lens[i]; j++) {
        for (int k = 0; k < ref_lens[i]; k++) {
          int match = (query[seq_len*i+j] == ref[seq_len*i+k]) ? 1 : -1;
          int score_diag = H[j][k] + match;
          int score_up = H[j][k+1] - 1;
          int score_left = H[j+1][k] - 1;
          H[j+1][k+1] = std::max(0, std::max(score_diag, std::max(score_up, score_left)));
          if (H[j+1][k+1] > m[i]) {
            mj = j;
            mk = k;
            m[i] = H[j+1][k+1];
          }
        }
      }
    }

    // validate;
    printf("Max is at row %d, column %d\n", mj, mk);
    for (int i = 0; i < num_seq; i++) {
      int actual = actual_output[i];
      int expected = m[i];
      if (actual != expected) {
        fail = true;
        printf("Mismatch: i=%d, actual=%d, expected=%d\n", i, actual, expected);
      }
    }
  }


  // only the extra length arrays need explicit cleanup on the host side.
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
