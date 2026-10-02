/*-------头文件设计目的-------
跨平台兼容性：同一份代码可以在 Windows 和 Unix/Linux 上编译运行
内存管理：实现底层的内存映射功能
类型安全：使用固定宽度整数类型
调试支持：通过断言验证操作正确性*/

#ifdef _WIN32
 //windows平台代码
 #include<windows.h>//提供 Windows API 函数，包含所有 Windows 系统调用所需的类型和函数声明
#else
 //linux/unix平台

/*①启用 GNU 扩展功能
②必须在包含任何头文件之前定义
③必须在包含任何头文件之前定义，例如：启用 mmap 的某些扩展标志、mremap 等函数*/
 #define _GNU_SOURCE

 #include<sys/mman.h>//提供内存映射函数
 #include<unistd.h>/*①提供 POSIX 操作系统 API②包含系统调用相关函数*/
#endif
 #include<stdio.h>
 #include<stdlib.h>
 #include<stdint.h>//提供固定宽度的整数类型
 #include<string.h>/*①提供内存操作函数②用于初始化和复制内存映射区域*/

/*①提供断言宏
②用于调试时验证程序假设，例如验证内存映射是否成功*/
 #include<assert.h>

/*平台无关地获取/释放一段匿名内存（模拟物理内存）*/
static void* acquire_mem(size_t sz)
{
#ifdef _WIN32
	return VirtualAlloc(NULL, sz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
	void* p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return (p == MAP_FAILED) ? NULL : p;
#endif
}

static void release_mem(void* p, size_t sz)
{
#ifdef _WIN32
	(void)sz;
	VirtualFree(p, 0, MEM_RELEASE);
#else
	munmap(p, sz);
#endif
}

#define PAGE_SIZE 4096UL
#define MEM_SIZE (1UL<<20)//1MB物理内存
#define NPAGES (MEM_SIZE/PAGE_SIZE)//256页
#define MAX_ORDER 10//上限；对256页实际顶阶为8

/*GFP(获取空闲页)标志*/
#define _GFP_KERNEL 0x01
#define _GFP_ATOMIC 0x02
#define _GFP_DMA 0x04
#define GFP_KERNEL _GFP_KERNEL
#define GFP_ATOMIC _GFP_ATOMIC
#define GFP_DMA _GFP_DMA
typedef unsigned int gfp_t;

/*页描述符*/
struct page
{
	int order;//若为空闲块首页，记录块阶数，否则记录-1
	int allocated;//是否在用
	struct page* next;//空闲链表后继（单向）
};

static struct page pages[NPAGES];
static struct page* free_area[MAX_ORDER + 1];//每个order，一条空闲链
static unsigned long* bitmap;//使用位图，每页1比特
static void* mem;//mmap的模拟物理内存

#define BITS_PER_LONG 8*sizeof(unsigned long)
#define BITMAP_LONGS (NPAGES+BITS_PER_LONG-1)/BITS_PER_LONG

/*======
	i为页号；
	i落在第几个 unsigned long：i / BITS_PER_LONG（整数除法，向下取整）→ 这是数组下标；
	i在这个 unsigned long 的第几位：i % BITS_PER_LONG（取余）→ 这是位偏移量
======*/
// 把第 i 页标记为"已使用"（置 1）：取出bitmap[i / BITS_PER_LONG]这个64位数，把它的第 (i % BITS_PER_LONG) 位强制设为 1，其它位保持不变
static inline void bmp_set(int i)
{
	bitmap[i / BITS_PER_LONG] |= (1UL << (i % BITS_PER_LONG));
}
// 把第 i 页标记为"未使用"（清 0）：取出 bitmap[i / BITS_PER_LONG]，把它的第 (i % BITS_PER_LONG) 位强制清 0，其它位保持不变
static inline void bmp_clr(int i) 
{
	bitmap[i / BITS_PER_LONG] &= ~(1UL << (i % BITS_PER_LONG));
}
//读取第 i 页的使用状态（返回 0 或 1）：取出所在 long → 把目标位右移到第 0 位 → 只保留第 0 位 → 返回
static inline int bmp_tst(int i)
{
	return (int)((bitmap[i / BITS_PER_LONG] >> (i % BITS_PER_LONG)) & 1UL);
}

static inline int page_idx(void* p)
{
	return (int)(((uintptr_t)p - (uintptr_t)mem)/PAGE_SIZE);
}
static inline void* page_addr(int i)
{
	return (char*)mem + (size_t)i * PAGE_SIZE;
}
static inline int buddy_idx(int i, int o)
{
	return i ^(1 << o);
}

/*入栈（push）：把首页为p的空闲块压入order链表头*/
static void fa_push(struct page* p, int order)
{
	p->order = order;
	p->allocated = 0;
	p->next = free_area[order];
	free_area[order] = p;
}
/*出栈（pop）：取order链表头*/
static struct page* fa_pop(int order)
{
	struct page* p = free_area[order];
	if (p)
	{
		free_area[order] = p->next;
		p->next = NULL;
	}
	return p;
}

/*定向删除：从order链表删除target，成功返回1，否则返回0*/
static int fa_remove(struct page* target, int order)
{
	for (struct page** pp = &free_area[order];*pp;pp = &(*pp)->next)
	{
		if (*pp == target)
		{
			*pp = target->next;
			target->next = NULL;
			return 1;
		}
	}
	return 0;
}

/*伙伴系统分配器初始化入口（entry point of the initialization）：把整段内存作为“最大可容纳阶数”的单一空闲块*/
static void buddy_init()
{
	memset(pages, 0, sizeof(pages));
	for (int o = 0;o <= MAX_ORDER;o++)
		free_area[o] = NULL;
	bitmap = (unsigned long*)calloc(BITMAP_LONGS, sizeof(unsigned long));
	assert(bitmap && "bitmap calloc failed");//断言（直接写死规则，如果违反规则，程序终止）：位图分配失败打印bitmap calloc failed
	for (int i = 0;i < NPAGES;i++)
	{
		pages[i].order = -1;
		pages[i].allocated = 0;
	}
	//计算最大可容纳阶数并压入空闲链表
	int o = 0;
	while (o < MAX_ORDER && (1UL << (o + 1)) <= NPAGES)
		o++;
	fa_push(&pages[0], o);
}

/*伙伴系统的核心机制之一：分裂（split）*/
//*p：要分裂的块的首页描述符指针，传的是 &pages[i]，即页描述符数组里的元素地址；order:这个块当前的阶数
static struct page* split_block(struct page* p, int order)
{
	int idx = (int)(p - pages);//计算块首页在pages[] 数组中的下标
	int right = idx ^ (1 << (order - 1));//计算右伙伴的页号
	fa_push(&pages[right], order - 1);//把右伙伴压入order-1阶空闲链表
	return p;//返回左伙伴首页指针
}

/*伙伴系统分配入口*/
void* alloc_pages(gfp_t gfp, int order)
{
	(void)gfp;//gfp强制转换成void并丢弃。本简化版不实现 GFP 语义，但又要保持与内核一致的函数签名，所以参数必须存在但不用。C 语言处理"必须保留但未使用参数"的标准惯用法
	if (order<0 || order>MAX_ORDER)
		return NULL;
	//自下而上找一个足够大的非空链
	//从小往大找：取到的是"刚好能装下需求的最小块"，分裂次数最少，内部碎片最小
	int o;
	for (o = order;o <= MAX_ORDER;o++)
	{
		if (free_area[o])
			break;
	}
	if (o > MAX_ORDER)
		return NULL;
	struct page* p = fa_pop(o);//取出适合的块
	//分配第二步：如果找到的块比需求大，就不断对半分裂，直到块的阶数等于 order
	while (o > order)
	{
		p = split_block(p, o);
		o--;
	}
	int idx = (int)(p - pages);
	p->order = order;
	p->allocated = 1;
	for (int i = 0;i < (1 << order);i++)
		bmp_set(idx + i);
	return page_addr(idx);
}

void free_pages(void* ptr, int order)
{
	if (!ptr || order > MAX_ORDER || order < 0)
		return;
	int idx = page_idx(ptr);
	if (idx < 0 || idx >= NPAGES||idx+(1<<order)>NPAGES)//还要检查第三个条件
		return;
	//“在用”的格子涂回“空闲”
	for (int i = 0;i < (1 << order);i++)
		bmp_clr(idx + i);
	pages[idx].allocated = 0;
	pages[idx].order = order;
	//逐阶向上合并（包括合并清单）
	while (order < MAX_ORDER)
	{
		int b = buddy_idx(idx, order);
		if (b < 0 || b >= NPAGES)//伙伴越界
			break;
		if (bmp_tst(b))//伙伴在使用
			break;
		if (pages[b].order != order)//不是同阶空闲块首页
			break;
		if (!fa_remove(&pages[b], order))//不在空闲链表里（原来写的是&pages[order],错了；如果不是&pages[b]，即伙伴首页，会导致链表被破坏，后续fa_push操作到非法指针，出现段错误
			break;
		//新首页取较小者
		if (b < idx)
			idx = b;
		//合并成功三件事：①阶数+1②新首页记录新页数③标记空闲
		order++;
		pages[idx].order = order;
		pages[idx].allocated = 0;
	}
	fa_push(&pages[idx], order);
}

/*统计与诊断*/
struct buddy_stat
{
	int total_pages;//模拟物理内存总页数
	int free_pages;//空闲页数（当前未被任何分配块占用的页总数）
	int used_pages;//已使用页数（当前被分配块占用的页总数）
	int largest_free_pages;//最大连续空闲页数（当前最大的空闲块的页数）
	double frag_rate;//内存碎片率（1-largest_free_pages/free_pages）
};
static void buddy_stat(struct buddy_stat* s)
{
	s->total_pages = NPAGES;//总页数
	s->free_pages = 0;
	for (int i = 0;i < NPAGES;i++)
	{
		if (!bmp_tst(i))
			s->free_pages++;
	}
	s->used_pages = s->total_pages - s->free_pages;//数学恒等式，直接算出
	s->largest_free_pages = 0;//如果所有页都在使用，最大连续空闲页数就是0
	for (int i = MAX_ORDER;i >= 0;i--)
	{
		if (free_area[i])
		{
			s->largest_free_pages = 1 << i;
			break;
		}
	}
	s->frag_rate = s->free_pages ? 1.0 - (double)s->largest_free_pages / s->free_pages : 0.0;
}

static void stat_print(const char* tag)
{
	struct buddy_stat s;
	buddy_stat(&s);
	printf("[%-12s] 总页=%-3d 空闲=%-3d 已用=%-3d 最大空闲块=%-3d页 碎片率=%5.2f%%\n",
		tag, s.total_pages, s.free_pages, s.used_pages, s.largest_free_pages, s.frag_rate * 100.0);
}