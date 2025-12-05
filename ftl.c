#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>    // kmalloc
#include <linux/vmalloc.h> // vmalloc
#include <linux/string.h>
#include "nand_hal.h"
#include "ftl.h"

// --------------------------------------------------------
// Internal Functions Prototypes
// --------------------------------------------------------
static void ftl_gc(void);
static int ftl_find_victim_block(void);
static int ftl_get_free_block(void);

// --------------------------------------------------------
// Structs & Globals
// --------------------------------------------------------
typedef struct {
    int invalid_page_count; // garbage page count
    int is_free;            // 1: Free, 0: Active/Used
} block_info_t;

static uint32_t *l2p_table = NULL;    // Mapping Table (Large)
static block_info_t *block_table = NULL; // Block Info Table
static int current_block_index = 0;
static int current_page_index = 0;

// --------------------------------------------------------
// FTL Functions
// --------------------------------------------------------

// Initialize
int ftl_init(void) {
    if (nand_init() != NAND_SUCCESS) return -EIO;

    // 1. L2P Table 할당 (Size: 60000 * 4B = 약 240KB)
    // 크기가 크므로 vmalloc 사용 권장
    unsigned long l2p_size = sizeof(uint32_t) * LOGICAL_PAGES_COUNT;
    l2p_table = (uint32_t *)vmalloc(l2p_size);
    if (!l2p_table) {
        printk(KERN_ERR "[FTL] Failed to vmalloc L2P table\n");
        return -ENOMEM;
    }
    memset(l2p_table, 0xFF, l2p_size); // 0xFF로 초기화

    // 2. Block Table 할당
    unsigned long blk_table_size = sizeof(block_info_t) * BLOCKS_PER_CHIP;
    block_table = (block_info_t *)vmalloc(blk_table_size);
    if (!block_table) {
        printk(KERN_ERR "[FTL] Failed to vmalloc Block table\n");
        vfree(l2p_table);
        return -ENOMEM;
    }
    
    // 블록 초기화
    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        block_table[i].invalid_page_count = 0;
        block_table[i].is_free = 1; 
    }

    // 0번 블록 할당
    current_block_index = 0;
    current_page_index = 0;
    block_table[0].is_free = 0; 

    printk(KERN_INFO "[FTL] Initialized. Logical Pages: %d\n", LOGICAL_PAGES_COUNT);
    return 0;
}

// Read
void ftl_read(uint32_t lba, uint8_t *buffer) {
    if (lba >= LOGICAL_PAGES_COUNT) {
        printk(KERN_ERR "[FTL] Read LBA out of range: %d\n", lba);
        return;
    }

    uint32_t ppa = l2p_table[lba];

    if (ppa == 0xFFFFFFFF) {
        memset(buffer, 0xFF, NAND_PAGE_SIZE);
        return;
    }

    nand_read(ppa, buffer, NULL); 
}

// Write
void ftl_write(uint32_t lba, const uint8_t *buffer) {
    if (lba >= LOGICAL_PAGES_COUNT) {
        printk(KERN_ERR "[FTL] Write LBA out of range: %d\n", lba);
        return;
    }

    // 1. 블록 꽉 참 확인 -> 새 블록 할당 (GC 포함)
    if (current_page_index >= PAGES_PER_BLOCK) {
        int next_block = ftl_get_free_block();
        
        if (next_block == -1) {
            printk(KERN_CRIT "[FTL] System Full! Write Failed LBA %d\n", lba);
            return;
        }
        current_block_index = next_block;
        current_page_index = 0;
    }

    uint32_t target_ppa = current_block_index * PAGES_PER_BLOCK + current_page_index;

    // 2. 기존 데이터 무효화
    uint32_t old_ppa = l2p_table[lba];
    if (old_ppa != 0xFFFFFFFF) {
        int old_block = old_ppa / PAGES_PER_BLOCK;
        block_table[old_block].invalid_page_count++;
    }

    // 3. NAND 쓰기 (OOB에 LBA 기록)
    uint8_t spare_buf[NAND_OOB_SIZE];
    memset(spare_buf, 0xFF, NAND_OOB_SIZE);
    memcpy(spare_buf, &lba, sizeof(uint32_t)); 

    int ret = nand_write(target_ppa, buffer, spare_buf);
    
    if (ret == NAND_SUCCESS) {
        l2p_table[lba] = target_ppa;
        current_page_index++;
    } else {
        printk(KERN_ERR "[FTL] NAND Write Failed PPA %d\n", target_ppa);
    }
}

