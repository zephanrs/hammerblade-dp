#ifndef SW_COMMON_TEST_INPUT_HPP
#define SW_COMMON_TEST_INPUT_HPP

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// variable-length tests default to the fixed-length case unless overridden.
#ifndef VAR_LEN_MIN
#define VAR_LEN_MIN SEQ_LEN
#endif

#ifndef LEN_SEED
#define LEN_SEED 1
#endif

#ifndef LEN_QUANTUM
#define LEN_QUANTUM 1
#endif

inline uint32_t next_length_rng(uint32_t* state) {
  *state = (*state * 1664525u) + 1013904223u;
  return *state;
}

inline int quantize_length(int len, int min_len, int max_len) {
  const int quantum = std::max(1, LEN_QUANTUM);
  if (quantum == 1) {
    return len;
  }

  const int rounded = ((len + quantum - 1) / quantum) * quantum;
  const int quantized_max = (max_len / quantum) * quantum;
  const int bounded_max = std::max(quantum, quantized_max);
  return std::max(min_len, std::min(rounded, bounded_max));
}

inline void read_seq_token(FILE* file, char* token, const char* filename) {
  if (fscanf(file, "%63s", token) == 1) {
    return;
  }

  clearerr(file);
  rewind(file);
  if (fscanf(file, "%63s", token) != 1) {
    fprintf(stderr, "Failed to read sequence token from %s\n", filename);
    exit(EXIT_FAILURE);
  }
}

inline void fill_dense_sequences(const char* filename,
                                uint8_t* seq,
                                int num_seq,
                                int max_seq_len) {
  FILE* file = fopen(filename, "r");
  if (file == nullptr) {
    fprintf(stderr, "Failed to open %s\n", filename);
    exit(EXIT_FAILURE);
  }

  const int chunks_per_seq = (max_seq_len + 31) / 32;
  for (int s = 0; s < num_seq; s++) {
    uint8_t* dst = &seq[s * max_seq_len];
    memset(dst, 0, max_seq_len);

    for (int chunk = 0; chunk < chunks_per_seq; chunk++) {
      char temp_seq[64];
      read_seq_token(file, temp_seq, filename);
      read_seq_token(file, temp_seq, filename);

      const int offset = chunk * 32;
      const int bytes = std::min(32, max_seq_len - offset);
      memcpy(&dst[offset], temp_seq, bytes);
    }
  }

  fclose(file);
}

inline void fill_lengths(int* lens,
                         int num_seq,
                         int min_len,
                         int max_len,
                         uint32_t seed) {
  min_len = std::max(1, std::min(min_len, max_len));
  max_len = std::max(min_len, max_len);

  if (min_len == max_len) {
    for (int i = 0; i < num_seq; i++) {
      lens[i] = max_len;
    }
    return;
  }

  uint32_t state = seed;
  const uint32_t span = static_cast<uint32_t>(max_len - min_len + 1);
  for (int i = 0; i < num_seq; i++) {
    const int raw_len = min_len + static_cast<int>(next_length_rng(&state) % span);
    lens[i] = quantize_length(raw_len, min_len, max_len);
  }
}

inline void fill_tail_padding(uint8_t* seq,
                              const int* lens,
                              int num_seq,
                              int max_seq_len,
                              uint8_t pad_value) {
  for (int s = 0; s < num_seq; s++) {
    if (lens[s] < max_seq_len) {
      memset(&seq[(s * max_seq_len) + lens[s]], pad_value, max_seq_len - lens[s]);
    }
  }
}

inline void prepare_sw_inputs(const char* query_path,
                              const char* ref_path,
                              uint8_t* query,
                              uint8_t* ref,
                              int* qry_lens,
                              int* ref_lens,
                              int num_seq,
                              int max_seq_len) {
  fill_dense_sequences(query_path, query, num_seq, max_seq_len);
  fill_dense_sequences(ref_path, ref, num_seq, max_seq_len);
  fill_lengths(qry_lens, num_seq, VAR_LEN_MIN, max_seq_len, LEN_SEED ^ 0x13579bdfu);
  for (int i = 0; i < num_seq; i++) {
    ref_lens[i] = qry_lens[i];
  }
  fill_tail_padding(query, qry_lens, num_seq, max_seq_len, 0);
  fill_tail_padding(ref, ref_lens, num_seq, max_seq_len, 1);
}

