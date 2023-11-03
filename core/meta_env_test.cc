// No copyright
#include "core/meta_env.hh"

#include <gtest/gtest.h>

#include "iface/env.hh"


static inline bool MatchPair(
    const std::optional<nf7::subsys::MetaEnv::Pair>& a,
    const nf7::subsys::MetaEnv::Pair& b) {
  return a && a->first == b.first && &a->second == &b.second;
}

TEST(MetaEnv, FindOrByName) {
  const auto a = nf7::LazyEnv::Make();
  const auto b = nf7::LazyEnv::Make();
  const auto c = nf7::LazyEnv::Make();

  nf7::core::MetaEnv sut {
    {
      {"b", b},
      {"c", c},
      {"a", a},
    },
  };

  EXPECT_EQ(sut.FindOr(""), nullptr);
  EXPECT_EQ(sut.FindOr("a"), a.get());
  EXPECT_EQ(sut.FindOr("b"), b.get());
  EXPECT_EQ(sut.FindOr("c"), c.get());
  EXPECT_EQ(sut.FindOr("d"), nullptr);
}

TEST(MetaEnv, FindOrByIndex) {
  const auto a = nf7::LazyEnv::Make();
  const auto b = nf7::LazyEnv::Make();
  const auto c = nf7::LazyEnv::Make();

  nf7::core::MetaEnv sut {
    {
      {"b", b},
      {"c", c},
      {"a", a},
    },
  };

  EXPECT_TRUE(MatchPair(sut.FindOr(0), {"a", *a}));
  EXPECT_TRUE(MatchPair(sut.FindOr(1), {"b", *b}));
  EXPECT_TRUE(MatchPair(sut.FindOr(2), {"c", *c}));
  EXPECT_EQ(sut.FindOr(3), std::nullopt);
}

TEST(MetaEnv, FetchAll) {
  const auto a = nf7::LazyEnv::Make();
  const auto b = nf7::LazyEnv::Make();
  const auto c = nf7::LazyEnv::Make();

  nf7::core::MetaEnv sut {
    {
      {"b", b},
      {"c", c},
      {"a", a},
    },
  };

  const auto all = sut.FetchAll();
  EXPECT_EQ(all.size(), 3);
  EXPECT_TRUE(MatchPair(all[0], {"a", *a}));
  EXPECT_TRUE(MatchPair(all[1], {"b", *b}));
  EXPECT_TRUE(MatchPair(all[2], {"c", *c}));
}
