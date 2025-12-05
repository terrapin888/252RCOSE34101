#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>  // malloc 대신 대용량 할당용
#include <linux/string.h>   // memset, memcpy
#include <linux/types.h>    // uint8_t, uint32_t 등
#include "nand_hal.h"

// 구조체 정의 (그대로 유지)
typedef struct {
    uint8_t data[NAND_PAGE_SIZE];
    uint8_t oob[NAND_OOB_SIZE];
    uint8_t is_written; // write flag
} nand_page_t;

typedef struct {
    nand_page_t pages[PAGES_PER_BLOCK];
    uint32_t erase_count;
    int is_bad;
} nand_block_t;

// 가상 디바이스 포인터
static nand_block_t *nand_device = NULL;

int nand_init(void) {
    // 1. 메모리 할당 변경: malloc -> vmalloc
    // NAND 전체 크기가 수백 MB 단위이므로 vmalloc 필수 (kmalloc은 용량 제한 있음)
    unsigned long total_size = sizeof(nand_block_t) * BLOCKS_PER_CHIP;
    
    nand_device = (nand_block_t *)vmalloc(total_size);
    if (!nand_device) {
        printk(KERN_ERR "[NAND-HAL] Failed to vmalloc memory (%lu bytes)\n", total_size);
        return -1;
    }

    // 2. 초기화 (로직 동일)
    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        nand_device[i].erase_count = 0;
        nand_device[i].is_bad = 0;
        for (int j = 0; j < PAGES_PER_BLOCK; j++) {
            memset(nand_device[i].pages[j].data, 0xFF, NAND_PAGE_SIZE);
            memset(nand_device[i].pages[j].oob, 0xFF, NAND_OOB_SIZE);
            nand_device[i].pages[j].is_written = 0;
        }
    }
    
    printk(KERN_INFO "[NAND-HAL] Initialized Virtual NAND (%lu MB)\n", total_size / (1024 * 1024));
    return NAND_SUCCESS;
}

// write 
int nand_write(ppa_t ppa, const uint8_t *data_buf, const uint8_t *oob_buf) {
    int block = ppa / PAGES_PER_BLOCK;
    int page = ppa % PAGES_PER_BLOCK;

    if (block >= BLOCKS_PER_CHIP) return NAND_ERR_INVALID;
    // 커널 패닉 방지를 위해 NULL 체크 추가
    if (!nand_device) return -1; 
    if (nand_device[block].is_bad) return NAND_ERR_BADBLOCK;

    // Overwrite Check
    if (nand_device[block].pages[page].is_written == 1) {
        // printf -> printk(KERN_ERR ...) 변경
        printk(KERN_ERR "[NAND-HAL] Overwrite detected at Block %d, Page %d\n", block, page);
        return NAND_ERR_OVERWRITE; 
    }

    // write
    if (data_buf)
        memcpy(nand_device[block].pages[page].data, data_buf, NAND_PAGE_SIZE);
    if (oob_buf)
        memcpy(nand_device[block].pages[page].oob, oob_buf, NAND_OOB_SIZE);
    
    nand_device[block].pages[page].is_written = 1;

    return NAND_SUCCESS;
}

int nand_erase(int block_index) {
    if (block_index >= BLOCKS_PER_CHIP) return NAND_ERR_INVALID;
    if (!nand_device) return -1;
    if (nand_device[block_index].is_bad) return NAND_ERR_BADBLOCK;

    // erase
    for (int j = 0; j < PAGES_PER_BLOCK; j++) {
        memset(nand_device[block_index].pages[j].data, 0xFF, NAND_PAGE_SIZE);
        memset(nand_device[block_index].pages[j].oob, 0xFF, NAND_OOB_SIZE);
        nand_device[block_index].pages[j].is_written = 0;
    }

    // erase count ++
    nand_device[block_index].erase_count++;

    // bad block simulation
    if (nand_device[block_index].erase_count > 3000) {
        printk(KERN_NOTICE "[NAND-HAL] HW Event: Block %d is worn out!\n", block_index); 
    }

    return NAND_SUCCESS;
}


// read
int nand_read(ppa_t ppa, uint8_t *data_buf, uint8_t *oob_buf) {
    int block = ppa / PAGES_PER_BLOCK;
    int page = ppa % PAGES_PER_BLOCK;

    if (block >= BLOCKS_PER_CHIP) return NAND_ERR_INVALID;
    if (!nand_device) return -1;
    if (nand_device[block].is_bad) return NAND_ERR_BADBLOCK;

    // read
    if (data_buf != NULL) {
        memcpy(data_buf, nand_device[block].pages[page].data, NAND_PAGE_SIZE);
    }

    if (oob_buf != NULL) {
        memcpy(oob_buf, nand_device[block].pages[page].oob, NAND_OOB_SIZE);
    }
    return NAND_SUCCESS;
}

// free
void nand_exit(void) {
    if (nand_device != NULL) {
        vfree(nand_device); // free -> vfree 변경
        nand_device = NULL;
        printk(KERN_INFO "[NAND-HAL] Memory Freed.\n");
    }
}

// Debug
uint32_t nand_get_erase_count(int block_index) {
    if (block_index >= BLOCKS_PER_CHIP || !nand_device) return 0;
    return nand_device[block_index].erase_count;
}

// bad block
int nand_is_bad_block(int block_index) {
    if (block_index >= BLOCKS_PER_CHIP || !nand_device) return 1; 
    return nand_device[block_index].is_bad;
}
