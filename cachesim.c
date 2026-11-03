#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

typedef struct CacheLine {
    int valid;
    unsigned long tag;
    int age;
} CacheLine;

typedef struct Set {
    CacheLine *lines;
} Set;

typedef struct Cache {
    int memReads;
    int memWrites;
    int cacheHits;
    int cacheMisses;

    //block size is always a power of 2
    //cache size is 2 * number of cache lines (C=SAB)
    Set *sets;
    //S = C/AB
    int setsSize;
    int blockSize;
    //direct mapped is only one line per set (direct)
    //n-way is n lines per set (assoc:n)
    //full is all lines in one set (assoc)

    //cache lines = C/2
    int linesPerSet;
    int cacheSize;
} Cache;

void read(Cache *cache, unsigned long address, int prefetch);
void write(Cache *cache, unsigned long address, int prefetch);
int log2impl(int x);
int powerOfTwoCheck(int x);
void createCache(Cache *cache, int cacheSize, int blockSize, char *assoc);
void fifo(Cache *cache, int setIndex, unsigned long tag);
void freeCache(Cache *cache);

int main(int argc, char **argv) {
    if (argc < 6 || argc > 6) {
        printf("Incorrect arguments");
        return EXIT_FAILURE;
    }

    int cacheSize = atoi(argv[1]);
    char *assoc = argv[2];
    //char *policy = argv[3]; //policy is always FIFO
    int blockSize = atoi(argv[4]);

    if (powerOfTwoCheck(cacheSize) == 0) {
        printf("Cache size is not a power of 2");
        return EXIT_FAILURE;
    }
    if (powerOfTwoCheck(blockSize) == 0) {
        printf("Block size is not a power of 2");
        return EXIT_FAILURE;
    }
    
    FILE *file = fopen(argv[5], "r");
    if (file == NULL) {
        printf("File not found");
        return EXIT_FAILURE;
    }

    Cache *withPF = malloc(sizeof(Cache));
    createCache(withPF, cacheSize, blockSize, assoc);

    Cache *withoutPF = malloc(sizeof(Cache));
    createCache(withoutPF, cacheSize, blockSize, assoc);

    char line[99];
    while (fgets(line, sizeof(line), file)) {
        char mode;
        unsigned long address;
        if (sscanf(line, "%*x: %c %lx", &mode, &address)) {
            if (mode == 'R') {
                read(withPF, address, 1);
                read(withoutPF, address, 0);
            } else {
                write(withPF, address, 1);
                write(withoutPF, address, 0);
            }
        } else { //EOF
            break;
        }
    }


    printf("Prefetch 0\n");
    printf("Memory reads: %d\n", withoutPF->memReads);
    printf("Memory writes: %d\n", withoutPF->memWrites);
    printf("Cache hits: %d\n", withoutPF->cacheHits);
    printf("Cache misses: %d\n", withoutPF->cacheMisses);

    printf("Prefetch 1\n");
    printf("Memory reads: %d\n", withPF->memReads);
    printf("Memory writes: %d\n", withPF->memWrites);
    printf("Cache hits: %d\n", withPF->cacheHits);
    printf("Cache misses: %d\n", withPF->cacheMisses);

    fclose(file);
    freeCache(withPF);
    freeCache(withoutPF);
    return EXIT_SUCCESS;
}

void createCache(Cache *cache, int cacheSize, int blockSize, char* assoc) {
    cache->memWrites = 0;
    cache->memReads = 0;
    cache->cacheHits = 0;
    cache->cacheMisses = 0;
    cache->cacheSize = cacheSize;
    cache->blockSize = blockSize;

    //only used for assoc:n
    int number = 0;

    if (strcmp(assoc, "direct") == 0) {
        cache->linesPerSet = 1;
    } else if (strcmp(assoc, "assoc") == 0) {
        cache->linesPerSet = cacheSize / blockSize;
    } else if (sscanf(assoc, "assoc:%d", &number)) {
        if (powerOfTwoCheck(number) == 0) {
            printf("n is not a power of 2");
            exit(EXIT_FAILURE);
        }
        cache->linesPerSet = number;
    } else {
        printf("Form of cache not recognized. Pick between direct, assoc, or assoc:n");
        exit(EXIT_FAILURE);
    }

    cache->setsSize = cacheSize / (blockSize * cache->linesPerSet);
    cache->sets = malloc(sizeof(Set) * cache->setsSize);
    for (int i = 0; i < cache->setsSize; i++) {
        cache->sets[i].lines = malloc(sizeof(CacheLine) * cache->linesPerSet);
        for (int x = 0; x < cache->linesPerSet; x++) {
            cache->sets[i].lines[x].age = 0;
            cache->sets[i].lines[x].tag = 0;
            cache->sets[i].lines[x].valid = 0;
        }
    }
}

