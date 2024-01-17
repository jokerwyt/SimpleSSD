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

#include "ftl/fast_mapping.hh"

#include <algorithm>
#include <limits>
#include <random>

#include "util/algorithm.hh"
#include "util/bitset.hh"
#include "fast_mapping.hh"

namespace SimpleSSD {

namespace FTL {


FastMapping::FastMapping(ConfigReader &c, Parameter &p, PAL::PAL *l,
                         DRAM::AbstractDRAM *d)
    : AbstractFTL(p, l, d),
      pPAL(l),
      conf(c) {

  assert(param.totalPhysicalBlocks > 1u + this->kRWBlockCnt);
        
  this->physicalBlocks.reserve(param.totalPhysicalBlocks);
  this->logicalToPhysicalBlockMapping.resize(param.totalLogicalBlocks);
  this->physicalToLogicalBlockMapping.resize(param.totalPhysicalBlocks);


  // init physicalBlocks
  for (uint32_t i = 0; i < param.totalPhysicalBlocks; i++) {
    this->physicalBlocks.emplace_back(Block(i, param.pagesInBlock, param.ioUnitInPage));
  }
  
  this->SWBlock = 0;
  for (uint32_t i = 1; i <= this->kRWBlockCnt; i++) {
    this->RWBlocks.emplace_back(i);
  }
  for (uint32_t i = this->kRWBlockCnt + 1; i < param.totalPhysicalBlocks; i++) {
    freeBlocks.emplace_back(i);
  }

  status.totalLogicalPages = param.totalLogicalBlocks * param.pagesInBlock;
  
  memset(&stat, 0, sizeof(stat));

  bool bRandomTweak = conf.readBoolean(CONFIG_FTL, FTL_USE_RANDOM_IO_TWEAK);
  assert(bRandomTweak == false);
}

FastMapping::~FastMapping() {}

bool FastMapping::initialize() {
  uint64_t nPagesToWarmup;
  uint64_t nPagesToInvalidate;
  uint64_t nTotalLogicalPages;
  uint64_t tick;
  uint64_t valid;
  uint64_t invalid;
  FILLING_MODE mode;

  Request req(param.ioUnitInPage);

  debugprint(LOG_FTL_FAST_MAPPING, "Initialization started");

  nTotalLogicalPages = param.totalLogicalBlocks * param.pagesInBlock;
  nPagesToWarmup =
      nTotalLogicalPages * conf.readFloat(CONFIG_FTL, FTL_FILL_RATIO);
  nPagesToInvalidate =
      nTotalLogicalPages * conf.readFloat(CONFIG_FTL, FTL_INVALID_PAGE_RATIO);

  // Warn: the feature of pre-invalidating pages is not tested yet.
  assert(nPagesToInvalidate == 0); 

  mode = (FILLING_MODE)conf.readUint(CONFIG_FTL, FTL_FILLING_MODE);

  debugprint(LOG_FTL_FAST_MAPPING, "Total logical pages: %" PRIu64,
             nTotalLogicalPages);
  debugprint(LOG_FTL_FAST_MAPPING,
             "Total logical pages to fill: %" PRIu64 " (%.2f %%)",
             nPagesToWarmup, nPagesToWarmup * 100.f / nTotalLogicalPages);
  debugprint(LOG_FTL_FAST_MAPPING,
             "Total invalidated pages to create: %" PRIu64 " (%.2f %%)",
             nPagesToInvalidate,
             nPagesToInvalidate * 100.f / nTotalLogicalPages);

  req.ioFlag.set();

  // Step 1. Filling
  if (mode == FILLING_MODE_0 || mode == FILLING_MODE_1) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      tick = 0;
      req.lpn = i;
      writeInternal(req, tick, false);
    }
  }
  else {
    // Random
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

    for (uint64_t i = 0; i < nPagesToWarmup; i++) {
      tick = 0;
      req.lpn = dist(gen);
      writeInternal(req, tick, false);
    }
  }

