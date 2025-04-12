#include "cachelab.h"
#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
A block can be 2 numbers denoting the range [L, R]
A line is a block, tag and valid bit
A set is an array of lines
A cache is an array of sets
*/

unsigned long long global_time = 0;

typedef enum set_probe_result {
  PROBE_HIT = 0,
  PROBE_MISS = 1
} set_probe_result_t;

typedef enum operation {
  INSTRUCTION_LOAD = 0,
  DATA_LOAD = 1,
  DATA_STORE = 2, // S
  MODIFY = 3,
} operation_t;

// block logic Begin
typedef struct block {
  size_t l;
  size_t r;
} block_t;

void flush_and_fetch_new_block(block_t *block, size_t new_l,
                               size_t block_bits) {
  block->l = new_l;
  block->r = new_l + (1 << (block_bits+1));
}

block_t *construct_block() {
  block_t *new_block = (block_t *)malloc(sizeof(block_t));

  new_block->l = 0;
  new_block->r = 0;

  return new_block;
}

void break_down_block(block_t *block) { free(block); }

// block section end

// Line section begin
typedef struct Line {
  int valid;
  size_t tag;

  unsigned long long last_access_time;
  block_t *block;
} line_t;

line_t construct_line() {
  line_t line = {.valid = 0, .tag = 0};
  line.block = construct_block();

  return line;
}

int is_address_in_line(struct Line *line, size_t tag, operation_t operation) {
  int isInLine = 0;

  if (line->valid && line->tag == tag)
    isInLine = 1;
  if (isInLine /*&& operation == DATA_STORE*/)
    line->last_access_time = ++global_time;

  return isInLine;
}

void load_new_block_in_line(line_t *line, size_t tag, size_t address,
                            size_t block_bits) {
  line->tag = tag;
  line->valid = 1;
  flush_and_fetch_new_block(line->block, address, block_bits);
}

void break_down_line(line_t *line) {
  break_down_block(line->block);

  free(line);
}

// Line section end

// Set section start
typedef struct set {
  size_t line_count; // Associativity (E)
  line_t *lines;
} set_t;

set_t construct_set(size_t line_count) {
  set_t set;

  set.line_count = line_count;
  set.lines = malloc(sizeof(line_t) * line_count);

  if (!set.lines) {
    perror("line malloc failure");
    exit(EXIT_FAILURE);
  }

  for (size_t i = 0; i < line_count; i++) {
    set.lines[i] = construct_line();
  }

  return set;
}

void break_down_set(set_t *set) {
  for (size_t i = 0; i < set->line_count; i++) {
    break_down_line(&set->lines[i]);
  }

  // free(set->lines);
  free(set);
}

set_probe_result_t probe_set_for_memory(set_t *set, size_t tag,
                                        size_t *hit_line,
                                        operation_t operation) {
  set_probe_result_t probeResult = PROBE_MISS;

  size_t line_count = set->line_count;

  for (size_t i = 0; i < line_count; i++) {
    struct Line *line = &set->lines[i];
    if (is_address_in_line(line, tag, operation)) {
      probeResult = PROBE_HIT;
      *hit_line = i;
    }
  }

  return probeResult;
};

void getblockFromSet(set_t *set) {} // NOTE: not required

size_t line_to_evict(set_t *set) {
  // evict based on a cache eviction policy
  // printf("evicting %ld\n", set->line_count);
  size_t target_line = -1;
  unsigned long long min_time = INT_MAX;
  for (size_t i = 0; i < set->line_count; i++) {
    line_t line = set->lines[i];
    if (line.last_access_time <= min_time) {
      min_time = line.last_access_time;
      target_line = i;
    }
  }
  return target_line;
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
                      size_t block_bits, enum set_probe_result *probe_result,
                      operation_t operation, int *did_evict) {
  size_t hit_line = 0;
  *probe_result = probe_set_for_memory(set, tag, &hit_line, operation);

  if (*probe_result == PROBE_MISS) {
    size_t line_to_load_into = -1;
    *did_evict = should_set_evict(set, &line_to_load_into);
    if (*did_evict)
      line_to_load_into = line_to_evict(set);
    assert(line_to_load_into != -1);
    load_new_block_in_line(&set->lines[line_to_load_into], tag, address,
                           block_bits);
    set->lines[line_to_load_into].last_access_time = ++global_time;
  }
}

// Set section end

// Cache section start

typedef struct cache {
  int s; // setIndexBits
  int b; // blockBits
  set_t *sets;
} cache_t;

