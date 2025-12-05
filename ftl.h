#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nand_hal.h"
#include "ftl.h"
-
typedef struct {
    int invalid_page_count; // garbage of page
} block_info_t;

static uint32_t *l2p_table;           // mapping table)
static block_info_t *block_table;     // blcok state
static int current_block_index = 0;   // writing block
static int current_page_index = 0;    // writing block

// initialize
int ftl_init(void) {
    if (nand_init() != NAND_SUCCESS) return -1;

    l2p_table = (uint32_t *)malloc(sizeof(uint32_t) * LOGICAL_PAGES_COUNT);
    if (!l2p_table) return -1;
    
    memset(l2p_table, 0xFF, sizeof(uint32_t) * LOGICAL_PAGES_COUNT);

    block_table = (block_info_t *)malloc(sizeof(block_info_t) * BLOCKS_PER_CHIP);
    if (!block_table) {
        free(l2p_table);
        return -1;
    }
    memset(block_table, 0, sizeof(block_info_t) * BLOCKS_PER_CHIP);

    current_block_index = 0;
    current_page_index = 0;

    printf("[FTL] Initialized. Logical Pages: %d\n", LOGICAL_PAGES_COUNT);
    return 0;
}

// read
void ftl_read(uint32_t lba, uint8_t *buffer) {
    if (lba >= LOGICAL_PAGES_COUNT) {
        printf("[FTL Error] Read LBA out of range: %d\n", lba);
        return;
    }

    uint32_t ppa = l2p_table[lba];

    // empty address
    if (ppa == 0xFFFFFFFF) {
        memset(buffer, 0xFF, NAND_PAGE_SIZE);
        return;
    }

    // read
    nand_read(ppa, buffer, NULL); 
}

// write
void ftl_write(uint32_t lba, const uint8_t *buffer) {
    if (lba >= LOGICAL_PAGES_COUNT) {
        printf("[FTL Error] Write LBA out of range: %d\n", lba);
        return;
    }

    if (current_page_index >= PAGES_PER_BLOCK) {
        current_block_index++;
        current_page_index = 0;

        if (current_block_index >= BLOCKS_PER_CHIP) {
            printf("[FTL Error] NAND Full! Critical Error (GC needed)\n");
            return;
        }
    }

    uint32_t target_ppa = current_block_index * PAGES_PER_BLOCK + current_page_index;
    uint32_t old_ppa = l2p_table[lba];
    if (old_ppa != 0xFFFFFFFF) {
        int old_block = old_ppa / PAGES_PER_BLOCK;
        
         block_table[old_block].invalid_page_count++;
       
         if (block_table[old_block].invalid_page_count == PAGES_PER_BLOCK) {
            printf("[FTL Info] Block %d is fully invalid.\n", old_block);
        }
    }


    uint8_t spare_buf[NAND_OOB_SIZE];      
    memset(spare_buf, 0xFF, NAND_OOB_SIZE);
    memcpy(spare_buf, &lba, sizeof(uint32_t));
    
    int ret = nand_write(target_ppa, buffer, spare_buf);
    if (ret != NAND_SUCCESS) {
        printf("[FTL Error] NAND Write Failed at PPA %d\n", target_ppa);
        return;
    }

    
    l2p_table[lba] = target_ppa;

    
    current_page_index++;
}


int ftl_find_victim_block(void) {
    int victim_block = -1;
    int max_invalid = -1;

    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        if (i == current_block_index) continue;

        if (nand_is_bad_block(i)) continue;

        if (block_table[i].invalid_page_count > max_invalid) {
            max_invalid = block_table[i].invalid_page_count;
            victim_block = i;
        }
    }

    if (max_invalid <= 0) {
        return -1;
    }

    return victim_block;
}

void ftl_exit(void) {
    if (l2p_table) {
        free(l2p_table);
        l2p_table = NULL;
    }

    if (block_table) {
        free(block_table);
        block_table = NULL;
    }
    nand_exit();
}

// Debug
void ftl_print_map(uint32_t lba) {
    if (lba < LOGICAL_PAGES_COUNT) {
        uint32_t ppa = l2p_table[lba];
        if (ppa == 0xFFFFFFFF) {
            printf("LBA %d -> Unmapped\n", lba);
        } else {
            printf("LBA %d -> PPA %d (Block %d, Page %d)\n", 
                lba, ppa, ppa / PAGES_PER_BLOCK, ppa % PAGES_PER_BLOCK);
        }
    }
}
