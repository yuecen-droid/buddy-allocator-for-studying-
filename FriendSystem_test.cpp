#include<stdio.h>
#include "FriendSystem.h"
#define MAX_TRACK 4096
struct track
{
	void* p;
	int order;
};
int main()
{
	//申请1MB模拟物理内存（Linux：nmap  Windows：VirtualAlloc）
	mem = acquire_mem(MEM_SIZE);
	if (!mem)
	{
		printf("acquired_mem failed\n");
		return 1;
	}
	buddy_init();
	printf("模拟物理内存大小：%lu KB=%lu 页（页大小%lu B）\n", MEM_SIZE >> 10, (unsigned long)NPAGES, PAGE_SIZE);
	stat_print("初始化");

	//反复申请/释放
	struct track tr[MAX_TRACK];//已分配块的“台账”数组
	int n = 0;//当前持有块数，“台账”的有效游标
	unsigned int seed = 20260819u;
	srand(seed);//随机数生成器

	int rounds = 2000;//执行2000次申请/释放操作
	for (int r = 0;r < rounds;r++)
	{
		//人为设定负载模型：70% 概率申请（且未超上限），30% 概率释放（若有持有块）
		/*逻辑拆解：
		n == 0：手上没有任何块时， 强制申请 （否则无事可做）；
		 否则rand() % 10 < 7 ：产生 0–9 的随机数，小于 7 的概率为 70 % ，即 70 % 概率申请；
		 且n < MAX_TRACK ：台账未满（ < 4096）才能申请，防止数组越界。三者满足"无块 或 (抽到申请 且 台账有空)"才申请*/
		if (n == 0 || (rand() % 10 < 7 && n < MAX_TRACK))
		{
			int order = rand() % 4;//随机生成一个 0–3 的阶,决定本次申请的块大小为`2^order` 页，即 1、2、4、8 页。限制在 order≤3 是为了让申请更容易成功（高阶块稀缺），同时也制造多种尺寸混合的碎片场景。
			void* p = alloc_pages(GFP_KERNEL, order);//调用 buddy 分配器申请`2^order` 页
			if (p)
			{
				/*写满整块内存 ，模拟真实使用，
				同时也是一种校验手段——如果 allocator 返回的区域有越界或重叠,memset可能踩坏其他块数据。
				填充值随轮次变化，便于事后检查。
				大小为2order × PAGE_SIZE 字节*/
				memset(p, (int)(r & 0xff), (1UL << order) * PAGE_SIZE);
				tr[n].p = p;
				tr[n].order = order;
				n++;
			}
		}
		else if (n > 0)
		{
			int k = rand() % n;
			free_pages(tr[k].p, tr[k].order);
			tr[k] = tr[--n];
		}
		if ((r + 1) % 500 == 0)
		{
			stat_print("mid");
		}
	}
	stat_print("after-hand");

	/*3:释放剩余全部：伙伴系统应能完全合并，碎片率归零*/
	for (int i = 0;i < n;i++)
		free_pages(tr[i].p, tr[i].order);
	stat_print("final");

	/*4:正确性：最终应能再次分配到最大阶块*/
	int top = 0;
	while (top < MAX_ORDER && (1UL << (top + 1)) <= NPAGES)
		top++;
	void* big = alloc_pages(GFP_KERNEL, top);
	if (big)
	{
		printf("自检通过：释放后可重新分配最大%d阶块（=%lu页）\n",top,(1UL<<top));
		free_pages(big, top);
	}
	else
	{
		fprintf(stderr, "自检失败：存在碎片无法合并\n");//错误信息写到 stderr 而非 stdout——这是 Unix 惯例：诊断/报错与正常输出分离，重定向 stdout 时错误信息不会丢失，也不会污染正常结果流
	}

	release_mem(mem, MEM_SIZE);
	return 0;
}