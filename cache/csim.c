#include "cachelab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
A block can be 2 numbers denoting the range [L, R]
A line is a block, tag and valid bit
A set is an array of lines
A cache is an array of sets
*/

typedef enum set_probe_result {
  PROBE_HIT = 0,
  PROBE_MISS = 1
} set_probe_result;

typedef enum Operation {
  INSTRUCTION_LOAD = 0,
  DATA_LOAD = 1,
  DATA_STORE = 2, // S
  MODIFY = 3,
} operation;

// block logic Begin
typedef struct block {
  size_t block_bits;
  size_t _l;
  size_t _r;
} block_t;

void flush_and_fetch_new_block(block_t *block, size_t new_l) {
  block->_l = new_l;
  block->_r = new_l + (1 << (block->block_bits + 1));
}

// block section end

// Line section begin
typedef struct Line {
  int valid;
  size_t tag;

  block_t block;
} line_t;

int is_address_in_line(struct Line *line, size_t tag) {
  int isInLine = 0;

  if (line->valid && line->tag == tag)
    isInLine = 1;

  return isInLine;
}

void load_new_block_in_line(line_t *line, size_t tag, size_t address) {
  line->tag = tag;
  line->valid = 1;
  flush_and_fetch_new_block(&line->block, address);
}

// Line section end

// Set section start
typedef struct set {
  size_t line_count; // Associativity (E)
  struct Line lines[];
} set_t;

set_probe_result probe_set_for_memory(set_t *set, size_t tag, size_t *hit_line) {
  set_probe_result probeResult = PROBE_MISS;

  size_t line_count = set->line_count;

  for (size_t i = 0; i < line_count; i++) {
    struct Line *line = &set->lines[i];
    if (is_address_in_line(line, tag)) {
      probeResult = PROBE_HIT;
      *hit_line = i;
    }
  }

  return probeResult;
};

void getblockFromSet(set_t *set) {} // NOTE: not required

size_t line_to_evict(set_t *set) {
  // evict based on a cache eviction policy
  return 0;
}

int should_set_evict(set_t *set, size_t *line_to_load_into) {
  int should_evict = 1;

  for (size_t i = 0; i < set->line_count; i++) {
    should_evict = should_evict & set->lines[i].valid;
    if (!set->lines[i].valid)
      *line_to_load_into = i;
  }

  return should_evict;
}

void handle_operation(set_t *set, size_t tag, size_t offset, size_t address,
                      enum set_probe_result *probe_result, int *did_evict) {
  size_t hit_line = 0;
  *probe_result = probe_set_for_memory(set, tag, &hit_line);

  if (*probe_result == PROBE_MISS) {
    size_t line_to_load_into = -1;
    *did_evict = should_set_evict(set, &line_to_load_into);
    if (*did_evict)
      line_to_load_into = line_to_evict(set);
    assert(line_to_load_into!=-1);
    load_new_block_in_line(&set->lines[line_to_load_into], tag, address);
  }
}

// Set section end

// Cache section start

typedef struct cache {
  int s; // setIndexBits
  int b; // blockBits
  set_t sets[];
} cache_t;

void execute_operation_in_cache(
    cache_t *cache, enum Operation operation, size_t address,
    size_t size,          // NOTE: ignore the size and assume the
    unsigned int *misses, // accesses are aligned perfectly
    unsigned int *hits, unsigned int *evictions) {
  size_t set_mask = 0LL; // TODO: get set mask
  size_t setIndex = set_mask & address;

  size_t tag_mask = 0LL;
  size_t tag = tag_mask & address;

  size_t block_mask = 0LL;
  size_t offset = block_mask & address;

  set_probe_result probe_result= PROBE_MISS;
  int did_evict = 0;
  handle_operation(&cache->sets[setIndex], tag, offset, address, &probe_result, &did_evict);
}

// Cache section end

// Csim start

typedef struct cache_simulator {
  int e;         // associativity
  int isVerbose; // 0 -> concise
  cache_t cache;
} cache_simulator_t;

void get_address_and_mem_size(char *instruction, size_t *address,
                              size_t *size) {
  // split the instruction and load into address and size variables
  char *token = strtok(instruction, ",");

  printf("%s", token);
  *address = atoi(token);

  token = strtok(NULL, ",");
  printf("%s", token);
  *size = atoi(token);
};

void simulate_trace(cache_simulator_t *cache_simulator, char *instruction) {
  // if(strlen(instruction))return;
  if (instruction[0] == ' ') {
    *instruction += 1;
  }
  char op_char = instruction[0];
  enum Operation operation = INSTRUCTION_LOAD;

  switch (op_char) {
  case 'I':
    operation = INSTRUCTION_LOAD;
    break;
  case 'L':
    operation = DATA_LOAD;
    break;
  case 'M':
    operation = MODIFY;
    break;
  case 'S':
    operation = DATA_STORE;
    break;
  }
  // ignore INSTRUCTION_LOAD operation (do a no_op in the method)
  size_t address, size;

  get_address_and_mem_size(instruction, &address, &size);

  unsigned int misses, hits, evictions = 0;
  execute_operation_in_cache(&cache_simulator->cache, operation, address, size,
                             &misses, &hits, &evictions);
}

// Csim start

int main() {
  printSummary(0, 0, 0);
  return 0;
}
