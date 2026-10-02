# buddy-allocator-for-studying-
cross platform buddy system[跨平台的操作系统]
**中文**

本项目用约 200 行 C 代码实现了一个简化版的 **伙伴系统（Buddy System）** 内存分配器，用于模拟操作系统物理页管理。代码支持 Windows 与 Unix/Linux 双平台，使用位图跟踪页状态，并内置随机压测与碎片率统计，适合作为操作系统内存管理的入门学习素材。

**English**

This project implements a simplified **Buddy System** memory allocator in about 200 lines of C, simulating how an OS manages physical pages. It supports both Windows and Unix/Linux, uses a bitmap to track page states, and includes randomized stress testing plus fragmentation statistics — ideal as a hands-on introduction to OS memory management.

---

## 特性 · Features

| 特性 / Feature | 说明 / Description |
|---|---|
| 跨平台 / Cross-platform | `mmap`（Linux/Unix）与 `VirtualAlloc`（Windows）双实现 |
| 伙伴系统 / Buddy System | 支持块的分配、分裂、释放与合并 |
| 位图管理 / Bitmap | 每页 1 bit，`bmp_set` / `bmp_clr` / `bmp_tst` |
| 空闲链表 / Free lists | 按阶数组织的单向链表 `free_area[MAX_ORDER+1]` |
| 碎片率统计 / Fragmentation stats | 实时输出总页、空闲页、已用页、最大空闲块、碎片率 |
| 随机压测 / Random stress test | 2000 轮随机申请/释放，模拟真实负载 |
| 自检 / Self-check | 释放全部后尝试重新分配最大阶块，验证合并正确性 |

## 运行结果 · result
模拟物理内存大小：1024 KB=256 页（页大小4096 B）
[初始化    ] 总页=256 空闲=256 已用=0   最大空闲块=256页 碎片率= 0.00%
[mid       ] 总页=256 空闲=4   已用=252 最大空闲块=2  页 碎片率=50.00%
[mid       ] 总页=256 空闲=22  已用=234 最大空闲块=4  页 碎片率=81.82%
[mid       ] 总页=256 空闲=9   已用=247 最大空闲块=1  页 碎片率=88.89%
[mid       ] 总页=256 空闲=19  已用=237 最大空闲块=2  页 碎片率=89.47%
[after-hand] 总页=256 空闲=19  已用=237 最大空闲块=2  页 碎片率=89.47%
[final     ] 总页=256 空闲=256 已用=0   最大空闲块=256页 碎片率= 0.00%
自检通过：释放后可重新分配最大8阶块（=256页）
