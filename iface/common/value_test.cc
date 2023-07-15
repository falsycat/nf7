// No copyright
#include "iface/common/value.hh"

#include <gtest/gtest.h>


TEST(Value, NullAsNull) {
  const nf7::Value v = nf7::Value::Null {};
  EXPECT_TRUE(v.is<nf7::Value::Null>());
  EXPECT_FALSE(v.is<nf7::Value::Integer>());
  EXPECT_FALSE(v.is<nf7::Value::Real>());
  EXPECT_FALSE(v.is<nf7::Value::Buffer>());
  EXPECT_FALSE(v.is<nf7::Value::Object>());
}
TEST(Value, NullAsInvalid) {
  const nf7::Value v = nf7::Value::Null {};
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
}

TEST(Value, IntegerAsInteger) {
  const nf7::Value v = nf7::Value::Integer {777};

  EXPECT_FALSE(v.is<nf7::Value::Null>());
  EXPECT_TRUE(v.is<nf7::Value::Integer>());
  EXPECT_FALSE(v.is<nf7::Value::Real>());
  EXPECT_FALSE(v.is<nf7::Value::Buffer>());
  EXPECT_FALSE(v.is<nf7::Value::Object>());

  EXPECT_EQ(v.as<nf7::Value::Integer>(), nf7::Value::Integer {777});
}
TEST(Value, IntegerAsInvalid) {
  const nf7::Value v = nf7::Value::Integer {777};
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
}
TEST(Value, IntegerAsValidNum) {
  const nf7::Value v = nf7::Value::Integer {777};
  EXPECT_EQ(v.num<int32_t>(), int32_t {777});
  EXPECT_EQ(v.num<double>(),  double {777});
}
TEST(Value, IntegerAsInvalidNum) {
  const nf7::Value v = nf7::Value::Integer {777};
  EXPECT_THROW(v.num<int8_t>(), nf7::Exception);
  EXPECT_THROW(v.num<int8_t>(int8_t {77}), nf7::Exception);
}

TEST(Value, RealAsReal) {
  const nf7::Value v = nf7::Value::Real {777};

  EXPECT_FALSE(v.is<nf7::Value::Null>());
  EXPECT_FALSE(v.is<nf7::Value::Integer>());
  EXPECT_TRUE(v.is<nf7::Value::Real>());
  EXPECT_FALSE(v.is<nf7::Value::Buffer>());
  EXPECT_FALSE(v.is<nf7::Value::Object>());

  EXPECT_EQ(v.as<nf7::Value::Real>(), nf7::Value::Real {777});
}
TEST(Value, RealAsInvalid) {
  const nf7::Value v = nf7::Value::Real {777};
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
}
TEST(Value, RealAsValidNum) {
  const nf7::Value v = nf7::Value::Real {777};
  EXPECT_EQ(v.num<int32_t>(), int32_t {777});
  EXPECT_EQ(v.num<double>(),  double {777});
}
TEST(Value, RealAsInvalidNum) {
  const nf7::Value v = nf7::Value::Real {777};
  EXPECT_THROW(v.num<int8_t>(), nf7::Exception);
  EXPECT_THROW(v.num<int8_t>(int8_t {77}), nf7::Exception);
}

TEST(Value, BufferAsBuffer) {
  const nf7::Value v = nf7::Value::Buffer {};
  EXPECT_FALSE(v.is<nf7::Value::Null>());
  EXPECT_FALSE(v.is<nf7::Value::Integer>());
  EXPECT_FALSE(v.is<nf7::Value::Real>());
  EXPECT_TRUE(v.is<nf7::Value::Buffer>());
  EXPECT_FALSE(v.is<nf7::Value::Object>());
}
TEST(Value, BufferAsInvalid) {
  const nf7::Value v = nf7::Value::Buffer {};
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
}

TEST(Value, ObjectAsObject) {
  const nf7::Value v = nf7::Value::Object {};
  EXPECT_FALSE(v.is<nf7::Value::Null>());
  EXPECT_FALSE(v.is<nf7::Value::Integer>());
  EXPECT_FALSE(v.is<nf7::Value::Real>());
  EXPECT_FALSE(v.is<nf7::Value::Buffer>());
  EXPECT_TRUE(v.is<nf7::Value::Object>());
}
TEST(Value, ObjectAsInvalid) {
  const nf7::Value v = nf7::Value::Object {};
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
}

