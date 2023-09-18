// No copyright
#pragma once

#include "core/sqlite/database.hh"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

#include "core/env_test.hh"


namespace nf7::core::sqlite::test {

class DatabaseFixture : public nf7::core::test::EnvFixtureWithTasking {
 public:
  DatabaseFixture()
      : nf7::core::test::EnvFixtureWithTasking({
              {typeid(nf7::subsys::Database), [](auto& env) {
                return std::make_shared<Database>(env, ":memory:");
              }},
            }) {
  }
};

}  // namespace nf7::core::sqlite::test
