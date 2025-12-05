/*
在fs/proc/task_mmu.c中的show_map_vma() 函数末尾，找到：
    if (name) {
        seq_pad(m, ' ');
        seq_puts(m, name);
    }
在其后、在 seq_putc(m, '\n'); 之前，添加以下代码。
*/
/* === 实验三：追加页面状态字符串（修正版）=== */
    {
        unsigned long addr;
        unsigned long max_pages = 1024 * 10; // 最多遍历 40MB (10K pages)，防止性能爆炸
        unsigned long vma_size = vma->vm_end - vma->vm_start;
        unsigned long total_pages = vma_size >> PAGE_SHIFT;

        // 如果 VMA 太大，只输出提示，避免卡死系统
        if (total_pages > max_pages) {
            seq_puts(m, " [large]");
        } else {
            for (addr = vma->vm_start; addr < vma->vm_end; addr += PAGE_SIZE) {
                pgd_t *pgd;
                p4d_t *p4d;
                pud_t *pud;
                pmd_t *pmd;
                pte_t *pte;
                pte_t pte_val;

                // 检查 mm 是否有效，避免空指针访问
                if (!vma->vm_mm) {
                    seq_putc(m, '?');
                    continue;
                }

                pgd = pgd_offset(vma->vm_mm, addr);
                if (pgd_none(*pgd) || pgd_bad(*pgd)) {
                    seq_putc(m, '.');
                    continue;
                }

                p4d = p4d_offset(pgd, addr);
                if (p4d_none(*p4d) || p4d_bad(*p4d)) {
                    seq_putc(m, '.');
                    continue;
                }

                pud = pud_offset(p4d, addr);
                if (pud_none(*pud) || pud_bad(*pud)) {
                    seq_putc(m, '.');
                    continue;
                }

                // 检测 PUD 级别的大页
                if (pud_large(*pud)) {
                    seq_putc(m, 'H');
                    continue;
                }

                pmd = pmd_offset(pud, addr);
                if (pmd_none(*pmd) || pmd_bad(*pmd)) {
                    seq_putc(m, '.');
                    continue;
                }

                // 检测 PMD 级别的透明大页
                if (pmd_trans_huge(*pmd)) {
                    seq_putc(m, 'H');
                    continue;
                }

                pte = pte_offset_map(pmd, addr);
                if (!pte) {
                    seq_putc(m, '?');
                    continue;
                }

                pte_val = *pte;
                pte_unmap(pte); // ⚠️ 关键：尽早释放映射！

                if (!pte_present(pte_val)) {
                    seq_putc(m, '.');
                    continue;
                }

                struct page *page = pte_page(pte_val);
                if (!page) {
                    seq_putc(m, '.');
                    continue;
                }

                int ref = page_count(page);
                if (ref < 10)
                    seq_putc(m, '0' + ref);
                else
                    seq_putc(m, 'x');
            }
        }
    }