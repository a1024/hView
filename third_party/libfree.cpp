#include"libfree.h"
#include<stdlib.h>
void gcc_free_memory(void *p)
{
	free(p);
}