TEST(Value_Buffer, Make) {
  const nf7::Value value = nf7::MakeValue<uint8_t>({1, 2, 3, 4});
  const auto&      sut   = value.as<nf7::Value::Buffer>();
  EXPECT_EQ(sut.size(), 4);
  EXPECT_EQ((std::vector<uint8_t> {sut.begin<uint8_t>(), sut.end<uint8_t>()}),
            (std::vector<uint8_t> {1, 2, 3, 4}));
}
TEST(Value_Buffer, AsStr) {
  const nf7::Value value = nf7::MakeValue<char>({'h', 'e', 'l', 'l'});
  const auto&      sut   = value.as<nf7::Value::Buffer>();
  EXPECT_EQ(sut.size(), 4);
  EXPECT_EQ(sut.str(), "hell");
}
TEST(Value_Buffer, AsU64) {
  const nf7::Value value = nf7::MakeValue<uint64_t>({7777, 8888, 9999});
  const auto&      sut   = value.as<nf7::Value::Buffer>();
  EXPECT_EQ(sut.size(), 24);
  EXPECT_EQ(sut.size<uint64_t>(), 3);
  EXPECT_EQ((std::vector<uint64_t> {
               sut.begin<uint64_t>(), sut.end<uint64_t>()}),
            (std::vector<uint64_t> {7777, 8888, 9999}));
}

TEST(Value_Object, MakeArray) {
  const nf7::Value value = nf7::MakeValue<nf7::Value>({
    nf7::Value::Integer {1}, nf7::Value::Real {2.0}, nf7::Value::Integer {3},
  });
  const auto& sut = value.as<nf7::Value::Object>();
  EXPECT_EQ(sut.size(), 3);
  EXPECT_EQ(sut[0].as<nf7::Value::Integer>(), 1);
  EXPECT_EQ(sut[1].as<nf7::Value::Real>(), 2.0);
  EXPECT_EQ(sut[2].as<nf7::Value::Integer>(), 3);
  EXPECT_EQ(sut.at(0).as<nf7::Value::Integer>(), 1);
  EXPECT_EQ(sut.at(1).as<nf7::Value::Real>(), 2.0);
  EXPECT_EQ(sut.at(2).as<nf7::Value::Integer>(), 3);
}
TEST(Value_Object, ArrayOutOfBounds) {
  const nf7::Value value = nf7::MakeValue<nf7::Value>({
    nf7::Value::Integer {1}, nf7::Value::Real {2.0}, nf7::Value::Integer {3},
  });
  const auto& sut = value.as<nf7::Value::Object>();
  EXPECT_THROW(sut[4], nf7::Exception);
  EXPECT_TRUE(sut.at(4).is<nf7::Value::Null>());
}

TEST(Value_Object, MakeObject) {
  const nf7::Value value = nf7::MakeValue<nf7::Value::Object::Pair>({
    {"one",   nf7::Value::Integer {1}},
    {"two",   nf7::Value::Real {2.0}},
    {"three", nf7::Value::Integer {3}},
  });
  const auto& sut = value.as<nf7::Value::Object>();

  EXPECT_EQ(sut.size(), 3);
  EXPECT_EQ(sut[0].as<nf7::Value::Integer>(), 1);
  EXPECT_EQ(sut[1].as<nf7::Value::Real>(), 2.0);
  EXPECT_EQ(sut[2].as<nf7::Value::Integer>(), 3);
  EXPECT_EQ(sut.at(0).as<nf7::Value::Integer>(), 1);
  EXPECT_EQ(sut.at(1).as<nf7::Value::Real>(), 2.0);
  EXPECT_EQ(sut.at(2).as<nf7::Value::Integer>(), 3);

  EXPECT_EQ(sut["one"  ].as<nf7::Value::Integer>(), 1);
  EXPECT_EQ(sut["two"  ].as<nf7::Value::Real>(), 2.0);
  EXPECT_EQ(sut["three"].as<nf7::Value::Integer>(), 3);
  EXPECT_EQ(sut.at("one"  ).as<nf7::Value::Integer>(), 1);
  EXPECT_EQ(sut.at("two"  ).as<nf7::Value::Real>(), 2.0);
  EXPECT_EQ(sut.at("three").as<nf7::Value::Integer>(), 3);

  const auto begin = sut.begin();
  EXPECT_EQ(begin[0].first, "one");
  EXPECT_EQ(begin[1].first, "two");
  EXPECT_EQ(begin[2].first, "three");
  EXPECT_EQ(begin[0].second.as<nf7::Value::Integer>(), 1);
  EXPECT_EQ(begin[1].second.as<nf7::Value::Real>(), 2.0);
  EXPECT_EQ(begin[2].second.as<nf7::Value::Integer>(), 3);
}

TEST(Value_Object, UnknownKey) {
  const nf7::Value value = nf7::MakeValue<nf7::Value::Object::Pair>({
    {"one",   nf7::Value::Integer {1}},
    {"two",   nf7::Value::Real {2.0}},
    {"three", nf7::Value::Integer {3}},
  });
  const auto& sut = value.as<nf7::Value::Object>();

  EXPECT_THROW(sut["four"], nf7::Exception);
  EXPECT_TRUE(sut.at("four").is<nf7::Value::Null>());
}
