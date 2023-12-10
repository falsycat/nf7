// No copyright
#include "util/signal.h"

#include "test/common.h"

static void on_recv_(struct nf7util_signal_recv* recv) {
  uint32_t* cnt = recv->data;
  ++*cnt;
}


NF7TEST(nf7util_signal_test_emit) {
  uint32_t cnt = 0;

  struct nf7util_signal signal = {0};
  nf7util_signal_init(&signal, test_->malloc);

  struct nf7util_signal_recv recv = {
    .data = &cnt,
    .func = on_recv_,
  };
  nf7util_signal_recv_set(&recv, &signal);

  nf7util_signal_emit(&signal);
  if (!nf7test_expect(1 == cnt)) {
    return false;
  }

  nf7util_signal_deinit(&signal);
  return true;
}

NF7TEST(nf7util_signal_test_emit_after_unset) {
  uint32_t cnt = 0;

  struct nf7util_signal signal = {0};
  nf7util_signal_init(&signal, test_->malloc);

  struct nf7util_signal_recv recv = {
    .data = &cnt,
    .func = on_recv_,
  };
  nf7util_signal_recv_set(&recv, &signal);
  nf7util_signal_recv_unset(&recv);

  nf7util_signal_emit(&signal);
  if (!nf7test_expect(0 == cnt)) {
    return false;
  }

  nf7util_signal_deinit(&signal);
  return true;
}

NF7TEST(nf7util_signal_test_del_after_set) {
  uint32_t cnt = 0;

  struct nf7util_signal signal = {0};
  nf7util_signal_init(&signal, test_->malloc);

  struct nf7util_signal_recv recv = {
    .data = &cnt,
    .func = on_recv_,
  };
  nf7util_signal_recv_set(&recv, &signal);
  nf7util_signal_deinit(&signal);

  return nf7test_expect(0 == cnt);
}