// Garbage Collection
static void ftl_gc(void) {
    uint8_t *data_buf;
    uint8_t *oob_buf;
    uint32_t lba_from_oob;

    // 커널 스택 오버플로우 방지를 위해 버퍼도 동적 할당 권장 (또는 kmalloc)
    // 여기서는 간단히 kmalloc 사용
    data_buf = kmalloc(NAND_PAGE_SIZE, GFP_KERNEL);
    oob_buf = kmalloc(NAND_OOB_SIZE, GFP_KERNEL);
    
    if (!data_buf || !oob_buf) {
        printk(KERN_ERR "[FTL GC] Memory allocation failed\n");
        if(data_buf) kfree(data_buf);
        if(oob_buf) kfree(oob_buf);
        return;
    }

    int victim_blk = ftl_find_victim_block();
    if (victim_blk == -1) {
        kfree(data_buf);
        kfree(oob_buf);
        return;
    }

    // Valid Page Copy
    for (int page = 0; page < PAGES_PER_BLOCK; page++) {
        uint32_t ppa = victim_blk * PAGES_PER_BLOCK + page;
        nand_read(ppa, NULL, oob_buf);
        memcpy(&lba_from_oob, oob_buf, sizeof(uint32_t));

        if (lba_from_oob < LOGICAL_PAGES_COUNT && l2p_table[lba_from_oob] == ppa) {
            nand_read(ppa, data_buf, NULL);
            ftl_write(lba_from_oob, data_buf); // Copy-back
        }
    }

    // Erase
    if (nand_erase(victim_blk) == NAND_SUCCESS) {
        block_table[victim_blk].invalid_page_count = 0;
        block_table[victim_blk].is_free = 1; 
        // printk(KERN_INFO "[FTL GC] Block %d Reclaimed\n", victim_blk);
    }

    kfree(data_buf);
    kfree(oob_buf);
}

// Victim 선정
static int ftl_find_victim_block(void) {
    int victim_block = -1;
    int max_invalid = -1;

    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        if (i == current_block_index) continue;
        if (nand_is_bad_block(i)) continue;
        if (block_table[i].is_free) continue; // 이미 빈 건 제외

        if (block_table[i].invalid_page_count > max_invalid) {
            max_invalid = block_table[i].invalid_page_count;
            victim_block = i;
        }
    }

    if (max_invalid < 0) return -1;
    return victim_block;
}

// Free Block 찾기
static int ftl_get_free_block(void) {
    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        if (block_table[i].is_free && !nand_is_bad_block(i)) {
            block_table[i].is_free = 0; 
            return i;
        }
    }

    ftl_gc(); // GC Trigger

    for (int i = 0; i < BLOCKS_PER_CHIP; i++) {
        if (block_table[i].is_free && !nand_is_bad_block(i)) {
            block_table[i].is_free = 0;
            return i;
        }
    }

    return -1;
}

// Exit (Memory Free)
void ftl_exit(void) {
    if (l2p_table) {
        vfree(l2p_table);
        l2p_table = NULL;
    }

    if (block_table) {
        vfree(block_table);
        block_table = NULL;
    }
    nand_exit(); // HAL cleanup
    printk(KERN_INFO "[FTL] Exit & Memory Freed.\n");
}

// Debug Print
void ftl_print_map(uint32_t lba) {
    if (lba < LOGICAL_PAGES_COUNT) {
        uint32_t ppa = l2p_table[lba];
        if (ppa == 0xFFFFFFFF) {
            printk(KERN_INFO "LBA %d -> Unmapped\n", lba);
        } else {
            printk(KERN_INFO "LBA %d -> PPA %d (Blk %d, Pg %d)\n", 
                lba, ppa, ppa / PAGES_PER_BLOCK, ppa % PAGES_PER_BLOCK);
        }
    }
}