  // Step 2. Invalidating
  if (mode == FILLING_MODE_0) {
    // Sequential
    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = i;
      writeInternal(req, tick, false);
    }
  }
  else if (mode == FILLING_MODE_1) {
    // Random
    // We can successfully restrict range of LPN to create exact number of
    // invalid pages because we wrote in sequential mannor in step 1.
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nPagesToWarmup - 1);

    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = dist(gen);
      writeInternal(req, tick, false);
    }
  }
  else {
    // Random
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist(0, nTotalLogicalPages - 1);

    for (uint64_t i = 0; i < nPagesToInvalidate; i++) {
      tick = 0;
      req.lpn = dist(gen);
      writeInternal(req, tick, false);
    }
  }

  // Report
  calculateTotalPages(valid, invalid);
  debugprint(LOG_FTL_FAST_MAPPING, "Filling finished. Page status:");
  debugprint(LOG_FTL_FAST_MAPPING,
             "  Total valid physical pages: %" PRIu64
             " (%.2f %%, target: %" PRIu64 ", error: %" PRId64 ")",
             valid, valid * 100.f / nTotalLogicalPages, nPagesToWarmup,
             (int64_t)(valid - nPagesToWarmup));
  debugprint(LOG_FTL_FAST_MAPPING,
             "  Total invalid physical pages: %" PRIu64
             " (%.2f %%, target: %" PRIu64 ", error: %" PRId64 ")",
             invalid, invalid * 100.f / nTotalLogicalPages, nPagesToInvalidate,
             (int64_t)(invalid - nPagesToInvalidate));
  debugprint(LOG_FTL_FAST_MAPPING, "Initialization finished");

  return true;
}

uint32_t FastMapping::convertPageToBlock(uint64_t page_number) {
  return page_number / param.pagesInBlock;
}

uint64_t FastMapping::convertPageToOffsetInBlock(uint64_t page_number) {
  return page_number % param.pagesInBlock;
}

void FastMapping::read(Request &req, uint64_t &tick) {
  uint64_t begin = tick;

  if (req.ioFlag.count() > 0) {
    readInternal(req, tick);

    debugprint(LOG_FTL_FAST_MAPPING,
               "READ  | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
               ")",
               req.lpn, begin, tick, tick - begin);
  }
  else {
    warn("FTL got empty request");
  }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::READ);
}

void FastMapping::write(Request &req, uint64_t &tick) {
  uint64_t begin = tick;

  if (req.ioFlag.count() > 0) {
    writeInternal(req, tick);

    debugprint(LOG_FTL_FAST_MAPPING,
               "WRITE | LPN %" PRIu64 " | %" PRIu64 " - %" PRIu64 " (%" PRIu64
               ")",
               req.lpn, begin, tick, tick - begin);
  }
  else {
    warn("FTL got empty request");
  }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE);
}

void FastMapping::trim(Request &req, uint64_t &tick) {
  (void) req, (void) tick;
  assert(0); // not implemented yet.
}

void FastMapping::format(LPNRange &range, uint64_t &tick) {
  (void) range, (void) tick;
  assert(0); // not implemented yet.
}

Status *FastMapping::getStatus(uint64_t lpnBegin, uint64_t lpnEnd) {
  (void) lpnBegin, (void) lpnEnd;
  assert(0); // not implemented yet.
}

float FastMapping::freeBlockRatio() {
  return (float)freeBlocks.size() / param.totalPhysicalBlocks;
}

void FastMapping::eraseInternal(uint32_t physicalBlockNum, uint64_t &tick, bool sendToPAL) {

  
  Block &block = physicalBlocks[physicalBlockNum];
  block.erase();

  if (sendToPAL) {
    PAL::Request req(1);
    req.blockIndex = physicalBlockNum;
    req.pageIndex = 0;
    req.ioFlag.set();
    pPAL->erase(req, tick); // tick += ...
  }

  physicalToLogicalBlockMapping[physicalBlockNum] = std::nullopt;

  // static uint64_t threshold =
  //     conf.readUint(CONFIG_FTL, FTL_BAD_BLOCK_THRESHOLD);
  // wear leveling check: disable now.
  // if (block.getEraseCount() < threshold) {
  //   freeBlocks.emplace_back(physicalBlockNum);
  // }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::ERASE_INTERNAL);
}

