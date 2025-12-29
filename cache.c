#include "cache.h"
#include <stdlib.h>      
#include <string.h>      
#include <stdio.h>       
#include <unistd.h>      
#include <time.h>       


Cache g_cache = {0};

static CacheEntry* find_entry(const char* url) {
    CacheEntry* cur = g_cache.head;
    while (cur) {
        if (strcmp(cur->url, url) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

static void move_to_head(CacheEntry* entry) {
    if (entry == g_cache.head) return;
    if (entry->prev) entry->prev->next = entry->next;
    if (entry->next) entry->next->prev = entry->prev;
    if (entry == g_cache.tail) g_cache.tail = entry->prev;

    entry->next = g_cache.head;
    entry->prev = NULL;
    if (g_cache.head) g_cache.head->prev = entry;
    g_cache.head = entry;
    if (!g_cache.tail) g_cache.tail = entry;
}

CacheEntry* cache_get_or_create(const char* url) {
    pthread_mutex_lock(&g_cache.mtx);

    CacheEntry* entry = find_entry(url);
    if (entry) {
        cache_touch(entry);
        pthread_mutex_unlock(&g_cache.mtx);
        return entry;
    }

    entry = calloc(1, sizeof(CacheEntry));
    entry->url = strdup(url);
    entry->capacity = 4096;
    entry->data = malloc(entry->capacity);
    pthread_mutex_init(&entry->mtx, NULL);
    pthread_cond_init(&entry->cv, NULL);
    cache_touch(entry);

    move_to_head(entry);
    g_cache.total_size += entry->capacity;

    if (g_cache.total_size > g_cache.max_size * 0.9) {
        pthread_cond_signal(&g_cache.cv);
    }

    pthread_mutex_unlock(&g_cache.mtx);
    return entry;
}

void cache_touch(CacheEntry* entry) {
    entry->last_access = time(NULL);
}

void cache_append(CacheEntry* entry, const char* data, size_t len) {
    pthread_mutex_lock(&entry->mtx);

    if (entry->size + len > entry->capacity) {
        while (entry->size + len > entry->capacity) {
            entry->capacity *= 2;
        }
        entry->data = realloc(entry->data, entry->capacity);
    }

    memcpy(entry->data + entry->size, data, len);
    entry->size += len;
    cache_touch(entry); 

    pthread_cond_broadcast(&entry->cv);
    //--------------------------------------------------------------------------/
    static size_t counter = 0;
    if (++counter % 2 == 0) {
        cache_print_stats();
    }
    //--------------------------------------------------------------------------//
    pthread_mutex_unlock(&entry->mtx);
}

void cache_mark_complete(CacheEntry* entry, int status_code, const char* content_type) {
    pthread_mutex_lock(&entry->mtx);
    entry->status_code = status_code;
    entry->content_type = content_type ? strdup(content_type) : NULL;
    entry->complete = (status_code == 200) ? 1 : 0;
    pthread_mutex_unlock(&entry->mtx);
}

void* cache_gc_thread(void* arg) {
    (void)arg;
    pthread_mutex_lock(&g_cache.mtx);
    while (!g_cache.should_exit) {
        while (g_cache.total_size <= g_cache.max_size * 0.9 && !g_cache.should_exit) {
            pthread_cond_wait(&g_cache.cv, &g_cache.mtx);
        }

        while (g_cache.total_size > g_cache.max_size && g_cache.tail) {
            CacheEntry* victim = g_cache.tail;
            g_cache.tail = victim->prev;
            if (g_cache.tail) g_cache.tail->next = NULL;
            if (victim == g_cache.head) g_cache.head = NULL;

            g_cache.total_size -= victim->capacity;
            free(victim->data);
            free(victim->url);
            free(victim->content_type);
            pthread_mutex_destroy(&victim->mtx);
            pthread_cond_destroy(&victim->cv);
            free(victim);
        }
    }
    pthread_mutex_unlock(&g_cache.mtx);
    return NULL;
}


#include <stdio.h>

void cache_print_stats(void) {
    pthread_mutex_lock(&g_cache.mtx);
    double used_kb = (double)g_cache.total_size / (1024);
    double max_kb = (double)g_cache.max_size / (1024);
    double percent = (g_cache.max_size > 0) ? (100.0 * g_cache.total_size / g_cache.max_size) : 0.0;

    size_t count = 0;
    CacheEntry* cur = g_cache.head;
    while (cur) {
        count++;
        cur = cur->next;
    }

    fprintf(stderr, "[PROXY] Cache: %.1f KB / %.1f KB (%.0f%%) | Entries: %zu\n",
            used_kb, max_kb, percent, count);
    fflush(stderr);
    pthread_mutex_unlock(&g_cache.mtx);
}