inline void sort_sw_inputs_by_length(uint8_t* query,
                                     uint8_t* ref,
                                     int* qry_lens,
                                     int* ref_lens,
                                     int num_seq,
                                     int max_seq_len) {
  std::vector<int> order(num_seq, 0);
  for (int i = 0; i < num_seq; i++) {
    order[i] = i;
  }

  std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
    return qry_lens[lhs] < qry_lens[rhs];
  });

  std::vector<uint8_t> query_sorted(num_seq * max_seq_len, 0);
  std::vector<uint8_t> ref_sorted(num_seq * max_seq_len, 0);
  std::vector<int> qry_sorted(num_seq, 0);
  std::vector<int> ref_sorted_lens(num_seq, 0);

  for (int dst = 0; dst < num_seq; dst++) {
    const int src = order[dst];
    qry_sorted[dst] = qry_lens[src];
    ref_sorted_lens[dst] = ref_lens[src];
    memcpy(&query_sorted[dst * max_seq_len],
           &query[src * max_seq_len],
           max_seq_len);
    memcpy(&ref_sorted[dst * max_seq_len],
           &ref[src * max_seq_len],
           max_seq_len);
  }

  memcpy(query, query_sorted.data(), query_sorted.size());
  memcpy(ref, ref_sorted.data(), ref_sorted.size());
  memcpy(qry_lens, qry_sorted.data(), qry_sorted.size() * sizeof(int));
  memcpy(ref_lens, ref_sorted_lens.data(), ref_sorted_lens.size() * sizeof(int));
}

inline void pack_variable_stride_sequences(const uint8_t* dense_src,
                                          const int* lens,
                                          int num_seq,
                                          int max_seq_len,
                                          uint8_t* packed_dst) {
  memset(packed_dst, 0, num_seq * max_seq_len);
  for (int s = 0; s < num_seq; s++) {
    memcpy(&packed_dst[s * lens[s]], &dense_src[s * max_seq_len], lens[s]);
  }
}

inline int sw_reference_score(const uint8_t* query,
                              int qry_len,
                              const uint8_t* ref,
                              int ref_len) {
  std::vector<int> prev(ref_len + 1, 0);
  std::vector<int> curr(ref_len + 1, 0);
  int maxv = 0;

  for (int i = 0; i < qry_len; i++) {
    curr[0] = 0;
    for (int j = 0; j < ref_len; j++) {
      const int match = (query[i] == ref[j]) ? 1 : -1;
      const int score_diag = prev[j] + match;
      const int score_up = prev[j + 1] - 1;
      const int score_left = curr[j] - 1;
      curr[j + 1] = std::max(0, std::max(score_diag, std::max(score_up, score_left)));
      if (curr[j + 1] > maxv) {
        maxv = curr[j + 1];
      }
    }
    std::swap(prev, curr);
  }

  return maxv;
}

inline bool validate_sw_outputs(const uint8_t* query,
                                const uint8_t* ref,
                                const int* qry_lens,
                                const int* ref_lens,
                                int max_seq_len,
                                int num_seq,
                                const int* actual_output) {
  bool fail = false;

  for (int s = 0; s < num_seq; s++) {
    const int expected = sw_reference_score(&query[s * max_seq_len],
                                            qry_lens[s],
                                            &ref[s * max_seq_len],
                                            ref_lens[s]);
    if (actual_output[s] != expected) {
      fail = true;
      printf("Mismatch: i=%d, actual=%d, expected=%d, qry_len=%d, ref_len=%d\n",
             s,
             actual_output[s],
             expected,
             qry_lens[s],
             ref_lens[s]);
    }
  }

  return !fail;
}

#endif