uint32_t FastMapping::getFreeBlock() {
  if (freeBlocks.empty()) {
    panic("No free block");
  }

  auto ret = freeBlocks.front();
  freeBlocks.pop_front();
  return ret;
}

void FastMapping::readInternal(Request &req, uint64_t &tick) {
  PAL::Request palRequest(req);

  uint32_t pbn;
  uint32_t pageIdx;
  Block *pBlock;
  BlockType blockType;

  if (findValidPage(req.lpn, pbn, pageIdx, pBlock, blockType)) {
    pBlock->read(pageIdx, 0, tick); // tick stays

    palRequest.blockIndex = pbn;
    palRequest.pageIndex = pageIdx;
    palRequest.ioFlag = req.ioFlag;

    pPAL->read(palRequest, tick); // tick += ...
  } else {
    // panic("No valid page found");
    // page_mapping don't panic, so we do nothing here.
  }

  tick += applyLatency(CPU::FTL__PAGE_MAPPING, CPU::READ_INTERNAL);
}

void FastMapping::writeInternal(Request &req, uint64_t &tick, bool sendToPAL) {
  PAL::Request palRequest(req);
  uint64_t finishedAt = tick;

  if (req.reqID >= 1025) {
    int kkk = tick;
    (void) kkk;
  }

  auto logicalBlockNumber = convertPageToBlock(req.lpn);
  auto &block_opt = logicalToPhysicalBlockMapping[logicalBlockNumber];

  if (block_opt.has_value() == false) {
    // create new logical-physical block mapping
    uint32_t pbn = getFreeBlock();
    block_opt = pbn;
    physicalToLogicalBlockMapping[pbn] = logicalBlockNumber;
  }

  uint32_t physicalBlockNumber;
  uint32_t pageIndex;
  Block *pBlock;
  BlockType blockType;

  if (!this->findValidPage(req.lpn, physicalBlockNumber, pageIndex, pBlock, blockType)) {
    // no valid page. just write into data block.
    physicalBlockNumber = block_opt.value();
    pageIndex = this->convertPageToOffsetInBlock(req.lpn);
    pBlock = &physicalBlocks[physicalBlockNumber];
    blockType = kBlockTypeData;

    pBlock->write(pageIndex, req.lpn, 0, tick); // tick stays

    if (sendToPAL) {
      palRequest.blockIndex = physicalBlockNumber;
      palRequest.pageIndex = pageIndex;
      palRequest.ioFlag = req.ioFlag;

      pPAL->write(palRequest, finishedAt); // finishedAt += ...
    }
  } else {
    // there is a valid page. we should invalidate the old page, 
    // and create a new one in log block. this process may involve merging and erasing.

    // no matter where the valid page is, we should invalidate it first

    if (pBlock->getBlockIndex() == 9120 && pageIndex == 0) {
      int kkk = pageIndex;
      (void) kkk;
    }

    pBlock->invalidate(pageIndex, tick); // tick stays
    if (blockType == kBlockTypeRW) {
      // remove the original RW mapping
      this->RWlogMapping.erase(req.lpn);
    }

    // follow the pseudo code in the paper
    uint32_t pageOffset = this->convertPageToOffsetInBlock(req.lpn);

    if (pageOffset == 0) {
      // It's a beginning of one page. 
      assert(SWBlock.has_value());
      Block *swblock = &physicalBlocks[SWBlock.value()];

      // Check if sw block is a clean block.
      if (!swblock->isCleanBlock()) {
        // If it is not, we should create a new SW log block
        // recycle the old SW block first.

        // merging is parrellel with writing the new SW block.
        auto startTick = tick;
        mergeLogBlock(SWBlock.value(), kBlockTypeSW, std::nullopt, startTick, palRequest, sendToPAL); // startTick += ...
        finishedAt = std::max(finishedAt, startTick);

        SWBlock = getFreeBlock();
        // phy to logical mapping changed below.

        swblock = &physicalBlocks[SWBlock.value()];
      }

      // write to the first page of SW block
      swblock->write(0, req.lpn, 0, tick); // tick stays

      // the first page should claim the ownership of the SW block.
      physicalToLogicalBlockMapping[SWBlock.value()] = logicalBlockNumber;


      if (sendToPAL) {
        palRequest.blockIndex = SWBlock.value();
        palRequest.pageIndex = 0;
        palRequest.ioFlag = req.ioFlag;

        auto startTick = tick;
        pPAL->write(palRequest, startTick); // startTick += ...
        finishedAt = std::max(finishedAt, startTick);
      }
    } else {
      auto SWBlockOwner_opt = physicalToLogicalBlockMapping[SWBlock.value()];

      if (SWBlockOwner_opt.has_value() && SWBlockOwner_opt.value() == logicalBlockNumber) {
        // write into SW log block, or start merging...
        Block &swblock = physicalBlocks[SWBlock.value()];
        // We hope to see appending, but we still allow out-of-order page writing.

        uint64_t lpn;
        bool valid, erased;

        swblock.getPageInfo(pageOffset, lpn, valid, erased);

        if (erased) {
          // clean page, just write into it.
          swblock.write(pageOffset, req.lpn, 0, tick); // tick stays

          if (sendToPAL) {
            palRequest.blockIndex = SWBlock.value();
            palRequest.pageIndex = pageOffset;
            palRequest.ioFlag = req.ioFlag;

            auto startTick = tick;
            pPAL->write(palRequest, startTick); // tick += ...
            finishedAt = std::max(finishedAt, startTick);
          }
        } else {
          // merge: 
          // the current SW log block, 
          // the original data block, 
          // and the writing page.
          auto startTick = tick;
          mergeLogBlock(SWBlock.value(), kBlockTypeSW, pageOffset, startTick, palRequest, sendToPAL); // startTick += ...
          finishedAt = std::max(finishedAt, startTick);
        }
      } else {
        // two case: 
        // 1. SW Block has no owner
        // 2. SW Block has another owner
        // for both cases, we write into RW log block instead. 

        // find a free RW log block
        uint32_t freeRwBlockNumber = std::numeric_limits<uint32_t>::max();

        for (uint32_t rwBlockNum : RWBlocks) {
          Block &rwblock = physicalBlocks[rwBlockNum];
          if (rwblock.getErasedPageCount() != 0) {
            freeRwBlockNumber = rwBlockNum;
            break;
          }
        }

        if (freeRwBlockNumber == std::numeric_limits<uint32_t>::max()) {
          // no free RW log block, we should recycle one.

          uint32_t victimRwBlock = RWBlocks.front(); 
          RWBlocks.pop_front();

          // merge is parrellel with writing the new RW block.
          auto startTick = tick;
          mergeLogBlock(victimRwBlock, kBlockTypeRW, std::nullopt, startTick, palRequest, sendToPAL); // startTick += ...
          finishedAt = std::max(finishedAt, startTick); 

          freeRwBlockNumber = getFreeBlock();
          RWBlocks.push_back(freeRwBlockNumber);

          physicalToLogicalBlockMapping[freeRwBlockNumber] = std::nullopt;
        }

        // write into rwblock
        Block &rwblock = physicalBlocks[freeRwBlockNumber];
        auto nextFreePage = rwblock.getNextWritePageIndex(0);

        rwblock.write(nextFreePage, req.lpn, 0, tick); // tick stays
        RWlogMapping[req.lpn] = std::make_pair(freeRwBlockNumber, nextFreePage);

        if (sendToPAL) {
          palRequest.blockIndex = freeRwBlockNumber;
          palRequest.pageIndex = nextFreePage;
          palRequest.ioFlag = req.ioFlag;

          auto startTick = tick;
          pPAL->write(palRequest, startTick); // startTick += ...
          finishedAt = std::max(finishedAt, startTick);
        }
      }
    }
  }


  tick = finishedAt + applyLatency(CPU::FTL__PAGE_MAPPING, CPU::WRITE_INTERNAL);
}



