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
  SUT sut {{
    SUT::MakePair<IA, A>(),
  }};
  auto ptr = sut.Get<IA>();
  EXPECT_TRUE(std::dynamic_pointer_cast<A>(ptr));
}
TEST(SimpleContainer, FetchDepending) {
  SUT sut {{
    SUT::MakePair<IA, A>(),
    SUT::MakePair<IB, B>(),
  }};
  auto ptr = sut.Get<IB>();
  EXPECT_TRUE(std::dynamic_pointer_cast<B>(ptr));
}
TEST(SimpleContainer, FetchUnknown) {
  SUT sut {{}};
  EXPECT_THROW(sut.Get<IA>(), nf7::Exception);
}
TEST(SimpleContainer, FetchUnknownDepending) {
  SUT sut {{
    SUT::MakePair<IB, B>(),
  }};
  EXPECT_THROW(sut.Get<IB>(), nf7::Exception);
}
TEST(SimpleContainer, CheckInstalled) {
  SUT sut {{
    SUT::MakePair<IA, A>(),
  }};
  EXPECT_TRUE(sut.installed<IA>());
  EXPECT_FALSE(sut.installed<IB>());
}

TEST(SimpleContainer, FetchWithFallback) {
  SUT fb {{
    SUT::MakePair<IA, A>(),
  }};
  SUT sut {{}, fb};
  auto ptr = sut.Get<IA>();
  EXPECT_TRUE(std::dynamic_pointer_cast<A>(ptr));
}
TEST(SimpleContainer, FetchUnknownWithFallback) {
  SUT fb {{}};
  SUT sut {{}, fb};
  EXPECT_THROW(sut.Get<IA>(), nf7::Exception);
}
TEST(SimpleContainer, CheckInstalledWithFallback) {
  SUT fb {{
    SUT::MakePair<IA, A>(),
  }};
  SUT sut {{}, fb};
  EXPECT_TRUE(sut.installed<IA>());
  EXPECT_FALSE(sut.installed<IB>());
}

#if !defined(NDEBUG)
TEST(SimpleContainer, DeathByFetchRecursive) {
  SUT sut {{
    SUT::MakePair<IB, BRecursive>(),
  }};
  ASSERT_DEATH_IF_SUPPORTED(sut.Get<IB>(), "");
}
#endif
