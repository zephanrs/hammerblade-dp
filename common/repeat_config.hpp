#ifndef COMMON_REPEAT_CONFIG_HPP
#define COMMON_REPEAT_CONFIG_HPP

#ifndef INPUT_REPEAT_FACTOR
#define INPUT_REPEAT_FACTOR 1
#endif

constexpr int kInputRepeatFactor = INPUT_REPEAT_FACTOR;

inline int total_output_count(int unique_num_seq) {
  return unique_num_seq * kInputRepeatFactor;
}

#endif