void FastMapping::mergeLogBlock(uint32_t logBlockPhyNum,
    enum BlockType blockType, std::optional<uint32_t> additionalPage, uint64_t &tick, PAL::Request &req, bool sendToPAL) {

  std::vector<PAL::Request> readRequests;
  std::vector<std::pair<PAL::Request, uint64_t>> writeRequests; // pair: request, lpn
  std::vector<PAL::Request> eraseRequests;

  Block &logBlock = physicalBlocks[logBlockPhyNum];
  if (blockType == kBlockTypeRW) {
    assert(additionalPage.has_value() == false);
    
    std::vector<uint32_t> logicalBlocks;
    std::unordered_map<uint32_t, uint32_t> lbnToNewPbn, lbnToOldPbn;

    for (uint32_t i = 0; i < param.pagesInBlock; i++) {
      uint64_t lpn;
      bool valid, erased;
      logBlock.getPageInfo(i, lpn, valid, erased);

      if (valid) {
        logicalBlocks.emplace_back(convertPageToBlock(lpn));

        // remove RW log mapping
        RWlogMapping.erase(lpn);
      }
    }

    // sort and remove duplicates
    std::sort(logicalBlocks.begin(), logicalBlocks.end());
    logicalBlocks.erase(std::unique(logicalBlocks.begin(), logicalBlocks.end()), logicalBlocks.end());

    // assign new physical block number to each logical block
    for (auto lbn : logicalBlocks) {
      uint32_t newBlockPhyBlockNum = getFreeBlock();
      lbnToNewPbn[lbn] = newBlockPhyBlockNum;
      lbnToOldPbn[lbn] = logicalToPhysicalBlockMapping[lbn].value();

      physicalToLogicalBlockMapping[newBlockPhyBlockNum] = lbn;
      logicalToPhysicalBlockMapping[lbn] = newBlockPhyBlockNum;
    }

    for (auto lbn : logicalBlocks) {
      // old data blocks:
      uint32_t pbn = lbnToOldPbn[lbn];
      Block &block = physicalBlocks[pbn];

      // read from old data block, write to the new one
      for (uint32_t i = 0; i < param.pagesInBlock; i++) {
        if (block.isValid(i)) {
          PAL::Request readReq(req);
          readReq.blockIndex = pbn;
          readReq.pageIndex = i;
          readReq.ioFlag.set();
          readRequests.emplace_back(readReq);

          PAL::Request writeReq(req);
          writeReq.blockIndex = lbnToNewPbn[lbn];
          writeReq.pageIndex = i;
          writeReq.ioFlag.set();
          writeRequests.emplace_back(std::make_pair(writeReq, lbn * param.pagesInBlock + i));
        }
      }

      // erase old data block
      PAL::Request eraseReq(req);
      eraseReq.blockIndex = pbn;
      eraseReq.ioFlag.set();
      eraseRequests.emplace_back(eraseReq);
    }

    // read from victim RW log block, write to the new one
    for (uint32_t i = 0; i < param.pagesInBlock; i++) {
      if (logBlock.isValid(i)) {
        PAL::Request readReq(req);
        readReq.blockIndex = logBlockPhyNum;
        readReq.pageIndex = i;
        readReq.ioFlag.set();
        readRequests.emplace_back(readReq);

        PAL::Request writeReq(req);
        writeReq.blockIndex = lbnToNewPbn[convertPageToBlock(logBlock.getLPN(i))];
        writeReq.pageIndex = convertPageToOffsetInBlock(logBlock.getLPN(i));
        writeReq.ioFlag.set();
        writeRequests.emplace_back(std::make_pair(writeReq, logBlock.getLPN(i)));
      }
    }

    // erase old RW block
    PAL::Request eraseReq(1);
    eraseReq.blockIndex = logBlockPhyNum;
    eraseReq.ioFlag.set();
    eraseRequests.emplace_back(eraseReq);

    // handle new RW block
    uint32_t newBlockPhyBlockNum = getFreeBlock();
    physicalToLogicalBlockMapping[newBlockPhyBlockNum] = std::nullopt;

  } else if (blockType == kBlockTypeSW) {
    // TODO: additional page is not implemented yet.

    // check if we can use switching optimization.
    auto SWOwner = physicalToLogicalBlockMapping[logBlockPhyNum].value();
    uint32_t oldDataBlockPbn = logicalToPhysicalBlockMapping[SWOwner].value();

    if (logBlock.getValidPageCount() == param.pagesInBlock) {
      // switching
      // old data block can be erased now.
      // eraseInternal(oldDataBlockPbn, tick);

      PAL::Request eraseReq = PAL::Request(req);
      eraseReq.blockIndex = oldDataBlockPbn;
      eraseReq.ioFlag.set();
      eraseRequests.emplace_back(eraseReq);

      // update logicalToPhysicalBlockMapping
      logicalToPhysicalBlockMapping[SWOwner] = logBlockPhyNum;

     } else {
      // non-switching
      // create a new data block, and copy all valid pages into it.
      // create a new SW block.
      // recyle the old data block and SW block.

      uint32_t newDataBlockPhyNum = getFreeBlock();
      Block &oldDataBlock = physicalBlocks[oldDataBlockPbn];
      physicalToLogicalBlockMapping[newDataBlockPhyNum] = SWOwner;
      logicalToPhysicalBlockMapping[SWOwner] = newDataBlockPhyNum;

      // generate all page read, write and block erase requests.
      for (uint32_t i = 0; i < param.pagesInBlock; i++) {
        uint64_t lpn;
        bool valid, erased;
        logBlock.getPageInfo(i, lpn, valid, erased);

        bool valid2;
        oldDataBlock.getPageInfo(i, lpn, valid2, erased);

        if (valid || valid2) {
          // in log block or in data block, a copy is needed.
          PAL::Request readReq(req);
          if (valid) {
            // read from log block
            readReq.blockIndex = logBlockPhyNum;
          } else {
            // read from data block
            readReq.blockIndex = oldDataBlockPbn;
          }
          readReq.pageIndex = i;
          readReq.ioFlag.set();
          readRequests.emplace_back(readReq);

          PAL::Request writeReq(req);
          writeReq.blockIndex = newDataBlockPhyNum;
          writeReq.pageIndex = i;
          writeReq.ioFlag.set();

          writeRequests.emplace_back(
              std::make_pair(writeReq, SWOwner *  param.pagesInBlock + i)); 
        }
      }

      // erase two old blocks.
      PAL::Request eraseReq = PAL::Request(req);
      eraseReq.ioFlag.set();
      eraseReq.blockIndex = oldDataBlockPbn;
      eraseRequests.emplace_back(eraseReq);

      eraseReq.blockIndex = logBlockPhyNum;
      eraseRequests.emplace_back(eraseReq);

      // get a new SW block.
      SWBlock = getFreeBlock();
      physicalToLogicalBlockMapping[SWBlock.value()] = std::nullopt;
    }
  } else 
    assert(0);

  // calc tick.
  // read first, and then write and erase can be parrellel.
  // use PAL to calc time, and update FTL meta data.

  uint64_t readFinishAt = tick;
  for (auto &req : readRequests) {
    uint64_t readStartAt = tick;

    physicalBlocks[req.blockIndex].read(req.pageIndex, 0, readStartAt);
    if (sendToPAL) {
      pPAL->read(req, readStartAt);
      readFinishAt = std::max(readFinishAt, readStartAt);
    }

  }

  uint64_t writeFinishAt = readFinishAt;
  for (auto &pair : writeRequests) {
    auto &req = pair.first;
    auto lpn = pair.second;

    uint64_t writeStartAt = readFinishAt;
    physicalBlocks[req.blockIndex].write(req.pageIndex, lpn, 0, writeStartAt);

    if (sendToPAL) {
      pPAL->write(req, writeStartAt);
      writeFinishAt = std::max(writeFinishAt, writeStartAt);
    }
  }

  uint64_t eraseFinishAt = readFinishAt;
  for (auto &req : eraseRequests) {
    auto eraseStartAt = readFinishAt;
    eraseInternal(req.blockIndex, eraseStartAt, sendToPAL);
    eraseFinishAt = std::max(eraseFinishAt, eraseStartAt);
  }

  tick = std::max(writeFinishAt, eraseFinishAt);
}

