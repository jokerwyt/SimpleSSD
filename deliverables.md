# 1. FAST Implementation

## Key Data Structure
```
- `blockMappingTable`: map logical block number -> physical block number
- `blocks`: physical block number -> block info.
- `SWBlock`: The SW log block.
- `RWBlocks`: All RW log blocks.
- `RWlogMapping`: All lpn in RW log block -> (pbn, pageOffset)
```


## Workflow
```
# Algorithm design:

## read:
1. check block mapping table, suppose block pbn X.
  - no mapped: no data

2. check the corresponding page status:
  - if valid, just return
  - if erased && invalid: no data
  - if invalid: check log blocks.
  
3. If not found: otherwise: no data


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
```

## 内存分析
Total SSD Capacity.
```
Channel = 8
Package = 4
Die = 4
Plane = 1
Block = 512
Page = 512
PageSize = 4096

8*4*4*1*512*512*4096/2^30=128G
```

```
BlockCnt = 8*4*4*1*512=65536

- `blockMappingTable`: sizeof(uint32_t) * 2 * (65536) = 524288 B
- `blocks`: 65536 * sizeof(Block) = 4194304 B
- `RWlogMapping`: RW_cnt * pagesize * 8B = 24576B

precentage: 4743168 B / 128G = 0.0037056%
```

# 2. Git Patch
见附件

# 3. 与Page Mapping的性能比较

## 微基准测试
共有六种模式。

### 3.1 延迟
总体延迟统计量：
延迟点图：

### 3.2 吞吐

## 宏基准测试

### 3.1 延迟
总体延迟统计量：
延迟点图：

### 3.2 吞吐