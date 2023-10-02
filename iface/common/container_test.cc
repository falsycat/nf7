// No copyright
#include "iface/common/container.hh"

#include <gtest/gtest.h>

#include <string>

namespace {
class Object {
 public:
  virtual ~Object() = default;
};
using SUT = nf7::SimpleContainer<Object>;

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

TEST(SimpleContainer, FetchIsolated) {
  auto sut = SUT::Make({ SUT::MakeItem<IA, A>(), });
  auto ptr = sut->Get<IA>();
  EXPECT_TRUE(std::dynamic_pointer_cast<A>(ptr));
}
TEST(SimpleContainer, FetchDepending) {
  auto sut = SUT::Make({
    SUT::MakeItem<IA, A>(),
    SUT::MakeItem<IB, B>(),
  });
  auto ptr = sut->Get<IB>();
  EXPECT_TRUE(std::dynamic_pointer_cast<B>(ptr));
}
TEST(SimpleContainer, FetchUnknown) {
  auto sut = SUT::Make();
  EXPECT_THROW(sut->Get<IA>(), nf7::Exception);
}
TEST(SimpleContainer, FetchUnknownDepending) {
  auto sut = SUT::Make({ SUT::MakeItem<IB, B>(), });
  EXPECT_THROW(sut->Get<IB>(), nf7::Exception);
}

TEST(SimpleContainer, FetchWithFallback) {
  auto fb  = SUT::Make({ SUT::MakeItem<IA, A>(), });
  auto sut = SUT::Make({}, fb);
  auto ptr = sut->Get<IA>();
  EXPECT_TRUE(std::dynamic_pointer_cast<A>(ptr));
}
TEST(SimpleContainer, FetchUnknownWithFallback) {
  auto fb  = SUT::Make();
  auto sut = SUT::Make({}, fb);
  EXPECT_THROW(sut->Get<IA>(), nf7::Exception);
}

TEST(SimpleContainer, ConstructWithSharedInstance) {
  class Ashared : public IA {
   public:
    Ashared(const std::shared_ptr<nf7::Container<Object>>&) { }
  };
  auto sut = SUT::Make({ SUT::MakeItem<IA, Ashared>(), });
  EXPECT_TRUE(sut->Get<IA>());
}
TEST(SimpleContainer, ConstructWithNothing) {
  class Anothing : public IA {
   public:
    Anothing() { }
  };
  auto sut = SUT::Make({ SUT::MakeItem<IA, Anothing>(), });
  EXPECT_TRUE(sut->Get<IA>());
}

#if !defined(NDEBUG)
TEST(SimpleContainer, DeathByFetchRecursive) {
  auto sut = SUT::Make({ SUT::MakeItem<IB, BRecursive>(), });
  ASSERT_DEATH_IF_SUPPORTED(sut->Get<IB>(), "");
}
#endif
