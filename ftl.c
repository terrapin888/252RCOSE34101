#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nand_hal.h"
#include "ftl.h"

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

    // L2P 테이블 할당
    l2p_table = (uint32_t *)malloc(sizeof(uint32_t) * LOGICAL_PAGES_COUNT);
    memset(l2p_table, 0xFF, sizeof(uint32_t) * LOGICAL_PAGES_COUNT); // -1로 초기화

    // 블록 정보 테이블 할당
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

void ftl_read(uint32_t lba, uint8_t *buffer) {
    if (lba >= LOGICAL_PAGES_COUNT) {
        printf("[Error] Read Range\n");
        return;
    }
    uint32_t ppa = l2p_table[lba];

    if (ppa == 0xFFFFFFFF) {
        memset(buffer, 0xFF, NAND_PAGE_SIZE); // 맵핑 안됐으면 0xFF
    } else {
        nand_read(ppa, buffer, NULL);
    }
}

void ftl_write(uint32_t lba, const uint8_t *buffer) {
    if (lba >= LOGICAL_PAGES_COUNT) return;

    // 1. 블록 꽉 참 -> 새 블록 할당 (GC 자동 수행)
    if (current_page_index >= PAGES_PER_BLOCK) {
        int next_block = ftl_get_free_block();
        if (next_block == -1) {
            printf("[Critical] System Full! Write LBA %d Failed.\n", lba);
            return;
        }
        current_block_index = next_block;
        current_page_index = 0;
        // printf("[FTL] Switched to Block %d\n", current_block_index);
    }

    // 2. 주소 계산 및 기존 데이터 무효화
    uint32_t target_ppa = current_block_index * PAGES_PER_BLOCK + current_page_index;
    uint32_t old_ppa = l2p_table[lba];

    if (old_ppa != 0xFFFFFFFF) {
        block_table[old_ppa / PAGES_PER_BLOCK].invalid_page_count++;
    }

    // 3. OOB에 LBA 저장 (GC용)
    uint8_t spare[NAND_OOB_SIZE];
    memset(spare, 0xFF, NAND_OOB_SIZE);
    memcpy(spare, &lba, sizeof(uint32_t));

    // 4. 쓰기 및 매핑 갱신
    if (nand_write(target_ppa, buffer, spare) == NAND_SUCCESS) {
        l2p_table[lba] = target_ppa;
        current_page_index++;
    }
}

// === Garbage Collection ===
static void ftl_gc(void) {
    uint8_t data[NAND_PAGE_SIZE];
    uint8_t oob[NAND_OOB_SIZE];
    uint32_t lba_check;

    int victim = ftl_find_victim_block();
    if (victim == -1) return;

    // printf("[GC] Cleaning Block %d (Invalid: %d)\n", victim, block_table[victim].invalid_page_count);

    // Valid Page Copy-back
    for (int i=0; i<PAGES_PER_BLOCK; i++) {
        uint32_t ppa = victim * PAGES_PER_BLOCK + i;
        nand_read(ppa, NULL, oob);
        memcpy(&lba_check, oob, sizeof(uint32_t));

        // 매핑 정보가 일치하면 유효한 데이터
        if (lba_check < LOGICAL_PAGES_COUNT && l2p_table[lba_check] == ppa) {
            nand_read(ppa, data, NULL);
            ftl_write(lba_check, data); // 재귀적 호출로 새 블록에 씀
        }
    }

    // Erase
    nand_erase(victim);
    block_table[victim].invalid_page_count = 0;
    block_table[victim].is_free = 1;
}

static int ftl_find_victim_block(void) {
    int victim = -1, max_invalid = -1;
    for (int i=0; i<BLOCKS_PER_CHIP; i++) {
        // 현재 사용중, 빈 블록, 배드 블록 제외
        if (i == current_block_index || block_table[i].is_free || nand_is_bad_block(i)) continue;
        
        if (block_table[i].invalid_page_count > max_invalid) {
            max_invalid = block_table[i].invalid_page_count;
            victim = i;
        }
    }
    return (max_invalid < 0) ? -1 : victim;
}

static int ftl_get_free_block(void) {
    for(int k=0; k<2; k++) { // 1차 시도 -> 실패시 GC -> 2차 시도
        for(int i=0; i<BLOCKS_PER_CHIP; i++) {
            if(block_table[i].is_free && !nand_is_bad_block(i)) {
                block_table[i].is_free = 0; 
                return i;
            }
        }
        if(k==0) ftl_gc(); // GC 호출!
    }
    return -1;
}

void ftl_exit(void) {
    if(l2p_table) free(l2p_table);
    if(block_table) free(block_table);
    nand_exit();
}