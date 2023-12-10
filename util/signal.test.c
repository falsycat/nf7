// No copyright
#include "util/signal.h"

#include "util/log.h"

#include "test/common.h"

static void increment_on_recv_(struct nf7util_signal_recv* recv) {
  uint32_t* cnt = recv->data;
  ++*cnt;
}
static void set_on_recv_(struct nf7util_signal_recv* recv) {
  struct nf7util_signal_recv* secondary = recv->data;
  if (!nf7util_signal_recv_set(secondary, recv->signal)) {
    nf7util_log_error("failed to set secondary signal recv");
  }
}
static void unset_on_recv_(struct nf7util_signal_recv* recv) {
  nf7util_signal_recv_unset(recv);
}


NF7TEST(nf7util_signal_test_emit) {
  uint32_t cnt = 0;

  struct nf7util_signal signal = {0};
  nf7util_signal_init(&signal, test_->malloc);

  struct nf7util_signal_recv recv = {
    .data = &cnt,
    .func = increment_on_recv_,
  };
  nf7test_expect(nf7util_signal_recv_set(&recv, &signal));

  nf7util_signal_emit(&signal);
  nf7util_signal_deinit(&signal);

  return nf7test_expect(1 == cnt);
}

NF7TEST(nf7util_signal_test_emit_after_unset) {
  uint32_t cnt = 0;

  struct nf7util_signal signal = {0};
  nf7util_signal_init(&signal, test_->malloc);

  struct nf7util_signal_recv recv = {
    .data = &cnt,
    .func = increment_on_recv_,
  };
  nf7test_expect(nf7util_signal_recv_set(&recv, &signal));
  nf7util_signal_recv_unset(&recv);

  nf7util_signal_emit(&signal);
  nf7util_signal_deinit(&signal);

  return nf7test_expect(0 == cnt);
}

NF7TEST(nf7util_signal_test_del_after_set) {
  uint32_t cnt = 0;

  struct nf7util_signal signal = {0};
  nf7util_signal_init(&signal, test_->malloc);

  struct nf7util_signal_recv recv = {
    .data = &cnt,
    .func = increment_on_recv_,
  };
  nf7test_expect(nf7util_signal_recv_set(&recv, &signal));
  nf7util_signal_deinit(&signal);

  return nf7test_expect(0 == cnt);
}

NF7TEST(nf7util_signal_test_set_while_emit) {
  uint32_t cnt = 0;

  struct nf7util_signal signal = {0};
  nf7util_signal_init(&signal, test_->malloc);

  struct nf7util_signal_recv recv2 = {
    .data = &cnt,
    .func = increment_on_recv_,
  };
  struct nf7util_signal_recv recv1 = {
    .data = &recv2,
    .func = set_on_recv_,
  };

  const bool ret =
    nf7test_expect(nf7util_signal_recv_set(&recv1, &signal)) &&
    (nf7util_signal_emit(&signal), true) &&
    nf7test_expect(nullptr != recv1.signal) &&
    nf7test_expect(nullptr != recv2.signal) &&
    nf7test_expect(1 == cnt);

  nf7util_signal_deinit(&signal);
  return ret;
}

NF7TEST(nf7util_signal_test_unset_while_emit) {
  struct nf7util_signal signal = {0};
  nf7util_signal_init(&signal, test_->malloc);

  struct nf7util_signal_recv recv1 = {
    .func = unset_on_recv_,
  };
  struct nf7util_signal_recv recv2 = {
    .func = unset_on_recv_,
  };
  struct nf7util_signal_recv recv3 = {
    .func = unset_on_recv_,
  };

  const bool ret =
    nf7test_expect(nf7util_signal_recv_set(&recv1, &signal)) &&
    nf7test_expect(nf7util_signal_recv_set(&recv2, &signal)) &&
    nf7test_expect(nf7util_signal_recv_set(&recv3, &signal)) &&
    (nf7util_signal_emit(&signal), true) &&
    nf7test_expect(nullptr == recv1.signal) &&
    nf7test_expect(nullptr == recv2.signal) &&
    nf7test_expect(nullptr == recv3.signal);

  nf7util_signal_deinit(&signal);
  return ret;
}
