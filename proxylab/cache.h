/*
 *csapp.h - prototypes and definitions for cache implementation
 */
#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"
#include <time.h>
#include <semaphore.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


/* cache data structures */
typedef struct cache_block{
	struct cache_block *next;
	struct cache_block *prev;
	char* uri;
	char* content;
    time_t age;
	int size;
} block;
typedef struct cache_list{
	struct cache_block *head;
	int size;
} cache;


/* cache functions */
cache *cache_init();
block *find_cache(cache* c, char* uri);
void evict(cache *c);
void cache_insert(cache* c, char *uri, char *content, int size);
void clear(cache *c);

#endif