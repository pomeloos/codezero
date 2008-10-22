/*
 * vm objects.
 *
 * Copyright (C) 2008 Bahadir Balban
 */
#include <file.h>
#include <vm_area.h>
#include <l4/macros.h>
#include <l4/api/errno.h>
#include <kmalloc/kmalloc.h>

void print_cache_pages(struct vm_object *vmo)
{
	struct page *p;

	if (!list_empty(&vmo->page_cache))
		printf("Pages:\n======\n");

	list_for_each_entry(p, &vmo->page_cache, list) {
		dprintf("Page offset: 0x%x, virtual: 0x%x, refcnt: %d\n", p->offset,
		       p->virtual, p->refcnt);
	}
}

void vm_object_print(struct vm_object *vmo)
{
	struct vm_file *f;

	printf("Object type: %s %s. links: %d, shadows: %d, Pages in cache: %d.\n",
	       vmo->flags & VM_WRITE ? "writeable" : "read-only",
	       vmo->flags & VM_OBJ_FILE ? "file" : "shadow", vmo->nlinks, vmo->shadows,
	       vmo->npages);
	if (vmo->flags & VM_OBJ_FILE) {
		f = vm_object_to_file(vmo);
		char *ftype;

		if (f->type == VM_FILE_DEVZERO)
			ftype = "devzero";
		else if (f->type == VM_FILE_BOOTFILE)
			ftype = "bootfile";
		else if (f->type == VM_FILE_SHM)
			ftype = "shm file";
		else if (f->type == VM_FILE_VFS)
			ftype = "regular";
		else
			BUG();

		printf("File type: %s\n", ftype);
	}
	// print_cache_pages(vmo);
	// printf("\n");
}

/* Global list of in-memory vm objects. */
LIST_HEAD(vm_object_list);

struct vm_object *vm_object_init(struct vm_object *obj)
{
	INIT_LIST_HEAD(&obj->list);
	INIT_LIST_HEAD(&obj->shref);
	INIT_LIST_HEAD(&obj->shdw_list);
	INIT_LIST_HEAD(&obj->page_cache);
	INIT_LIST_HEAD(&obj->link_list);

	return obj;
}

/* Allocate and initialise a vmfile, and return it */
struct vm_object *vm_object_create(void)
{
	struct vm_object *obj;

	if (!(obj = kzalloc(sizeof(*obj))))
		return 0;

	return vm_object_init(obj);
}

struct vm_file *vm_file_create(void)
{
	struct vm_file *f;

	if (!(f = kzalloc(sizeof(*f))))
		return PTR_ERR(-ENOMEM);

	INIT_LIST_HEAD(&f->list);
	vm_object_init(&f->vm_obj);
	f->vm_obj.flags = VM_OBJ_FILE;

	return f;
}

/*
 * Populates the priv_data with vfs-file-specific
 * information.
 */
struct vm_file *vfs_file_create(void)
{
	struct vm_file *f = vm_file_create();

	if (IS_ERR(f))
		return f;

	f->priv_data = kzalloc(sizeof(struct vfs_file_data));
	f->type = VM_FILE_VFS;

	return f;
}

/* Deletes the object via its base, along with all its pages */
int vm_object_delete(struct vm_object *vmo)
{
	struct vm_file *f;

	// vm_object_print(vmo);

	/* Release all pages */
	vmo->pager->ops.release_pages(vmo);

	/* Remove from global list */
	list_del(&vmo->list);

	/* Check any references */
	BUG_ON(vmo->nlinks);
	BUG_ON(vmo->shadows);
	BUG_ON(!list_empty(&vmo->shdw_list));
	BUG_ON(!list_empty(&vmo->link_list));
	BUG_ON(!list_empty(&vmo->page_cache));

	/* Obtain and free via the base object */
	if (vmo->flags & VM_OBJ_FILE) {
		f = vm_object_to_file(vmo);
		BUG_ON(!list_empty(&f->list));
		if (f->priv_data) {
			if (f->destroy_priv_data)
				f->destroy_priv_data(f);
			else
				kfree(f->priv_data);
		}
		kfree(f);
	} else if (vmo->flags & VM_OBJ_SHADOW)
		kfree(vmo);
	else BUG();

	return 0;
}

int vm_file_delete(struct vm_file *f)
{
	/* Delete it from global file list */
	list_del_init(&f->list);

	/* Delete file via base object */
	return vm_object_delete(&f->vm_obj);
}

