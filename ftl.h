#ifndef FTL_H
#define FTL_H

#include <stdint.h>

// 설정값 정의
#define LOGICAL_PAGES_COUNT 60000 

// 함수 원형 선언 (내용 구현 없음, 세미콜론 필수)
int ftl_init(void);
void ftl_read(uint32_t lba, uint8_t *buffer);
void ftl_write(uint32_t lba, const uint8_t *buffer);
void ftl_exit(void);

#endif
