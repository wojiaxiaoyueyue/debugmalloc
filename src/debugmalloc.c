#include <stdlib.h>
#include <string.h>
#include <printf.h>
#include "debugmalloc.h"
#include "dmhelper.h"

#define BARRIER 0xCCDEADCC
#define HEADERSIZE sizeof(struct Header)
#define FOOTERSIZE sizeof(struct Footer)
#define BLOCKSIZE sizeof(struct Block)

int allocatedSize = 0;

typedef struct Header HEADER;
struct Header{
    int checkSum;
    int size;
    long fence;
};

typedef struct Footer FOOTER;
struct Footer{
    long fence;
};


typedef struct Block BLOCK;
struct Block{
    HEADER h;
    void *ptr;
    FOOTER f;
    char *filename;
    int linenumber;
    BLOCK *next;
};

BLOCK *block_list_header = NULL;

int align2Eight(int size)
{
    if((size & 0x7) == 0)
    {
        return size;
    }
    else
    {
        return ((size >> 3) + 1) << 3;
    }
}

BLOCK *find_block(void *ptr)
{
    BLOCK *p = block_list_header;
    while(p)
    {
        if(p->ptr == ptr)
        {
            return p;
        }
        else{
            p = p->next;
        }
    }
    return NULL;
}

int add_block(void *ptr, char *filename, int linenumber)
{
    HEADER h = *((HEADER *)ptr - 1) ;
    int size = h.size;
    FOOTER f = *(FOOTER *)((char *)ptr + align2Eight(size));
    BLOCK *curr = NULL;
    curr = (BLOCK *)malloc(BLOCKSIZE);

    if(!curr){
        return -1;
    }
    int len = (int)strlen(filename) + 1;
    char *name = malloc((size_t)len);
    if(!name){
        return -1;
    }
    strcpy(name, filename);

    curr->filename = name;
    curr->linenumber = linenumber;
    curr->ptr = ptr;
    curr->h = h;
    curr->f = f;
    curr->next = block_list_header;

    block_list_header = curr;
    return 0;
}

int get_check_number(long num)
{
    int count = 0;
    int standard = 0x1;
    int i;
    for(i = 0; i < sizeof(long); i++)
    {
        if((num & standard) == 1)
        {
            count ++;
        }
        num = num >> 1;
    }

    return count;
}

int remove_block(void *ptr)
{
    BLOCK *head = block_list_header;
    BLOCK *last = NULL;
    while(head){
        if(head->ptr == ptr){
            if(last){
                last->next = head->next;
            }else{
                block_list_header = block_list_header->next;
            }
            free(head->filename);
            free(head);
            return 0;
        }
        last = head;
        head = head->next;
    }
    return -1;
}

int check_block(void *ptr) {
    HEADER *h_p = (HEADER *) ptr - 1;
    long fence = BARRIER;
    long h_fence = h_p->fence;
    int size = h_p->size;
    int checkSum = h_p->checkSum;
    int extra_size = align2Eight(size) - size;

    FOOTER *f_p = (FOOTER *) ((char *) ptr + align2Eight(size));
    long f_fence = f_p->fence;

    if (extra_size <= 4)
    {
        if(strncmp((char *)f_p - extra_size, (char *) &fence, (size_t)extra_size) != 0)
        {
            return 2;
        }
    }
    else{
        if(strncmp((char *)f_p - extra_size, (char *) &fence, 4) != 0
                || strncmp((char *)f_p - extra_size + 4, (char *)&fence, (size_t)(extra_size - 4)) != 0)
        {
            return 2;
        }
    }

    if(h_fence != fence){
        return 1;
    }

    if(f_fence != fence){
        return 2;
    }

    if(checkSum != get_check_number(h_fence + size))
    {
        return 3;
    }
    return 0;
}

/* Wrappers for malloc and free */

void *MyMalloc(size_t size, char *filename, int linenumber) {
    char *all;
    char *footer;
    long barrier = BARRIER;
    all = malloc(align2Eight((int)size) + HEADERSIZE + FOOTERSIZE);
    if(!all){
        return NULL;
    }
    HEADER h;
    FOOTER f;
    h.size = (int)size;
    h.fence = barrier;
    h.checkSum = get_check_number(h.size + h.fence);
    memcpy(all, &h, HEADERSIZE);
    footer = all + HEADERSIZE + align2Eight((int)size);
    f.fence = barrier;
    memcpy(footer, &f, FOOTERSIZE);

    int extra_size = (int)(align2Eight((int)size) - size);
    if(extra_size <= 4)
    {
        strncpy(footer - extra_size, (char *)&barrier, extra_size);
    }
    else{
        strncpy(footer - extra_size, (char *)&barrier, 4);
        strncpy(footer - extra_size + 4, (char *)&barrier, extra_size - 4);
    }

    if(add_block((void *)(all + HEADERSIZE), filename, linenumber) == 0)
    {
        allocatedSize += h.size;
        return (void *)(all + HEADERSIZE);
    }
    else{
        free(all);
        return NULL;
    }

}

void MyFree(void *ptr, char *filename, int linenumber) {
    int num;
    HEADER *h = (HEADER *)ptr - 1;
    BLOCK *curr = NULL;
    curr = find_block(ptr);
    if(!curr){
        error(4, filename, linenumber);
    }
    else{
        num = check_block(ptr);
        if(num){
            errorfl(num, curr->filename, curr->linenumber, filename, linenumber);
        }

        allocatedSize -= h->size;
        free(h);
        remove_block(ptr);
    }
}

/* returns number of bytes allocated using MyMalloc/MyFree:
	used as a debugging tool to test for memory leaks */
int AllocatedSize() {
    return allocatedSize;
}



/* Optional functions */

/* Prints a list of all allocated blocks with the
	filename/line number when they were MALLOC'd */
void PrintAllocatedBlocks() {
    BLOCK *curr = block_list_header;
    while(curr)
    {
        PRINTBLOCK(curr->h.size, curr->filename, curr->linenumber);
        curr=curr->next;
    }
}

/* Goes through the currently allocated blocks and checks
	to see if they are all valid.
	Returns -1 if it receives an error, 0 if all blocks are
	okay.
*/
int HeapCheck() {
    BLOCK *curr = block_list_header;
    int result = 0, err;
    while(curr)
    {
        err = check_block(curr->ptr);
        if(err)
        {
            result = -1;
            PRINTERROR(err, curr->filename, curr->linenumber);
        }
        curr = curr->next;
    }
    return result;
}
