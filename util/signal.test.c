// No copyright
#include "util/signal.h"

#include "test/common.h"

static void on_recv_(struct nf7_util_signal_recv* recv) {
  uint32_t* cnt = recv->data;
  ++*cnt;
}


NF7_TEST(nf7_util_signal_test_emit) {
  uint32_t cnt = 0;

  struct nf7_util_signal signal = {
    .malloc = test_->malloc,
  };
  nf7_util_signal_init(&signal);

  struct nf7_util_signal_recv recv = {
    .data = &cnt,
    .func = on_recv_,
  };
  nf7_util_signal_recv_set(&recv, &signal);

  nf7_util_signal_emit(&signal);
  if (!nf7_test_expect(1 == cnt)) {
    return false;
  }

  nf7_util_signal_deinit(&signal);
  return true;
}

NF7_TEST(nf7_util_signal_test_emit_after_unset) {
  uint32_t cnt = 0;

  struct nf7_util_signal signal = {
    .malloc = test_->malloc,
  };
  nf7_util_signal_init(&signal);

  struct nf7_util_signal_recv recv = {
    .data = &cnt,
    .func = on_recv_,
  };
  nf7_util_signal_recv_set(&recv, &signal);
  nf7_util_signal_recv_unset(&recv);

  nf7_util_signal_emit(&signal);
  if (!nf7_test_expect(0 == cnt)) {
    return false;
  }

  nf7_util_signal_deinit(&signal);
  return true;
}

NF7_TEST(nf7_util_signal_test_del_after_set) {
  uint32_t cnt = 0;

  struct nf7_util_signal signal = {
    .malloc = test_->malloc,
  };
  nf7_util_signal_init(&signal);

  struct nf7_util_signal_recv recv = {
    .data = &cnt,
    .func = on_recv_,
  };
  nf7_util_signal_recv_set(&recv, &signal);
  nf7_util_signal_deinit(&signal);

  return nf7_test_expect(0 == cnt);
}
