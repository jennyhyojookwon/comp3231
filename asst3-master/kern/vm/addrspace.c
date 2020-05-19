/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	/* Allocate addrspace */
	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/* Allocate level one PT */
	as->page_table = (paddr_t **) alloc_kpages(1);
	if (as->page_table == NULL) {
		kfree(as);
		return NULL;
	}

	/* Initialise entries to NULL */
	for (int pt1 = 0; pt1 < PAGE_TABLE_SIZE; pt1++) {
		as->page_table[pt1] = NULL;
	}

	/* No regions when created */
	as->first_region = NULL;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	/* Flag to check if nomem happended */
	bool nomem = false;
	
	struct addrspace *new;

	/* Create new addrspace */
	new = as_create();
	if (new == NULL) {
		return ENOMEM;
	}
	/****************************************************/
	/* Copy in regions */
	struct region *old_region;
	struct region *new_region = new->first_region;
	for (old_region = old->first_region; old_region != NULL; old_region = old_region->next) {
		/* Allocate region for new as */
		struct region *reg = (struct region *) kmalloc(sizeof(struct region));
		if (reg == NULL) {
			nomem = true;
			break;
		}

		/* copy in data of old region to new region */
		reg->vbase = old_region->vbase;
		reg->npages = old_region->npages;
		reg->w = old_region->w;
		reg->w_reserve = old_region->w_reserve;
		reg->next = NULL;

		/* Link reg to list of regions */
		if (new_region == NULL) {
			new->first_region = reg;
		} else {
			new_region->next = reg;
		}

		/* Update new_region */
		new_region = reg;
	}

	/* ENOMEM while copying regions */
	if (nomem) {
		as_destroy(new);
		return ENOMEM;
	}

	/****************************************************/
	/* Copy in page table */
	for (int pt1 = 0; pt1 < PAGE_TABLE_SIZE; pt1++) {
		/* Allocate l1 page_table */
		if (old->page_table[pt1] != NULL) {
			new->page_table[pt1] = (paddr_t *) kmalloc(sizeof(paddr_t) * PAGE_TABLE_SIZE);
			if (new->page_table[pt1] == NULL) {
				nomem = true;
				break; /* Break out of first loop */
			}

			for (int pt2 = 0; pt2 < PAGE_TABLE_SIZE; pt2++) {
				/* Allocate l2 page_table */
				if (old->page_table[pt1][pt2] != 0) {
					vaddr_t frame = alloc_kpages(1);
					if (frame == 0) {
						nomem = true;
						break; /* Break out of second loop */
					}

					/* Copy pte from old to frame */
					memmove((void *)frame, (const void *) PADDR_TO_KVADDR(old->page_table[pt1][pt2] & PAGE_FRAME), PAGE_SIZE);
					int dirty = old->page_table[pt1][pt2] & TLBLO_DIRTY;
					new->page_table[pt1][pt2] = (KVADDR_TO_PADDR(frame) & PAGE_FRAME) | dirty | TLBLO_VALID;
				} else {
					new->page_table[pt1][pt2] = 0;
				}
			}

			if (nomem) {
				break; /* Break out of first loop */
			}
		}
	}

	/* ENOMEM when copying pagetable */
	if (nomem) {
		as_destroy(new);
		return ENOMEM;
	}

	/****************************************************/
	*ret = new;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/* Free regions */
	struct region *curr, *prev;
	curr = prev = as->first_region;
	while (curr != NULL) {
		curr = curr->next;
		kfree(prev);
		prev = curr;
	}

	/* Free ptes in page_table */
	for (int pt1 = 0; pt1 < PAGE_TABLE_SIZE; pt1++) {
		/* Check if pt1 has pages */
		if (as->page_table[pt1] != NULL) {
			for (int pt2 = 0; pt2 < PAGE_TABLE_SIZE; pt2++) {
				/* Check if pt2 has pages */
				if (as->page_table[pt1][pt2] != 0) {
					free_kpages(PADDR_TO_KVADDR(as->page_table[pt1][pt2] & PAGE_FRAME));
				}
			}
		}
	}

	/* Free page_table */
	kfree(as->page_table);

	/* Free addrspace */
	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/* Disable interrupts on this CPU while frobbkubg the TLB. */
	int spl = splhigh();
	for (int i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

void
as_deactivate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/* Disable interrupts on this CPU while frobbkubg the TLB. */
	int spl = splhigh();
	for (int i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/* Invalid region */
	if (as == NULL) {
        return ENOSYS;
	}

	/* Allocate new region */
	struct region *new_region = (struct region *) kmalloc(sizeof(struct region));
	if (new_region == NULL) {
		return ENOMEM;
	}

	/* Align the region. First, the base... */
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
	size_t npages = memsize / PAGE_SIZE;

	/* Initialize new region */
	new_region->vbase = vaddr;
	new_region->npages = npages;
	new_region->w = writeable;
	// if (readable)
	// 	new_region->rwx |= RG_READ_MASK;
	// if (writeable)
	// 	new_region->rwx |= RG_WRITE_MASK;
	// if (executable)
	// 	new_region->rwx |= RG_EXE_MASK;
	new_region->w_reserve = new_region->w;
    new_region->next = NULL;

	/* Check if any region is associated with as */
	if (as->first_region == NULL) {
        as->first_region = new_region;
        return 0;
    }

	/* Connect region to as */
	struct region* tmp_region = as->first_region;
    while(tmp_region->next != NULL) {
        tmp_region = tmp_region->next;
	}
    tmp_region->next = new_region;

	(void) readable;
	(void) executable;
    return 0;
}

/* Make READONLY regions READWRITE for loading purposes */
int
as_prepare_load(struct addrspace *as)
{
	/* Temporarily set all regions to writable */
	struct region *cur_reg;
	for (cur_reg = as->first_region; cur_reg != NULL; cur_reg = cur_reg->next) {
		cur_reg->w = 1;
	}
	
	return 0;
}

/* Enfore READONLY again */
int
as_complete_load(struct addrspace *as)
{
	/* Store back rwx permissions */
	struct region *cur_reg;
	for (cur_reg = as->first_region; cur_reg != NULL; cur_reg = cur_reg->next) {
		cur_reg->w = cur_reg->w_reserve;
	}

	/* Flush TLB */
	int spl = splhigh();
	for (int i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/* Define a stack region with 16*PAGE_SIZE size and permissions */
	int res = as_define_region(as, USERSTACK - USERSTACKSIZE, USERSTACKSIZE, 1, 1, 1);
	if (res) {
		return res;
	}
	
	/* User-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

