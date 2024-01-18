/*
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __FTL_FAST_MAPPING__
#define __FTL_FAST_MAPPING__

#include <cinttypes>
#include <unordered_map>
#include <vector>
#include <deque>
#include <optional>

#include "ftl/abstract_ftl.hh"
#include "ftl/common/block_fast.hh"
#include "ftl/ftl.hh"
#include "pal/pal.hh"

namespace SimpleSSD {

namespace FTL {

/* 

# Data Structure:
- `blockMappingTable`: map logical block number -> physical block number
- `blocks`: physical block number -> block info.
- `SWBlock`: The SW log block.
- `RWBlocks`: All RW log blocks.

# Invariance:
- For every logical page number, there should be at most one valid physical page
  - This page should be found at 
    1. the coresponding location by the block in blockMappingTable
    2. The SW log block or RW log blocks.
- We don't promise a sequencial writing order, even in SW log blocks.

# Algorithm design:

## read:
1. check block mapping table, suppose block pbn X.
  - no mapped: no-data panic

2. check the corresponding page status:
  - if valid, just return
  - if erased && invalid: no-data panic
  - if invalid: check log blocks.
  
3. If not found: otherwise: no-data panic


## write (a page):
Follow the psuedo code in the paper.

1. Check if this page can insert to block_mapping_table[lbn] directly.

2. Otherwise, there are two situations:
  1. offset = 0 (the beginning of a block)
    Merge and recycle the current SW log block.
    Create a new one that begins with the page inserted.

  2. offset != 0
    1. if the current logical owner of SW log block contains the page inserted. 
      Try to insert to the corresponding location of the SW log block.
      If conflict, merge the current SW log block, the original data block
       (and the page inserted).
    2. if not
      Try to write to RW log block. 
      If no free page, then recyle one of RW log block (in round robin style), 
      and create a new one.      

*/

class FastMapping : public AbstractFTL {
 private:
  PAL::PAL *pPAL;

  ConfigReader &conf;

  // logical block number -> physical block number
  std::vector<std::optional<uint32_t>>
      logicalToPhysicalBlockMapping; 
  
  std::vector<std::optional<uint32_t>>
      physicalToLogicalBlockMapping;

  // physical block number -> block info
  std::vector<BlockFast> physicalBlocks; // in use
  std::deque<uint32_t> freeBlocks;

  std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>>
    RWlogMapping; // logical page number -> (physical block number, page index)


  const uint32_t kRWBlockCnt = 6;
  std::optional<uint32_t> SWBlock;
  std::deque<uint32_t> RWBlocks;

  struct {
    uint64_t reclaimedBlocks;
    uint64_t validSuperPageCopies;
    uint64_t validPageCopies;
  } stat;

  float freeBlockRatio();
  uint32_t getFreeBlock();

  float calculateWearLeveling();
  void calculateTotalPages(uint64_t &, uint64_t &);

  void readInternal(Request &, uint64_t &);
  void writeInternal(Request &, uint64_t &, bool = true);

  enum BlockType{
    kBlockTypeData,
    kBlockTypeSW,
    kBlockTypeRW,
    kBlockTypeUnknown
  };

  // sometimes we merge the current writing page with previous data.
  void mergeLogBlock(uint32_t physicalBlockNumber, enum BlockType blockType, 
    std::optional<uint32_t> additionalPage, uint64_t &tick, PAL::Request &req, bool sendToPAL);

  // find the only one valid page
  // if found return true, otherwise return false
  bool findValidPage(uint32_t lpn, uint32_t &pbn, uint32_t &pageIdx, 
    BlockFast *&pBlock, enum BlockType &blockType);

  void eraseInternal(uint32_t physicalBlockNum, uint64_t &tick, bool sendToPAL);

 public:
  FastMapping(ConfigReader &, Parameter &, PAL::PAL *, DRAM::AbstractDRAM *);
  ~FastMapping();

  bool initialize() override;

  uint32_t convertPageToBlock(uint64_t);
  uint64_t convertPageToOffsetInBlock(uint64_t);

  void read(Request &, uint64_t &) override;
  void write(Request &, uint64_t &) override;
  void trim(Request &, uint64_t &) override;

  void format(LPNRange &, uint64_t &) override;

  Status *getStatus(uint64_t, uint64_t) override;

  void getStatList(std::vector<Stats> &, std::string) override;
  void getStatValues(std::vector<double> &) override;
  void resetStatValues() override;
};

}  // namespace FTL

}  // namespace SimpleSSD

#endif
