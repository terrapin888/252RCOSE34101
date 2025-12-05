#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nand_hal.h"

typedef struct {
    uint8_t data[NAND_PAGE_SIZE];
    uint8_t oob[NAND_OOB_SIZE];
    uint8_t is_written; // 덮어쓰기 방지 플래그
} nand_page_t;

typedef struct {
    nand_page_t pages[PAGES_PER_BLOCK];
    int is_bad;
} nand_block_t;

static nand_block_t *nand_device = NULL;

int nand_init(void) {
    // 256MB 메모리 할당
    nand_device = (nand_block_t *)malloc(sizeof(nand_block_t) * BLOCKS_PER_CHIP);
    if (!nand_device) return -1;

    // 초기화 (ALL 0xFF)
    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        nand_device[i].is_bad = 0;
        for (int j = 0; j < PAGES_PER_BLOCK; j++) {
            memset(nand_device[i].pages[j].data, 0xFF, NAND_PAGE_SIZE);
            memset(nand_device[i].pages[j].oob, 0xFF, NAND_OOB_SIZE);
            nand_device[i].pages[j].is_written = 0;
        }
    }
    return NAND_SUCCESS;
}

int nand_write(ppa_t ppa, const uint8_t *data, const uint8_t *oob) {
    int block = ppa / PAGES_PER_BLOCK;
    int page = ppa % PAGES_PER_BLOCK;

    if (block >= BLOCKS_PER_CHIP || !nand_device) return NAND_ERR_INVALID;
    if (nand_device[block].is_bad) return NAND_ERR_BADBLOCK;

    // 덮어쓰기 체크
    if (nand_device[block].pages[page].is_written) {
        printf("[HAL Error] Overwrite detected at Block %d Page %d\n", block, page);
        return NAND_ERR_OVERWRITE;
    }

    if (data) memcpy(nand_device[block].pages[page].data, data, NAND_PAGE_SIZE);
    if (oob)  memcpy(nand_device[block].pages[page].oob, oob, NAND_OOB_SIZE);
    
    nand_device[block].pages[page].is_written = 1;
    return NAND_SUCCESS;
}

int nand_read(ppa_t ppa, uint8_t *data, uint8_t *oob) {
    int block = ppa / PAGES_PER_BLOCK;
    int page = ppa % PAGES_PER_BLOCK;

    if (block >= BLOCKS_PER_CHIP || !nand_device) return NAND_ERR_INVALID;
    
    if (data) memcpy(data, nand_device[block].pages[page].data, NAND_PAGE_SIZE);
    if (oob)  memcpy(oob, nand_device[block].pages[page].oob, NAND_OOB_SIZE);
    return NAND_SUCCESS;
}

int nand_erase(int block) {
    if (block >= BLOCKS_PER_CHIP || !nand_device) return NAND_ERR_INVALID;

    for (int j = 0; j < PAGES_PER_BLOCK; j++) {
        memset(nand_device[block].pages[j].data, 0xFF, NAND_PAGE_SIZE);
        memset(nand_device[block].pages[j].oob, 0xFF, NAND_OOB_SIZE);
        nand_device[block].pages[j].is_written = 0;
    }
    return NAND_SUCCESS;
}

void nand_exit(void) {
    if (nand_device) {
        free(nand_device);
        nand_device = NULL;
    }
}

int nand_is_bad_block(int block) {
    if (!nand_device || block >= BLOCKS_PER_CHIP) return 1;
    return nand_device[block].is_bad;
}
