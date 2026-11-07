#include "startup_retry_support.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace esphome::sendspin_ {
#include "actual_hub_methods.inc"
}  // namespace esphome::sendspin_

using namespace esphome::sendspin_;

static void tick(SendspinHub &hub, uint32_t now) {
  App.now = now;
  hub.loop();
}

static void run_case(int scenario) {
  App.now = 0;
  if (scenario == 0) {
    TestClient client{{{false, true}}};
    SendspinHub hub(client);
    hub.set_enabled(true);
    tick(hub, 0);
    assert(client.starts == 1 && hub.error && !hub.warning);
    tick(hub, 50000);
    hub.set_enabled(false);  // Default zero preserves the old terminal behavior.
    hub.set_enabled(true);
    tick(hub, 60000);
    assert(client.starts == 1 && hub.enabled_ == true && hub.error);
    return;
  }
  if (scenario == 1) {
    TestClient client{{{true, false}, {true, false}}};
    SendspinHub hub(client);
    hub.set_enabled(true);
    tick(hub, 1);
    assert(client.running && client.starts == 1 && !hub.error && !hub.warning);
    tick(hub, 9000);
    assert(client.starts == 1);
    hub.set_enabled(false);
    tick(hub, 9001);
    tick(hub, 9002);
    assert(client.stops == 1 && !client.running);
    hub.set_enabled(true);
    tick(hub, 9003);
    assert(client.starts == 2 && client.running);
    return;
  }
  if (scenario == 2) {
    TestClient client{{{true, false}}};
    SendspinHub hub(client);
    tick(hub, 0);  // Missing enabled state is not an implicit enable.
    assert(client.starts == 0);
    hub.set_enabled(true);
    hub.unpaired_access_.reset();
    tick(hub, 1);
    assert(client.starts == 0);
    hub.unpaired_access_ = false;  // A known false is still a configured policy.
    tick(hub, 2);
    assert(client.starts == 1 && client.running);
    return;
  }
  if (scenario == 3) {
    TestClient client{{{true, false}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    tick(hub, 0);
    hub.on_shutdown();
    tick(hub, 10000);
    assert(client.stops == 1 && client.starts == 1 && !client.running);
    assert(hub.enabled_ == false && !hub.start_retry_.pending());
    return;
  }
  if (scenario == 4) {
    SendspinStartRetry retry;
    assert(!retry.enabled() && retry.ready(0));
    assert(!retry.failed(0, true) && retry.terminal());
    assert(!retry.ready(std::numeric_limits<uint32_t>::max()));
    retry.reset();
    retry.set_limit(4);
    assert(retry.enabled());
    assert(retry.failed(100, true) && retry.attempts() == 1);
    assert(!retry.ready(1099) && retry.ready(1100));
    assert(retry.failed(1100, true) && retry.retry_delay() == 2000);
    assert(!retry.ready(3099) && retry.ready(3100));
    assert(retry.failed(3100, true) && retry.retry_delay() == 4000);
    assert(retry.failed(7100, true) && retry.attempts() == 4);
    assert(!retry.failed(11100, true) && retry.terminal());
    retry.reset();
    assert(retry.ready(0) && retry.attempts() == 0 && !retry.pending());
    assert(!retry.failed(1, false) && retry.terminal());
    return;
  }
  if (scenario == 5) {
    TestClient client{{{false, true}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    tick(hub, 0);
#if CONFIG_ESPHOME_SENDSPIN_CHECKED_THREADS
    assert(hub.warning && !hub.error && hub.start_retry_.pending());
#else
    assert(hub.error && !hub.warning && !hub.start_retry_.pending());
    tick(hub, 50000);  // Without typed failures the Hub must not guess transient.
    assert(client.starts == 1);
#endif
    return;
  }
#if CONFIG_ESPHOME_SENDSPIN_CHECKED_THREADS
  if (scenario == 6) {
    TestClient client{{{false, true}, {false, true}, {false, true}, {false, true}, {true, false}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    const uint32_t attempts[]{100, 1100, 3100, 7100, 11100};
    for (size_t index = 0; index < 5; ++index) {
      if (index != 0) {
        tick(hub, attempts[index] - 1);
        assert(client.starts == index);
      }
      tick(hub, attempts[index]);
      assert(client.starts == index + 1);
    }
    assert(client.running && !hub.warning && !hub.error);
    assert(!hub.start_retry_.pending() && hub.start_retry_.attempts() == 0);
    tick(hub, 50000);
    assert(client.starts == 5);
    return;
  }
  if (scenario == 7) {
    TestClient client{{{false, true}, {false, true}, {false, true}, {false, true}, {false, true}, {true, false}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    for (const uint32_t now : {0U, 1000U, 3000U, 7000U, 11000U}) {
      hub.set_enabled(true);  // Repeated true cannot replenish the budget.
      tick(hub, now);
    }
    assert(client.starts == 5 && hub.error && hub.start_retry_.terminal() && !hub.warning);
    hub.set_enabled(true);
    tick(hub, 50000);
    assert(client.starts == 5);
    hub.set_enabled(false);  // An explicit off/on starts a new bounded series.
    assert(!hub.error && hub.enabled_ == false);
    tick(hub, 50001);
    assert(client.starts == 5);
    hub.set_enabled(true);
    tick(hub, 50002);
    assert(client.starts == 6 && client.running && !hub.error);
    return;
  }
  if (scenario == 8) {
    TestClient client{{{false, false}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    tick(hub, 0);
    assert(hub.error && hub.start_retry_.terminal() && hub.start_retry_.attempts() == 0);
    tick(hub, 10000);
    assert(client.starts == 1);
    return;
  }
  if (scenario == 9) {
    TestClient client{{{false, true}, {false, false}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    tick(hub, 0);
    tick(hub, 1000);
    assert(client.starts == 2 && hub.error && !hub.warning && !hub.start_retry_.pending());
    tick(hub, 10000);
    assert(client.starts == 2);  // Old transient status cannot override a new permanent failure.
    return;
  }
  if (scenario == 10) {
    TestClient client{{{false, true}, {true, false}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    tick(hub, 0);
    hub.set_enabled(false);
    tick(hub, 50000);
    assert(client.starts == 1 && !hub.warning && !hub.start_retry_.pending());
    hub.set_enabled(true);
    tick(hub, 50001);
    assert(client.starts == 2 && client.running);
    return;
  }
  if (scenario == 11) {
    TestClient client{{{false, true}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    tick(hub, 0);
    hub.on_shutdown();
    tick(hub, 50000);
    assert(client.starts == 1 && client.stops == 0 && hub.enabled_ == false);
    assert(!hub.start_retry_.pending());
    return;
  }
  if (scenario == 12) {
    TestClient client{{{false, true}, {true, false}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    const uint32_t failed = std::numeric_limits<uint32_t>::max() - 500;
    tick(hub, failed);
    tick(hub, failed + 999);
    assert(client.starts == 1);
    tick(hub, failed + 1000);
    assert(client.starts == 2 && client.running && !hub.warning);
    return;
  }
  if (scenario == 13) {
    TestClient client{{{false, true}, {true, false}, {false, true}, {true, false}}};
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    tick(hub, 0);
    tick(hub, 1000);
    hub.set_enabled(false);
    tick(hub, 1001);
    hub.set_enabled(true);
    tick(hub, 1002);
    assert(client.starts == 3 && hub.start_retry_.attempts() == 1);
    tick(hub, 2001);
    assert(client.starts == 3);
    tick(hub, 2002);
    assert(client.starts == 4 && client.running && client.stops == 1);
    return;
  }
  if (scenario == 14) {
    TestClient client;
    SendspinHub hub(client);
    hub.start_retry_.set_limit(4);
    hub.set_enabled(true);
    hub.status_set_error("unrelated error");
    hub.set_enabled(false);
    tick(hub, 10000);
    assert(hub.error && client.starts == 0 && hub.enabled_ == false);
    hub.set_enabled(true);
    assert(hub.enabled_ == false && hub.error);
    return;
  }
#endif
  assert(false && "unknown test scenario");
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const int scenario = std::atoi(argv[1]);
  run_case(scenario);
  std::printf("checked %d case %d PASS\n", CONFIG_ESPHOME_SENDSPIN_CHECKED_THREADS, scenario);
}
