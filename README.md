# Virtual SSD Simulator with Log-Structured FTL

## Project Overview
This project is a Virtual SSD Simulator implemented in C, designed to emulate the physical characteristics of NAND Flash memory and simulate the operation of a Log-Structured Flash Translation Layer (FTL).

The primary goal is to verify firmware algorithms such as Logical-to-Physical (L2P) mapping, Garbage Collection (GC), and Block Management in a User Space environment, overcoming the physical constraints of raw NAND flash.

## Key Features

### 1. NAND Flash Emulation (HAL Layer)
* **Virtual Hardware Abstraction Layer (nand_hal.c)**: Simulates NAND behavior in RAM.
* **Physical Constraints Implemented**:
  * **Erase-before-Write**: Returns an error if attempting to overwrite a non-empty page.
  * **Page-Unit Read/Write**: Operates on 4KB page units.
  * **Block-Unit Erase**: Operates on 256KB block units.
  * **OOB (Out-of-Band) Area**: Simulates spare area (128B) for storing metadata like LBA.

### 2. Log-Structured FTL Algorithm
* **Append-Only Strategy**: Writes data sequentially to new pages to handle the "no-overwrite" property of NAND.
* **Page-Level Mapping**: Manages address translation using an L2P (Logical-to-Physical) table.
* **Garbage Collection (GC)**:
  * **Trigger**: Automatically triggered when free blocks are exhausted.
  * **Policy**: Uses a Greedy Policy to select the victim block with the most invalid pages.
  * **Valid Page Copy-back**: Reads valid data from the victim block and rewrites it to the active block before erasure.

### 3. Stress Testing & Reliability
* **Circular Buffer Operation**: Verified that the system continues to operate without failure even after writing data exceeding the total physical capacity.
* **Data Integrity Check**: Confirmed that the last written data matches the read data after thousands of GC cycles.

## System Architecture

The system consists of three distinct layers to ensure modularity.

```mermaid
graph TD
    User[User Test Scenario / main.c] -->|Read/Write LBA| FTL[Flash Translation Layer]
    
    subgraph FTL Core
    FTL -->|Address Translation| Map[L2P Mapping Table]
    FTL -->|Block Management| BlockInfo[Block Status Table]
    FTL -->|Reclaim Space| GC[Garbage Collector]
    end

    FTL -->|Physical Operations| HAL[NAND HAL / Virtual HW]
    
    subgraph Virtual NAND Device
    HAL -->|Page Write / Read| Pages[Page Array]
    HAL -->|Block Erase| Blocks[Block Array]
    end
