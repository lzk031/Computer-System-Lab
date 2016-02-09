#include "csapp.h"
#include "cache.h"

/*
 * cache.c
 *  
 * cache is a doubly linked list. each web object is
 * stored in a node. each node has a size, a content pointer,
 * a uri pointer and a age field. The cache has a total size 
 * limit and each node have a maximum size. it use LRU to eivict
 * cache block when necessary.
 * 
 */

/*
 * initialize a empty cache and return the pointer of it
 */
cache *cache_init(){
	cache *c = (cache*)Malloc(sizeof(cache));
	c->size = 0;
	c->head = NULL;
	return c;
}


/*
 * insert a cache block into the cache list. 
 * copy content, and uri, assign size and
 */
void cache_insert(cache* c, char *uri, char *content, int size){

	block* node;
	int total_size;
	total_size = c->size+size;
	while(total_size>MAX_CACHE_SIZE){
		evict(c);
		total_size = c->size+size;
	}
	node = (block*)Malloc(sizeof(block));
    node->age = time(NULL);
	node->content = Malloc(size);
	node->uri = Malloc(strlen(uri)+1);
	memcpy(node->content, content, size);
	strcpy(node->uri, uri);
	node->size = size;
	node->prev = NULL;
	c->size += size;

	node->next = c->head;
	if(c->head==NULL){
		c->head = node;
		return;
	} 


	c->head->prev = node;
	c->head = node;
}

/*
 * find a cached web object based on
 * a provided uri. return the pointer
 * to that cache block if found. if
 * not found, return NULL
 */
block *find_cache(cache* c, char* uri){
	block* node = c->head;
	// block *prev, *next;
	int found = 0;
	while(node!=NULL){
		if(!strcmp(node->uri, uri)){
			found = 1;
			break;
		}
		node = node->next;
	}

	/* return NULL if not found */
	if(!found)
		return NULL;
	node->age = time(NULL);
	return node;
}


/*
 * remove the least recently used cache block.
 * loop over all the cached node, find the one
 * that has oldest age and remove it from list
 */
void evict(cache *c){
	if(c->head==NULL)
		return;
	time_t oldest;
	block *old, *cur;
	old = NULL;
	cur = c->head;
	while(cur!=NULL){
		if(old==NULL){
			old = cur;
			oldest = cur->age;
		}else if(cur->age<oldest){
			old = cur;
			oldest = cur->age;
		}
		cur = cur->next;
	}
	block *prev, *next;
	prev = old->prev;
	next = old->next;
	if(prev!=NULL)
		prev->next = next;
	else if(next!=NULL)
		next->prev = prev;
	else
		c->head = NULL;
	Free(cur->uri);
	Free(cur->content);
	Free(cur);
}


/*
 * clear the cache, free all the allocated memory
 */
void clear(cache *c){
    if (c->head==NULL) {
        Free(c);
        return;
    }
    block *prev, *cur;
    cur = c->head;
    while (cur!=NULL) {
        prev = cur;
        cur = cur->next;
        Free(prev->uri);
        Free(prev->content);
        Free(prev);
    }
    
    Free(c);
}
