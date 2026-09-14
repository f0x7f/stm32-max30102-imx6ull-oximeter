#ifndef ALGORITHM_H
#define ALGORITHM_H

#include <stdint.h>

#define FS 100 /* MAX30102 = 100 sps */
#define BUFFER_SIZE (FS * 5) /* 5秒窗 = 500 样本 */
#define MA4_SIZE 4 /* DO NOT CHANGE */
#define HAMMING_SIZE 5 /* DO NOT CHANGE */
#define min(x, y) ((x) < (y) ? (x) : (y))

void maxim_heart_rate_and_oxygen_saturation(uint32_t *pun_ir_buffer, int32_t n_ir_buffer_length, uint32_t *pun_red_buffer,
					    int32_t *pn_spo2, int8_t *pch_spo2_valid, int32_t *pn_heart_rate, int8_t *pch_hr_valid);
void maxim_find_peaks(int32_t *, int32_t *, int32_t *, int32_t, int32_t, int32_t, int32_t);
void maxim_peaks_above_min_height(int32_t *, int32_t *, int32_t *, int32_t, int32_t);
void maxim_remove_close_peaks(int32_t *, int32_t *, int32_t *, int32_t);
void maxim_sort_ascend(int32_t *, int32_t);
void maxim_sort_indices_descend(int32_t *, int32_t *, int32_t);

#endif
