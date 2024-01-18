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

#include "ftl/common/block_fast.hh"

#include <algorithm>
#include <cstring>
#include <cassert>
#include "block.hh"
#include "block_fast.hh"

namespace SimpleSSD {

namespace FTL {

BlockFast::BlockFast(uint32_t blockIdx, uint32_t count, bool createLPNs)
    : idx(blockIdx),
      pageCount(count),
      ioUnitInPage(1),
      pValidBits(nullptr),
      pErasedBits(nullptr),
      pLPNs(nullptr),
      lastAccessed(0),
      eraseCount(0) {
  if (ioUnitInPage == 1) {
    pValidBits = new Bitset(pageCount);
    pErasedBits = new Bitset(pageCount);

    if (createLPNs)
      pLPNs = (uint64_t *)calloc(pageCount, sizeof(uint64_t));
  }
  else if (ioUnitInPage > 1) {
    assert(0);
  }
  else {
    panic("Invalid I/O unit in page");
  }

  // C-style allocation
  pNextWritePageIndex = (uint32_t *)calloc(ioUnitInPage, sizeof(uint32_t));

  erase();
  eraseCount = 0;
}

BlockFast::BlockFast(const BlockFast &old)
    : BlockFast(old.idx, old.pageCount, old.pLPNs != nullptr) {
  if (ioUnitInPage == 1) {
    *pValidBits = *old.pValidBits;
    *pErasedBits = *old.pErasedBits;

    if (old.pLPNs != nullptr)
      memcpy(pLPNs, old.pLPNs, pageCount * sizeof(uint64_t));
  }
  else {
    assert(0);
  }

  memcpy(pNextWritePageIndex, old.pNextWritePageIndex,
         ioUnitInPage * sizeof(uint32_t));

  eraseCount = old.eraseCount;
}

BlockFast::BlockFast(BlockFast &&old) noexcept
    : idx(std::move(old.idx)),
      pageCount(std::move(old.pageCount)),
      ioUnitInPage(std::move(old.ioUnitInPage)),
      pNextWritePageIndex(std::move(old.pNextWritePageIndex)),
      pValidBits(std::move(old.pValidBits)),
      pErasedBits(std::move(old.pErasedBits)),
      pLPNs(std::move(old.pLPNs)),
      lastAccessed(std::move(old.lastAccessed)),
      eraseCount(std::move(old.eraseCount)) {
  // TODO Use std::exchange to set old value to null (C++14)
  old.idx = 0;
  old.pageCount = 0;
  old.ioUnitInPage = 0;
  old.pNextWritePageIndex = nullptr;
  old.pValidBits = nullptr;
  old.pErasedBits = nullptr;
  old.pLPNs = nullptr;
  old.lastAccessed = 0;
  old.eraseCount = 0;
}

BlockFast::~BlockFast() {
  free(pNextWritePageIndex);
  if (pLPNs)
    free(pLPNs);

  delete pValidBits;
  delete pErasedBits;

  pNextWritePageIndex = nullptr;
  pLPNs = nullptr;
  pValidBits = nullptr;
  pErasedBits = nullptr;
}

BlockFast &BlockFast::operator=(const BlockFast &rhs) {
  if (this != &rhs) {
    this->~BlockFast();
    *this = BlockFast(rhs);  // Call copy constructor
  }

  return *this;
}

BlockFast &BlockFast::operator=(BlockFast &&rhs) {
  if (this != &rhs) {
    this->~BlockFast();

    idx = std::move(rhs.idx);
    pageCount = std::move(rhs.pageCount);
    ioUnitInPage = std::move(rhs.ioUnitInPage);
    pNextWritePageIndex = std::move(rhs.pNextWritePageIndex);
    pValidBits = std::move(rhs.pValidBits);
    pErasedBits = std::move(rhs.pErasedBits);
    pLPNs = std::move(rhs.pLPNs);
    lastAccessed = std::move(rhs.lastAccessed);
    eraseCount = std::move(rhs.eraseCount);

    rhs.pNextWritePageIndex = nullptr;
    rhs.pValidBits = nullptr;
    rhs.pErasedBits = nullptr;
    rhs.pLPNs = nullptr;
    rhs.lastAccessed = 0;
    rhs.eraseCount = 0;
  }

  return *this;
}

uint32_t BlockFast::getBlockIndex() const {
  return idx;
}

uint64_t BlockFast::getLastAccessedTime() {
  return lastAccessed;
}

uint32_t BlockFast::getEraseCount() {
  return eraseCount;
}

uint32_t BlockFast::getValidPageCount() {
  uint32_t ret = 0;

  if (ioUnitInPage == 1) {
    ret = pValidBits->count();
  }
  else {
    assert(0);
  }

  return ret;
}

uint32_t BlockFast::getValidPageCountRaw() {
  uint32_t ret = 0;

  if (ioUnitInPage == 1) {
    // Same as getValidPageCount()
    ret = pValidBits->count();
  }
  else {
    assert(0);
  }

  return ret;
}

uint32_t BlockFast::getDirtyPageCount() {
  uint32_t ret = 0;

  if (ioUnitInPage == 1) {
    ret = (~(*pValidBits | *pErasedBits)).count();
  }
  else {
    assert(0);
  }

  return ret;
}

uint32_t BlockFast::getNextWritePageIndex() {
  uint32_t idx = 0;

  for (uint32_t i = 0; i < ioUnitInPage; i++) {
    if (idx < pNextWritePageIndex[i]) {
      idx = pNextWritePageIndex[i];
    }
  }

  return idx;
}

uint32_t BlockFast::getNextWritePageIndex(uint32_t idx) {
  return pNextWritePageIndex[idx];
}

uint32_t BlockFast::getErasedPageCount() {
  return pErasedBits->count();
}

uint32_t BlockFast::getLPN(uint32_t pageIndex) {
  assert(pLPNs != nullptr);
  return pLPNs[pageIndex];
}

bool BlockFast::isValid(uint32_t pageIndex) {
  return pValidBits->test(pageIndex);
}

bool BlockFast::isErased(uint32_t pageIndex) {
  return pErasedBits->test(pageIndex);
}

void BlockFast::claimLPN(bool exist) {
  if (exist) {
    if (pLPNs == nullptr)
      pLPNs = (uint64_t *)calloc(pageCount, sizeof(uint64_t));
    else 
      assert(0);
  } else {
    if (pLPNs != nullptr) {
      free(pLPNs);
      pLPNs = nullptr;
    }
    else 
      assert(0);
  }
}

bool BlockFast::isCleanBlock() {
  return getErasedPageCount() == pageCount;
}

bool BlockFast::read(uint32_t pageIndex, uint32_t idx, uint64_t tick) {
  bool read = false;

  if (ioUnitInPage == 1 && idx == 0) {
    read = pValidBits->test(pageIndex);
  }
  else if (idx < ioUnitInPage) {
    assert(0);
  }
  else {
    panic("I/O map size mismatch");
  }

  if (read) {
    lastAccessed = tick;
  }

  return read;
}

bool BlockFast::write(uint32_t pageIndex, uint64_t lpn, uint32_t idx,
                  uint64_t tick) {
  bool write = false;

  if (ioUnitInPage == 1 && idx == 0) {
    write = pErasedBits->test(pageIndex);
  }
  else if (idx < ioUnitInPage) {
    assert(0);
  }
  else {
    panic("I/O map size mismatch");
  }

  if (write) {
    lastAccessed = tick;

    if (ioUnitInPage == 1) {
      pErasedBits->reset(pageIndex);
      pValidBits->set(pageIndex);

      if (pLPNs)
        pLPNs[pageIndex] = lpn;
    }
    else {
      assert(0);
    }

    pNextWritePageIndex[idx] = pageIndex + 1;
  }
  else {
    panic("Write to non erased page");
  }

  return write;
}

void BlockFast::erase() {
  if (ioUnitInPage == 1) {
    pValidBits->reset();
    pErasedBits->set();
  }
  else {
    assert(0);
  }

  memset(pNextWritePageIndex, 0, sizeof(uint32_t) * ioUnitInPage);

  eraseCount++;
}

void BlockFast::invalidate(uint32_t pageIndex, uint32_t idx) {
  (void) idx;

  if (ioUnitInPage == 1) {
    pValidBits->reset(pageIndex);
  }
  else {
    assert(0);
  }
}

}  // namespace FTL

}  // namespace SimpleSSD