void execute_operation_in_cache(
    cache_t *cache, operation_t operation, size_t address,
    size_t size,        // NOTE: ignore the size and assume the
    unsigned int *miss, // accesses are aligned perfectly
    unsigned int *hit, unsigned int *eviction) {
  size_t set_mask = (1UL << cache->s) - 1; // TODO: get set mask
  size_t block_mask = (1UL << cache->b) - 1;

  size_t offset = block_mask & address;
  size_t setIndex = set_mask & (address >> cache->b);

  size_t tag = address >> (cache->s + cache->b);

  printf("set: %lx tag: %lx offset: %lx  ", setIndex, tag, offset);

  set_probe_result_t probe_result = PROBE_MISS;
  int did_evict = 0;
  handle_operation(&cache->sets[setIndex], tag, offset, cache->b, address,
                   &probe_result, operation, &did_evict);
  if (did_evict)
    *eviction = 1;
  switch (probe_result) {
  case PROBE_MISS:
    *miss = 1;
    break;
  case PROBE_HIT:
    *hit = 1;
    break;
  }

  if (operation == MODIFY) {
    *hit += 1;
  }
}

cache_t *construct_cache(size_t s, size_t b, size_t e) {
  cache_t *cache = malloc(sizeof(cache_t));

  size_t set_count = 1 << s;
  cache->sets = malloc(sizeof(set_t) * set_count);
  cache->s = s;
  cache->b = b;

  for (size_t i = 0; i < set_count; i++) {
    cache->sets[i] = construct_set(e);
  }

  return cache;
}

void break_down_cache(cache_t *cache) {
  for (size_t i = 0; i < 1 << cache->s; i++) {
    break_down_set(&cache->sets[i]);
  }

  // free(cache->sets);
  free(cache);
}

// Cache section end

// Csim start

typedef struct cache_simulator {
  // int e;          // associativity
  bool is_verbose; // 0 -> concise

  unsigned int hits;
  unsigned int misses;
  unsigned int evictions;

  cache_t *cache;
} cache_simulator_t;

void get_address_and_mem_size(char *instruction, size_t *address,
                              size_t *size) {
  instruction += 2;
  char *token = strtok(instruction, ",");

  *address = strtoul(token, NULL, 16);

  token = strtok(NULL, ",");
  *size = strtoul(token, NULL, 16);
};

void simulate_trace(cache_simulator_t *cache_simulator, char *instruction) {
  if (instruction[0] == ' ') {
    instruction += 0x1;
  }
  char op_char = instruction[0];
  operation_t operation = INSTRUCTION_LOAD;

  switch (op_char) {
  case 'I':
    operation = INSTRUCTION_LOAD;
    return;
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

  unsigned int miss, hit, eviction = 0;
    execute_operation_in_cache(cache_simulator->cache, operation, address, size,
                               &miss, &hit, &eviction);

  if (cache_simulator->is_verbose)
    printf("Verbose: %s %d %d %d\n", instruction, miss, hit, eviction);

  cache_simulator->misses += miss;
  cache_simulator->hits += hit;
  cache_simulator->evictions += eviction;
}

cache_simulator_t *construct_cache_simulator(size_t s, size_t b, size_t e,
                                             bool is_verbose) {
  cache_simulator_t *cache_simulator = malloc(sizeof(cache_simulator_t));

  cache_simulator->misses = 0;
  cache_simulator->hits = 0;
  cache_simulator->evictions = 0;
  cache_simulator->is_verbose = is_verbose;
  cache_simulator->cache = construct_cache(s, b, e);

  return cache_simulator;
}

void break_down_cache_simulator(cache_simulator_t *cache_simulator) {
  break_down_cache(cache_simulator->cache);
  free(cache_simulator);
}

// Csim start

int main(int argc, char *argv[]) {
  size_t s, b, e;
  bool is_verbose = false;
  char *trace_file = NULL;

  int opt;
  while ((opt = getopt(argc, argv, "s:E:b:vt:")) != -1) {
    switch (opt) {
    case 's':
      s = atoi(optarg);
      break;
    case 'E':
      e = atoi(optarg);
      break;
    case 'b':
      b = atoi(optarg);
      break;
    case 'v':
      is_verbose = true;
      break;
    case 't':
      trace_file = optarg;
      break;
    default:
      fprintf(stderr, "Usage: %s -s <s> -E <E> -b <b> -t <tracefile> [-v]\n",
              argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  if (is_verbose)
    printf("Debug mode: %d\n", is_verbose);
  cache_simulator_t *csim = construct_cache_simulator(s, b, e, is_verbose);

  FILE *file = fopen(trace_file, "r");

  if (!file) {
    perror("failed to open trace file");
    exit(EXIT_FAILURE);
  }

  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  while ((read = getline(&line, &len, file)) != -1)
    simulate_trace(csim, line);

  free(line);
  fclose(file);

  printSummary(csim->hits, csim->misses, csim->evictions);
  // break_down_cache_simulator(csim);
  return 0;
}
