#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "ftl.h"
#include "nand_hal.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Qualcomm_Candidate");
MODULE_DESCRIPTION("NAND FTL Simulator");

static int __init nand_ftl_init_module(void) {
    printk(KERN_INFO "[FTL-MOD] Loading Module...\n");

    if (ftl_init() < 0) return -ENOMEM;

    // === STRESS TEST START ===
    // 커널이 멈추지 않도록 적절한 양만 테스트 (약 2000 페이지 쓰기)
    // 실제 GC 동작을 보려면 블록 한두 개 분량 이상 써야 함.
    printk(KERN_INFO "[FTL-MOD] Starting Stress Test (Writes & GC)...\n");

    uint8_t *buf = kmalloc(NAND_PAGE_SIZE, GFP_KERNEL);
    if (!buf) { ftl_exit(); return -ENOMEM; }
    memset(buf, 0xAB, NAND_PAGE_SIZE);

    int test_count = 2000; // 충분히 돌려서 GC 유발
    for (int i = 0; i < test_count; i++) {
        uint32_t lba = i % 100; // LBA 0~99 반복 (Hot Data)
        ftl_write(lba, buf);
        
        // 너무 많은 로그 방지 (100번에 한번만 출력)
        if (i % 500 == 0) 
            printk(KERN_INFO "[FTL-MOD] Written %d pages...\n", i);
    }

    // 검증
    uint8_t *rbuf = kmalloc(NAND_PAGE_SIZE, GFP_KERNEL);
    if (rbuf) {
        ftl_read(0, rbuf);
        if (rbuf[0] == 0xAB) printk(KERN_INFO "[FTL-MOD] Verify Success: Data Matches.\n");
        else printk(KERN_ERR "[FTL-MOD] Verify Failed!\n");
        kfree(rbuf);
    }
    
    kfree(buf);
    printk(KERN_INFO "[FTL-MOD] Test Complete. Check dmesg for GC logs.\n");
    return 0;
}

static void __exit nand_ftl_exit_module(void) {
    ftl_exit();
    printk(KERN_INFO "[FTL-MOD] Unloaded.\n");
}

module_init(nand_ftl_init_module);
module_exit(nand_ftl_exit_module);
