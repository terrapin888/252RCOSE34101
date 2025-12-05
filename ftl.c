#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nand_hal.h"
#include "ftl.h"  // 여기서 ftl.h를 부릅니다

// 내부 함수 선언
static void ftl_gc(void);
static int ftl_find_victim_block(void);
static int ftl_get_free_block(void);

typedef struct {
    int invalid_page_count;
    int is_free;
} block_info_t;

static uint32_t *l2p_table = NULL;
static block_info_t *block_table = NULL;
static int current_block_index = 0;
static int current_page_index = 0;

int ftl_init(void) {
    if (nand_init() != NAND_SUCCESS) return -1;

    l2p_table = (uint32_t *)malloc(sizeof(uint32_t) * LOGICAL_PAGES_COUNT);
    memset(l2p_table, 0xFF, sizeof(uint32_t) * LOGICAL_PAGES_COUNT);

    block_table = (block_info_t *)malloc(sizeof(block_info_t) * BLOCKS_PER_CHIP);
    for(int i=0; i<BLOCKS_PER_CHIP; i++) {
        block_table[i].invalid_page_count = 0;
        block_table[i].is_free = 1;
    }
    current_block_index = 0;
    current_page_index = 0;
    block_table[0].is_free = 0;

    printf("[FTL] Init Complete. Logical Pages: %d\n", LOGICAL_PAGES_COUNT);
    return 0;
}

void ftl_write(uint32_t lba, const uint8_t *buffer) {
    if (lba >= LOGICAL_PAGES_COUNT) return;

    if (current_page_index >= PAGES_PER_BLOCK) {
        int next = ftl_get_free_block();
        if (next == -1) { printf("[Error] System Full\n"); return; }
        current_block_index = next;
        current_page_index = 0;
    }

    uint32_t target_ppa = current_block_index * PAGES_PER_BLOCK + current_page_index;
    uint32_t old_ppa = l2p_table[lba];
    if (old_ppa != 0xFFFFFFFF) block_table[old_ppa / PAGES_PER_BLOCK].invalid_page_count++;

    uint8_t spare[NAND_OOB_SIZE];
    memset(spare, 0xFF, NAND_OOB_SIZE);
    memcpy(spare, &lba, sizeof(uint32_t));

    nand_write(target_ppa, buffer, spare);
    l2p_table[lba] = target_ppa;
    current_page_index++;
}

void ftl_read(uint32_t lba, uint8_t *buffer) {
    if (lba >= LOGICAL_PAGES_COUNT) return;
    uint32_t ppa = l2p_table[lba];
    if (ppa == 0xFFFFFFFF) memset(buffer, 0xFF, NAND_PAGE_SIZE);
    else nand_read(ppa, buffer, NULL);
}

static void ftl_gc(void) {
    uint8_t data[NAND_PAGE_SIZE], oob[NAND_OOB_SIZE];
    uint32_t lba;
    int victim = ftl_find_victim_block();
    if (victim == -1) return;

    for (int i=0; i<PAGES_PER_BLOCK; i++) {
        uint32_t ppa = victim * PAGES_PER_BLOCK + i;
        nand_read(ppa, NULL, oob);
        memcpy(&lba, oob, sizeof(uint32_t));
        if (lba < LOGICAL_PAGES_COUNT && l2p_table[lba] == ppa) {
            nand_read(ppa, data, NULL);
            ftl_write(lba, data);
        }
    }
    nand_erase(victim);
    block_table[victim].invalid_page_count = 0;
    block_table[victim].is_free = 1;
}

static int ftl_find_victim_block(void) {
    int victim = -1, max = -1;
    for (int i=0; i<BLOCKS_PER_CHIP; i++) {
        if (i == current_block_index || block_table[i].is_free || nand_is_bad_block(i)) continue;
        if (block_table[i].invalid_page_count > max) {
            max = block_table[i].invalid_page_count;
            victim = i;
        }
    }
    return (max < 0) ? -1 : victim;
}

static int ftl_get_free_block(void) {
    for(int k=0; k<2; k++) {
        for(int i=0; i<BLOCKS_PER_CHIP; i++) {
            if(block_table[i].is_free && !nand_is_bad_block(i)) {
                block_table[i].is_free = 0; return i;
            }
        }
        if(k==0) ftl_gc();
    }
    return -1;
}

void ftl_exit(void) {
    if(l2p_table) free(l2p_table);
    if(block_table) free(block_table);
    nand_exit();
}
