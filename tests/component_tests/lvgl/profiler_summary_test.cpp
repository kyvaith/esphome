#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include "profiler_summary.inc"

static ProfilerStackEntry stack_storage[PROFILER_MAX_STACK];
static ProfilerAggEntry aggregate_storage[PROFILER_MAX_AGG];

static void reset() {
  s_profiler_stack = stack_storage;
  s_profiler_aggs = aggregate_storage;
  std::memset(stack_storage, 0, sizeof(stack_storage));
  std::memset(aggregate_storage, 0, sizeof(aggregate_storage));
  s_profiler_stack_depth = s_profiler_agg_count = 0;
  s_profiler_parse_drops = s_profiler_stack_overflow = 0;
  s_profiler_first_us = s_profiler_last_us = 0;
}

static std::string event(int thread, unsigned us, char phase, const char *name) {
  char line[256];
  std::snprintf(line, sizeof(line), "   LVGL-%d [1] 1.%09u: tracing_mark_write: %c|1|%s\n", thread, us * 1000, phase,
                name);
  return line;
}

static const ProfilerAggEntry &aggregate(const char *name) {
  for (size_t index = 0; index < s_profiler_agg_count; index++)
    if (std::strcmp(s_profiler_aggs[index].name, name) == 0)
      return s_profiler_aggs[index];
  assert(false);
  return aggregate_storage[0];
}

int main() {
  reset();
  auto first = event(17, 10, 'B', "dispatch") + event(23, 12, 'B', "draw") + event(17, 20, 'E', "dispatch");
  profiler_flush_cb(first.c_str());
  assert(s_profiler_stack_depth == 1 && s_profiler_stack[0].thread_id == 23);
  assert(aggregate("dispatch").total_us == 10);
  profiler_flush_cb(event(23, 32, 'E', "draw").c_str());
  assert(aggregate("draw").total_us == 20 && s_profiler_stack_depth == 0 && s_profiler_parse_drops == 0);

  reset();
  // Identical names on separate threads must pair only with their own begin.
  auto same =
      event(17, 1, 'B', "task") + event(23, 2, 'B', "task") + event(17, 5, 'E', "task") + event(23, 8, 'E', "task");
  profiler_flush_cb(same.c_str());
  assert(aggregate("task").count == 2 && aggregate("task").total_us == 10 && aggregate("task").max_us == 6);
  assert(s_profiler_stack_depth == 0 && s_profiler_parse_drops == 0);

  reset();
  auto nested = event(-42, 1, 'B', "outer") + event(-42, 2, 'B', "inner") + event(23, 3, 'B', "parallel") +
                event(-42, 6, 'E', "inner") + event(-42, 8, 'E', "outer") + event(23, 12, 'E', "parallel");
  profiler_flush_cb(nested.c_str());
  assert(aggregate("inner").total_us == 4 && aggregate("outer").total_us == 7);
  assert(aggregate("parallel").total_us == 9 && s_profiler_stack_depth == 0 && s_profiler_parse_drops == 0);

  reset();
  profiler_flush_cb("# tracer: nop\nmalformed\n");
  profiler_flush_cb(" LVGL-invalid [0] 1.000000000: tracing_mark_write: B|1|bad\n");
  profiler_flush_cb(event(17, 5, 'E', "missing").c_str());
  assert(s_profiler_parse_drops == 3 && s_profiler_agg_count == 0);
  for (unsigned index = 0; index <= PROFILER_MAX_STACK; index++)
    profiler_flush_cb(event(17, index, 'B', "bounded").c_str());
  assert(s_profiler_stack_depth == PROFILER_MAX_STACK && s_profiler_stack_overflow == 1);
  std::cout << "PASS interleaved profiler scopes, multiline/chunked flushes, nesting and bounded failures\n";
}
