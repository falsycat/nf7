// No copyright
#include "iface/common/container.hh"

#include <gtest/gtest.h>

#include <string>

namespace {
class Object {
 public:
  virtual ~Object() = default;
};
using Lazy  = nf7::LazyContainer<Object>;
using Fixed = nf7::FixedContainer<Object>;

class IA : public Object { };
class IB : public Object { };

class A : public IA {
 public:
  explicit A(nf7::Container<Object>&) noexcept { }
};
class B : public IB {
 public:
  explicit B(nf7::Container<Object>& sut) { sut.Get(a_); }
 private:
  std::shared_ptr<IA> a_;
};
class BRecursive : public IB {
 public:
  explicit BRecursive(nf7::Container<Object>& sut) { sut.Get(b_); }
 private:
  std::shared_ptr<IB> b_;
};
}  // namespace


TEST(LazyContainer, FetchIsolated) {
  auto sut = Lazy::Make({ Lazy::MakeItem<IA, A>(), });
  auto ptr = sut->Get<IA>();
  EXPECT_TRUE(std::dynamic_pointer_cast<A>(ptr));
}
TEST(LazyContainer, FetchIsolatedTwice) {
  auto sut = Lazy::Make({ Lazy::MakeItem<IA, A>(), });
  auto prev = sut->Get<IA>();
  EXPECT_EQ(prev, sut->Get<IA>());
}
TEST(LazyContainer, FetchDepending) {
  auto sut = Lazy::Make({
    Lazy::MakeItem<IA, A>(),
    Lazy::MakeItem<IB, B>(),
  });
  auto ptr = sut->Get<IB>();
  EXPECT_TRUE(std::dynamic_pointer_cast<B>(ptr));
}
TEST(LazyContainer, FetchUnknown) {
  auto sut = Lazy::Make();
  EXPECT_THROW(sut->Get<IA>(), nf7::Exception);
}
TEST(LazyContainer, FetchUnknownDepending) {
  auto sut = Lazy::Make({ Lazy::MakeItem<IB, B>(), });
  EXPECT_THROW(sut->Get<IB>(), nf7::Exception);
}

TEST(LazyContainer, FetchWithFallback) {
  auto fb  = Lazy::Make({ Lazy::MakeItem<IA, A>(), });
  auto sut = Lazy::Make({}, fb);
  auto ptr = sut->Get<IA>();
  EXPECT_TRUE(std::dynamic_pointer_cast<A>(ptr));
}
TEST(LazyContainer, FetchUnknownWithFallback) {
  auto fb  = Lazy::Make();
  auto sut = Lazy::Make({}, fb);
  EXPECT_THROW(sut->Get<IA>(), nf7::Exception);
}

TEST(LazyContainer, ConstructWithSharedInstance) {
  class Ashared : public IA {
   public:
    Ashared(const std::shared_ptr<nf7::Container<Object>>&) { }
  };
  auto sut = Lazy::Make({ Lazy::MakeItem<IA, Ashared>(), });
  EXPECT_TRUE(sut->Get<IA>());
}
TEST(LazyContainer, ConstructWithNothing) {
  class Anothing : public IA {
   public:
    Anothing() { }
  };
  auto sut = Lazy::Make({ Lazy::MakeItem<IA, Anothing>(), });
  EXPECT_TRUE(sut->Get<IA>());
}

#if !defined(NDEBUG)
TEST(LazyContainer, DeathByFetchRecursive) {
  auto sut = Lazy::Make({ Lazy::MakeItem<IB, BRecursive>(), });
  ASSERT_DEATH_IF_SUPPORTED(sut->Get<IB>(), "");
}
#endif


TEST(FixedContainer, Fetch) {
  auto lazy = Lazy::Make({
    Lazy::MakeItem<IA, A>(),
    Lazy::MakeItem<IB, B>(),
  });
  auto sut = Fixed::Make(*lazy, {typeid(IB)});
  EXPECT_THROW(sut->Get<IA>(), nf7::Exception);
  EXPECT_TRUE(sut->Get<IB>());
}
TEST(FixedContainer, MakeAndFetch) {
  auto lazy = Lazy::Make({ Lazy::MakeItem<IA, A>(), });
  auto sut  = Fixed::Make(lazy, {typeid(IB)}, {Lazy::MakeItem<IB, B>()});
  EXPECT_THROW(sut->Get<IA>(), nf7::Exception);
  EXPECT_TRUE(sut->Get<IB>());
}
