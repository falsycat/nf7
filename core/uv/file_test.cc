// No copyright
#include "core/uv/file.hh"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

#include "core/uv/context_test.hh"


class UV_File :
    public nf7::core::uv::test::ContextFixture,
    public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    ContextFixture::SetUp();

    const auto info = ::testing::UnitTest::GetInstance()->current_test_info();

    auto name =
        std::string {"unittest_"} + info->test_suite_name() + info->name();
    std::replace(name.begin(), name.end(), '/', '_');
    path_ = ::testing::TempDir() + "/" + name;
  }
  void TearDown() override {
    std::filesystem::remove(path_);
    ContextFixture::TearDown();
  }
  void PrepareFile(const char* text) {
    std::ofstream f {path_};
    f << text;
  }

 protected:
  bool isCompleteTest() const noexcept { return GetParam(); }
  const std::string& path() const noexcept { return path_; }

 private:
  std::string path_;
};

TEST_P(UV_File, Open) {
  auto sut = nf7::core::uv::File::Make(
      env(), path(),
      uvw::file_req::file_open_flags::RDWR
      | uvw::file_req::file_open_flags::CREAT);

  auto result = sut->Open();

  if (isCompleteTest()) {
    ctx_->Run();
    EXPECT_TRUE(result.done());
    EXPECT_TRUE(std::filesystem::exists(path()));
  } else {
    result.Then([](auto&) { FAIL(); });
  }
}

TEST_P(UV_File, FetchSize) {
  PrepareFile("helloworld");

  auto sut = nf7::core::uv::File::Make(
      env(), path(),
      uvw::file_req::file_open_flags::RDONLY);

  auto result = sut->FetchSize();

  if (isCompleteTest()) {
    ctx_->Run();
    EXPECT_TRUE(result.done());
    EXPECT_EQ(result.value(), 10);
  } else {
    result.Then([](auto&) { FAIL(); });
  }
}

TEST_P(UV_File, FetchSizeFail) {
  auto sut = nf7::core::uv::File::Make(
      env(), path(),
      uvw::file_req::file_open_flags::RDONLY);

  auto result = sut->FetchSize();
  if (isCompleteTest()) {
    ctx_->Run();
  }
  result.Then([](auto&) { FAIL(); });
}

TEST_P(UV_File, Truncate) {
  auto sut = nf7::core::uv::File::Make(
      env(), path(),
      uvw::file_req::file_open_flags::RDWR
      | uvw::file_req::file_open_flags::CREAT);

  auto result = sut->Truncate(256);
  if (isCompleteTest()) {
    ctx_->Run();
    EXPECT_TRUE(result.done());
    EXPECT_EQ(std::filesystem::file_size(path()), 256);
  } else {
    result.Then([](auto&) { FAIL(); });
  }
}

TEST_P(UV_File, TruncateFail) {
  auto sut = nf7::core::uv::File::Make(
      env(), path(),
      uvw::file_req::file_open_flags::RDONLY);

  auto result = sut->Truncate(256);
  if (isCompleteTest()) {
    ctx_->Run();
  }
  result.Then([](auto&) { FAIL(); });
}

TEST_P(UV_File, Read) {
  PrepareFile("helloworld");

  auto sut = nf7::core::uv::File::Make(
      env(), path(),
      uvw::file_req::file_open_flags::RDONLY);

  auto result = sut->Read(1, 3);

  if (isCompleteTest()) {
    ctx_->Run();
    ASSERT_TRUE(result.done());
    const auto [ptr, size] = result.value();
    const auto cptr = reinterpret_cast<const char*>(ptr.get());
    const std::string_view text {cptr, cptr+size};
    EXPECT_EQ(text, "ell");
  } else {
    result.Then([](auto&) { FAIL(); });
  }
}

TEST_P(UV_File, ReadFail) {
  PrepareFile("helloworld");

  auto sut = nf7::core::uv::File::Make(
      env(), path(),
      uvw::file_req::file_open_flags::WRONLY);

  auto result = sut->Read(1, 3);
  if (isCompleteTest()) {
    ctx_->Run();
  }
  result.Then([](auto&) { FAIL(); });
}

TEST_P(UV_File, Write) {
  PrepareFile("helloworld");

  auto sut = nf7::core::uv::File::Make(
      env(), path(),
      uvw::file_req::file_open_flags::WRONLY);

  auto result = sut->Write(5, reinterpret_cast<const uint8_t*>("universe"), 8);
  if (isCompleteTest()) {
    ctx_->Run();
    ASSERT_TRUE(result.done());
    EXPECT_EQ(result.value(), 8);

    std::ifstream fs {path()};
    const std::string text {
      std::istreambuf_iterator<char> {fs},
      std::istreambuf_iterator<char> {},
    };
    EXPECT_EQ(text, "hellouniverse");
  } else {
    result.Then([](auto&) { FAIL(); });
  }
}

TEST_P(UV_File, WriteFail) {
  PrepareFile("helloworld");

  auto sut = nf7::core::uv::File::Make(
      env(), path(),
      uvw::file_req::file_open_flags::RDONLY);

  auto result = sut->Write(5, reinterpret_cast<const uint8_t*>("universe"), 8);
  if (isCompleteTest()) {
    ctx_->Run();
  }
  result.Then([](auto&) { FAIL(); });
}

INSTANTIATE_TEST_SUITE_P(
    CompleteOrCancel, UV_File,
    testing::Values(true, false));
