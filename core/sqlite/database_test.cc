// No copyright
#include "core/sqlite/database.hh"
#include "core/sqlite/database_test.hh"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "iface/subsys/database.hh"


using SQLiteDatabase = nf7::core::sqlite::test::DatabaseFixture;

TEST_F(SQLiteDatabase, CreateTable) {
  const auto db = env().Get<nf7::subsys::Database>();
  const auto fu = db->Exec("CREATE TABLE tbl (a, b, c);");
  ConsumeTasks();
  EXPECT_NO_THROW(fu.value());
}

TEST_F(SQLiteDatabase, SelectOneShot) {
  const auto db = env().Get<nf7::subsys::Database>();

  std::vector<std::string> rows;
  const auto fu = db->Exec(
      "CREATE TABLE tbl (idx, spell);"
      "INSERT INTO tbl VALUES (0, 'zero');"
      "INSERT INTO tbl VALUES (1, 'one');"
      "INSERT INTO tbl VALUES (2, 'two');"
      "SELECT * FROM tbl ORDER BY idx DESC;",
      [&](auto& x) {
        rows.push_back(std::get<std::string>(x.Fetch(1)));
        return true;
      });
  ConsumeTasks();

  EXPECT_NO_THROW(fu.value());
  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(rows[2], "zero");
  EXPECT_EQ(rows[1], "one");
  EXPECT_EQ(rows[0], "two");
}

TEST_F(SQLiteDatabase, InsertMultiShot) {
  const auto db = env().Get<nf7::subsys::Database>();

  db->Exec("CREATE TABLE tbl (idx, spell);");
  db->Compile("INSERT INTO tbl VALUES (?, ?);")
      .Then([](auto& x) {
        x->Run([](auto& x) {
          x.Reset();
          x.Bind(uint64_t {1}, int64_t {0});
          x.Bind(uint64_t {2}, std::string {"zero"});
          x.Exec();

          x.Reset();
          x.Bind(uint64_t {1}, int64_t {1});
          x.Bind(uint64_t {2}, std::string {"one"});
          x.Exec();

          x.Reset();
          x.Bind(uint64_t {1}, int64_t {2});
          x.Bind(uint64_t {2}, std::string {"two"});
          x.Exec();
        });
      });
  ConsumeTasks();

  std::vector<std::string> rows;
  const auto fu = db->Exec(
      "SELECT * FROM tbl ORDER BY idx ASC;",
      [&](auto& x) {
        rows.push_back(std::get<std::string>(x.Fetch(1)));
        return true;
      });
  ConsumeTasks();

  EXPECT_NO_THROW(fu.value());
  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(rows[0], "zero");
  EXPECT_EQ(rows[1], "one");
  EXPECT_EQ(rows[2], "two");
}
