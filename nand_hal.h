// nand_hal.h
#ifndef NAND_HAL_H
#define NAND_HAL_H

#include <stdint.h>

#define NAND_PAGE_SIZE      4096    // 4KB Main Area
#define NAND_OOB_SIZE       128     // 128B Spare Area
#define PAGES_PER_BLOCK     64
#define BLOCKS_PER_CHIP     1024

typedef uint32_t ppa_t;    // PPA (Physical Page Address)

// Error Codes
#define NAND_SUCCESS        0
#define NAND_ERR_INVALID    -1  // wrong access to addtess
#define NAND_ERR_OVERWRITE  -2  // try to overwrite
#define NAND_ERR_BADBLOCK   -3  // access to bad block
#define NAND_ERR_NOT_ERASED -4  // try to write block not erased

// Command
int nand_init(void);     // allcoate memory
int nand_read(ppa_t ppa, uint8_t *data_buf, uint8_t *oob_buf);    // read memory
int nand_write(ppa_t ppa, const uint8_t *data_buf, const uint8_t *oob_buf);    // write memory & check overwrite
int nand_erase(int block_index); // erase
void nand_exit(void); // memory free

// Debug
uint32_t nand_get_erase_count(int blcok_index);    // debug for erase count
int nand_is_bad_block(int block_index);    // check if it is bad block

#endif
