#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <spl.h>
#include <current.h>
#include <proc.h>
#include <copyinout.h>


/* Place your page table functions here */

int
vm_create_l1_pte(paddr_t **page_table, uint32_t pt1)
{
    KASSERT(page_table[pt1] == NULL);

    /* Create PAGE_TABLE_SIZE pt1 */
    page_table[pt1] = (paddr_t *) kmalloc(sizeof(paddr_t) * PAGE_TABLE_SIZE);
    if (page_table[pt1] == NULL) {
        return ENOMEM;
    }
    
    /* Zero the pages */
    for (int pt2 = 0; pt2 < PAGE_TABLE_SIZE; pt2++) {
        page_table[pt1][pt2] = 0;
    }

    return 0;
}


int
vm_create_l2_pte(paddr_t **page_table, uint32_t pt1, uint32_t pt2, uint32_t dirty)
{
    KASSERT(page_table[pt1][pt2] == 0);

    vaddr_t v_page = alloc_kpages(1);
    if (v_page == 0)
        return ENOMEM;

    page_table[pt1][pt2] = (KVADDR_TO_PADDR(v_page) & PAGE_FRAME) | dirty | TLBLO_VALID;

    return 0;
}

void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
}

/* TLB miss handler */
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    uint32_t ehi, elo;
    uint32_t dirty = 0;
    volatile paddr_t paddr;
    bool alloc_pt1 = false;

	// faultaddress &= PAGE_FRAME;

    /* Check if VM_FAULT is Readonly */
    switch(faulttype) {
        case VM_FAULT_READONLY:
            return EFAULT;
        case VM_FAULT_READ:
        case VM_FAULT_WRITE:
            break;
        default:
            return EINVAL;
    }

    /* Check if curproc is kernel process */
    if (curproc == NULL) {
        return EFAULT;
    }

    /* Lookup PT */
    struct addrspace *as = proc_getas();
    if (as == NULL) {
        return EFAULT;
    }

    /* |PT1|PT2|Offset|
     * |10 |10 | 12   | */
    paddr = KVADDR_TO_PADDR(faultaddress);
    uint32_t pt1 = paddr >> 22;
    uint32_t pt2 = (paddr << 10) >> 22;

    /* Ensure pt1 is not NULL */
    if (as->page_table[pt1] == NULL) {
        /* Allocate level 1 page table */
        int res = vm_create_l1_pte(as->page_table, pt1);
        if (res) {
            return res;
        }
        alloc_pt1 = true;
    }

    /* Check if is a valid translation */
    if (as->page_table[pt1][pt2] == 0) {
        /* Not a valid translation -> Look up region */
        struct region *cur_reg;
        for (cur_reg = as->first_region; cur_reg != NULL; cur_reg = cur_reg->next) {
            /* Check if is valid region */
            if (faultaddress >= cur_reg->vbase &&
                faultaddress < (cur_reg->vbase + (cur_reg->npages * PAGE_SIZE))) {
                    /* Set dirty bit if region is writable */
                    if (cur_reg->w) {
                        dirty = TLBLO_DIRTY;
                    } else {
                        dirty = 0;
                    }
                    break;
            }
        }
        
        /* Invalid region */
        if (cur_reg == NULL) {
            if (alloc_pt1) {
                kfree(as->page_table[pt1]);
            }
            return EFAULT;
        }

        /* Allocate frame, zero-fill, Insert PTE */
        int res = vm_create_l2_pte(as->page_table, pt1, pt2, dirty);
        if (res) {
            if (alloc_pt1) {
                kfree(as->page_table[pt1]);
            }
            return res;
        }
    }

    /* Load TLB */
    ehi = faultaddress & PAGE_FRAME;
    elo = as->page_table[pt1][pt2];

    int spl = splhigh();
    tlb_random(ehi, elo);
    splx(spl);

    return 0;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

