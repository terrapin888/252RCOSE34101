#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nand_hal.h"
#include "ftl.h"

static void ftl_gc(void);
static int ftl_find_victim_block(void);
static int ftl_get_free_block(void);


typedef struct {
    int invalid_page_count; // garbage of page
    int is_free;
} block_info_t;

static uint32_t *l2p_table;           // mapping table)
static block_info_t *block_table;     // blcok state
static int current_block_index = 0;   // writing block
static int current_page_index = 0;    // writing block

// initialize
int ftl_init(void) {
    if (nand_init() != NAND_SUCCESS) return -1;

    l2p_table = (uint32_t *)malloc(sizeof(uint32_t) * LOGICAL_PAGES_COUNT);
    memset(l2p_table, 0xFF, sizeof(uint32_t) * LOGICAL_PAGES_COUNT);

    block_table = (block_info_t *)malloc(sizeof(block_info_t) * BLOCKS_PER_CHIP);
    
    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        block_table[i].invalid_page_count = 0;
        block_table[i].is_free = 1; 
    }

    current_block_index = 0;
    current_page_index = 0;
    block_table[0].is_free = 0; 

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
        printf("[Error] LBA out of range\n");
        return;
    }

    if (current_page_index >= PAGES_PER_BLOCK) {
        int next_block = ftl_get_free_block();
        
        if (next_block == -1) {
            return;
        }
        current_block_index = next_block;
        current_page_index = 0;
    }

    uint32_t target_ppa = current_block_index * PAGES_PER_BLOCK + current_page_index;

    uint32_t old_ppa = l2p_table[lba];
    if (old_ppa != 0xFFFFFFFF) {
        int old_block = old_ppa / PAGES_PER_BLOCK;
        block_table[old_block].invalid_page_count++;
    }

    uint8_t spare_buf[NAND_OOB_SIZE];
    memset(spare_buf, 0xFF, NAND_OOB_SIZE);
    memcpy(spare_buf, &lba, sizeof(uint32_t)); 

    int ret = nand_write(target_ppa, buffer, spare_buf);
    
    if (ret == NAND_SUCCESS) {
        l2p_table[lba] = target_ppa;
        current_page_index++;
    }
}

void ftl_gc(void) {
    uint8_t data_buf[NAND_PAGE_SIZE];
    uint8_t oob_buf[NAND_OOB_SIZE];
    uint32_t lba_from_oob;

    int victim_blk = ftl_find_victim_block();
    if (victim_blk == -1) {
        return;
    }

    for (int page = 0; page < PAGES_PER_BLOCK; page++) {
        uint32_t ppa = victim_blk * PAGES_PER_BLOCK + page;
        nand_read(ppa, NULL, oob_buf);
        memcpy(&lba_from_oob, oob_buf, sizeof(uint32_t));

        if (lba_from_oob < LOGICAL_PAGES_COUNT && l2p_table[lba_from_oob] == ppa) {
            nand_read(ppa, data_buf, NULL);
            ftl_write(lba_from_oob, data_buf);
        }
    }

    if (nand_erase(victim_blk) == NAND_SUCCESS) {
        block_table[victim_blk].invalid_page_count = 0;
        block_table[victim_blk].is_free = 1; 
    }
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

int ftl_get_free_block(void) {
    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        if (block_table[i].is_free && !nand_is_bad_block(i)) {
            block_table[i].is_free = 0; 
            return i;
        }
    }

    ftl_gc();

    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        if (block_table[i].is_free && !nand_is_bad_block(i)) {
            block_table[i].is_free = 0;
            return i;
        }
    }

    return -1;
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