void read(Cache *cache, unsigned long address, int prefetch) {
    unsigned long identifier = address >> (log2impl(cache->blockSize));
    int index = identifier & ((1 <<  log2impl(cache->setsSize)) - 1);
    unsigned long tag = identifier >> (log2impl(cache->setsSize));
    Set *set = &cache->sets[index];

    for (int i = 0; i < cache->linesPerSet; i++) {
        if (set->lines[i].valid == 1 && set->lines[i].tag == tag) {
            cache->cacheHits++;
            return;
        }
    }

    cache->memReads++;
    cache->cacheMisses++;
    fifo(cache, index, tag);

    if (prefetch == 1) {
        unsigned long next = identifier + 1;
        int index2 = next & ((1 << (log2impl(cache->setsSize))) - 1);
        unsigned long nextTag = next >> (log2impl(cache->setsSize));
        Set *nextSet = &cache->sets[index2];
    
        for (int i = 0; i < cache->linesPerSet; i++) {
            if (nextSet->lines[i].valid == 1 && nextSet->lines[i].tag == nextTag) {
                return;
            }
        }
        cache->memReads++;
        fifo(cache, index2, nextTag);
    }
}

void write(Cache *cache, unsigned long address, int prefetch) {
    unsigned long identifier = address >> (log2impl(cache->blockSize));
    int index = identifier & ((1 << (log2impl(cache->setsSize))) - 1);
    unsigned long tag = identifier >> (log2impl(cache->setsSize));
    Set *set = &cache->sets[index];

    for (int i = 0; i < cache->linesPerSet; i++) {
        if (set->lines[i].valid == 1 && set->lines[i].tag == tag) {
            cache->cacheHits++;
            cache->memWrites++;
            return;
        }
    }

    cache->memReads++;
    cache->cacheMisses++;
    cache->memWrites++;
    fifo(cache, index, tag);

    if (prefetch == 1) {
        unsigned long next = identifier + 1;
        int index2 = next & ((1 << (log2impl(cache->setsSize))) - 1);
        unsigned long nextTag = next >> (log2impl(cache->setsSize));
        Set *nextSet = &cache->sets[index2];
    
        for (int i = 0; i < cache->linesPerSet; i++) {
            if (nextSet->lines[i].valid == 1 && nextSet->lines[i].tag == nextTag) {
                return;
            }
        }    
        cache->memReads++;
        fifo(cache, index2, nextTag);
    }
}

void fifo(Cache *cache, int index, unsigned long tag) {
    Set *set = &cache->sets[index];
    int victim = -1;

    for (int i = 0; i < cache->linesPerSet; i++) {
        if (set->lines[i].valid == 0) {
            victim = i;
            break;
        }
    }

    if (victim == -1) {
        int oldest = 0;
        for (int i = 1; i < cache->linesPerSet; i++) {
            if (set->lines[i].age > set->lines[oldest].age) {
                oldest = i;
            } 
        }
        victim = oldest;
    }

    set->lines[victim].tag = tag;
    set->lines[victim].valid = 1;
    set->lines[victim].age = 0;

    for (int i = 0; i < cache->linesPerSet; i++) {
        if (i != victim && set->lines[i].valid) {
            set->lines[i].age++;
        }
    }
}

int powerOfTwoCheck(int x) {
    return x != 0 && (x & (x - 1)) == 0;
}

int log2impl(int x) {
    int result = 0;
    while (x >>= 1) {
        result++;
    }
    return result;
}

void freeCache(Cache *cache) {
    for (int i = 0; i < cache->setsSize; i++) {
        free(cache->sets[i].lines);
    }
    free(cache->sets);
    free(cache);
}