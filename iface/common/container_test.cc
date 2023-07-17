// No copyright
#include "iface/common/container.hh"

#include <gtest/gtest.h>

#include <string>

class Object {
 public:
  virtual ~Object() = default;
};
using SUT = nf7::Container<Object>;

class IA : public Object { };
class IB : public Object { };

class A : public IA {
 public:
  explicit A(SUT&) noexcept { }
};
class B : public IB {
 public:
  explicit B(SUT& sut) { sut.Get(a_); }
 private:
  std::shared_ptr<IA> a_;
};
class BRecursive : public IB {
 public:
  explicit BRecursive(SUT& sut) { sut.Get(b_); }
 private:
  std::shared_ptr<IB> b_;
};

TEST(Container, FetchIsolated) {
  SUT sut {{
    SUT::MakePair<IA, A>(),
  }};
  auto ptr = sut.Get<IA>();
  EXPECT_TRUE(std::dynamic_pointer_cast<A>(ptr));
}
TEST(Container, FetchDepending) {
  SUT sut {{
    SUT::MakePair<IA, A>(),
    SUT::MakePair<IB, B>(),
  }};
  auto ptr = sut.Get<IB>();
  EXPECT_TRUE(std::dynamic_pointer_cast<B>(ptr));
}
TEST(Container, FetchUnknown) {
  SUT sut {{}};
  EXPECT_THROW(sut.Get<IA>(), nf7::Exception);
}
TEST(Container, FetchUnknownDepending) {
  SUT sut {{
    SUT::MakePair<IB, B>(),
  }};
  EXPECT_THROW(sut.Get<IB>(), nf7::Exception);
}
TEST(Container, CheckInstalled) {
  SUT sut {{
    SUT::MakePair<IA, A>(),
  }};
  EXPECT_TRUE(sut.installed<IA>());
  EXPECT_FALSE(sut.installed<IB>());
}

#if !defined(NDEBUG)
TEST(Container, DeathByFetchRecursive) {
  SUT sut {{
    SUT::MakePair<IB, BRecursive>(),
  }};
  ASSERT_DEATH_IF_SUPPORTED(sut.Get<IB>(), "");
}
#endif
