#ifndef CACHE_H
#define CACHE_H

#include <time.h>
#include <pthread.h>
#include <stddef.h>      
#include <sys/types.h>   
#include <stdlib.h>     
#include <string.h>      


#define CACHE_MAX_SIZE (100 * 1024 * 1024) 

typedef struct CacheEntry {
    char* url;
    char* data;
    size_t size;
    size_t capacity;
    int status_code;
    char* content_type;
    int complete;

    pthread_mutex_t mtx;
    pthread_cond_t cv;

    time_t last_access;
    struct CacheEntry* next;
    struct CacheEntry* prev;
} CacheEntry;

typedef struct {
    CacheEntry* head; 
    CacheEntry* tail; 
    
    pthread_mutex_t mtx;
    pthread_cond_t cv;

    size_t total_size;
    size_t max_size;
    int should_exit; 
} Cache;

extern Cache g_cache;

CacheEntry* cache_get_or_create(const char* url);
void cache_append(CacheEntry* entry, const char* data, size_t len);
void cache_mark_complete(CacheEntry* entry, int status_code, const char* content_type);
void cache_touch(CacheEntry* entry); 
void* cache_gc_thread(void* arg);

void cache_print_stats(void);

#endif