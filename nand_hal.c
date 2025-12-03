#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nand_hal.h"

// 
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

static nand_block_t *nand_device = NULL;

int nand_init(void) {
    // alllcate space for nand
    nand_device = (nand_block_t *)malloc(sizeof(nand_block_t) * BLOCKS_PER_CHIP);
    if (!nand_device) return -1;

    // initialize
    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        nand_device[i].erase_count = 0;
        nand_device[i].is_bad = 0;
        for (int j = 0; j < PAGES_PER_BLOCK; j++) {
            memset(nand_device[i].pages[j].data, 0xFF, NAND_PAGE_SIZE);
            memset(nand_device[i].pages[j].oob, 0xFF, NAND_OOB_SIZE);
            nand_device[i].pages[j].is_written = 0;
        }
    }
    return NAND_SUCCESS;
}

// write 
int nand_write(ppa_t ppa, const uint8_t *data_buf, const uint8_t *oob_buf) {
    int block = ppa / PAGES_PER_BLOCK;
    int page = ppa % PAGES_PER_BLOCK;

    if (block >= BLOCKS_PER_CHIP) return NAND_ERR_INVALID;
    if (nand_device[block].is_bad) return NAND_ERR_BADBLOCK;

    // Overwrite Check
    if (nand_device[block].pages[page].is_written == 1) {
        printf("[Error] Overwrite detected at Block %d, Page %d\n", block, page);
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
    if (nand_device[block_index].is_bad) return NAND_ERR_BADBLOCK;

    // erase
    for (int j = 0; j < PAGES_PER_BLOCK; j++) {
        memset(nand_device[block_index].pages[j].data, 0xFF, NAND_PAGE_SIZE);
        memset(nand_device[block_index].pages[j].oob, 0xFF, NAND_OOB_SIZE);
        nand_device[block_index].pages[j].is_written = 0;
    }

    // errase count ++
    nand_device[block_index].erase_count++;

    // bad block
    if (nand_device[block_index].erase_count > 3000) {
        printf("[HW Event] Block %d is worn out!\n", block_index); 
    }

    return NAND_SUCCESS;
}


// read
int nand_read(ppa_t ppa, uint8_t *data_buf, uint8_t *oob_buf) {
    int block = ppa / PAGES_PER_BLOCK;
    int page = ppa % PAGES_PER_BLOCK;

    if (block >= BLOCKS_PER_CHIP) return NAND_ERR_INVALID;
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
        free(nand_device);
        nand_device = NULL;
    }
}

// Debug
uint32_t nand_get_erase_count(int block_index) {
    if (block_index >= BLOCKS_PER_CHIP) return 0;
    return nand_device[block_index].erase_count;
}

// bad block
int nand_is_bad_block(int block_index) {
    if (block_index >= BLOCKS_PER_CHIP) return 1; 
    return nand_device[block_index].is_bad;
}