bool FastMapping::findValidPage(uint32_t lpn, uint32_t &pbn, uint32_t &pageIdx,
                                Block *&pBlock, enum BlockType &blockType) {

  // fill impossible value to all output parameters
  pbn = std::numeric_limits<uint32_t>::max();
  pageIdx = std::numeric_limits<uint32_t>::max();
  pBlock = nullptr;
  blockType = kBlockTypeUnknown;

  auto logicalBlockNumber = convertPageToBlock(lpn);
  auto mapping = logicalToPhysicalBlockMapping[logicalBlockNumber];

  if (mapping.has_value() == false) {
    return false;
  }

  auto physicalBlockNumber = mapping.value();


  // check if the page is valid
  Block &block = physicalBlocks[physicalBlockNumber];

  uint64_t logicalPageNumber;
  bool valid, erased;

  block.getPageInfo(this->convertPageToOffsetInBlock(lpn), logicalPageNumber, valid, erased);

  if (valid) {
    // locate it in the data block.
    pbn = physicalBlockNumber;
    pageIdx = this->convertPageToOffsetInBlock(lpn);
    pBlock = &block;
    blockType = kBlockTypeData;
    return true;

  } else {
    // find in log blocks.

    // in SW block?
    if (SWBlock.has_value()) {
      Block &swblock = physicalBlocks[SWBlock.value()];
      uint32_t i = this->convertPageToOffsetInBlock(lpn);
      swblock.getPageInfo(i, logicalPageNumber, valid, erased);
      if (valid && logicalPageNumber == lpn) {
        // found
        pbn = SWBlock.value();
        pageIdx = i;
        pBlock = &swblock;
        blockType = kBlockTypeSW;
        return true;
      }
    }

    // in RW block?
    auto it = RWlogMapping.find(lpn);
    if (it != RWlogMapping.end()) {
      auto &pair = it->second;
      Block &rwblock = physicalBlocks[pair.first];
      uint32_t i = pair.second;
      rwblock.getPageInfo(i, logicalPageNumber, valid, erased);
      assert(valid && logicalPageNumber == lpn);
      pbn = pair.first;
      pageIdx = i;
      pBlock = &rwblock;
      blockType = kBlockTypeRW;
      return true;
    } else {
      return false;
    }
  }

  assert(0);
}


float FastMapping::calculateWearLeveling() {
  // assert(0); // not implemented yet.
  return 0.0f;
}

void FastMapping::calculateTotalPages(uint64_t &valid, uint64_t &invalid) {
  valid = 0;
  invalid = 0;

  for (auto &block : physicalBlocks) {
    valid += block.getValidPageCount();
    invalid += block.getDirtyPageCount();
  }
}

void FastMapping::getStatList(std::vector<Stats> &list, std::string prefix) {
  (void) list, (void) prefix;
  // assert(0); // not implemented yet.
}

void FastMapping::getStatValues(std::vector<double> &values) {
  (void) values;
  // assert(0); // not implemented yet.
}

void FastMapping::resetStatValues() {
  // assert(0); // not implemented yet.
}

}  // namespace FTL

}  // namespace SimpleSSD
