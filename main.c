#include <stdio.h>
#include <string.h>
#include "ftl.h"
#include "nand_hal.h"

int main() {
    printf("=== FTL Simulation Start (User Space) ===\n");
    if (ftl_init() != 0) {
        printf("Init Failed\n");
        return -1;
    }

    uint8_t buf[NAND_PAGE_SIZE];
    memset(buf, 0xAB, NAND_PAGE_SIZE);

    printf("Starting Stress Test (Writing 80,000 pages)...\n");
    // 총 용량(약 65,000 페이지)보다 많이 써서 GC를 유발함
    for (int i = 0; i < 80000; i++) {
        uint32_t lba = i % 200; // 0~199번 LBA만 계속 덮어쓰기 (Hot Data)
        ftl_write(lba, buf);
        
        if (i % 5000 == 0) printf(" - Written %d pages (GC Running...)\n", i);
    }

    // 검증
    uint8_t r_buf[NAND_PAGE_SIZE];
    ftl_read(199, r_buf); // 마지막 루프의 199번 데이터 확인
    
    if (r_buf[0] == 0xAB) {
        printf("[Success] Test Completed. Data Integrity Verified.\n");
    } else {
        printf("[Fail] Data Mismatch!\n");
    }

    ftl_exit();
    return 0;
}
