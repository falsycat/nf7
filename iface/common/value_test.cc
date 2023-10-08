// No copyright
#include "iface/common/value.hh"

#include <gtest/gtest.h>


namespace {

class CustomData1 : public nf7::Value::Data { };
class CustomData2 : public nf7::Value::Data { };

}  // namespace


TEST(Value, NullAsNull) {
  const auto v = nf7::Value::MakeNull();
  EXPECT_TRUE(v.is<nf7::Value::Null>());
  EXPECT_FALSE(v.is<nf7::Value::Integer>());
  EXPECT_FALSE(v.is<nf7::Value::Real>());
  EXPECT_FALSE(v.is<nf7::Value::Buffer>());
  EXPECT_FALSE(v.is<nf7::Value::Object>());
  EXPECT_FALSE(v.is<nf7::Value::SharedData>());
}
TEST(Value, NullAsInvalid) {
  const auto v = nf7::Value::MakeNull();
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::SharedData>(), nf7::Exception);
}
TEST(Value, NullEqual) {
  EXPECT_EQ(nf7::Value::MakeNull(), nf7::Value::MakeNull());
}
TEST(Value, NullNotEqual) {
  EXPECT_NE(nf7::Value::MakeNull(), nf7::Value::MakeInteger(0));
  EXPECT_NE(nf7::Value::MakeNull(), nf7::Value::MakeReal(0));
  EXPECT_NE(nf7::Value::MakeNull(), nf7::Value::MakeBuffer<uint8_t>({}));
  EXPECT_NE(nf7::Value::MakeNull(), nf7::Value::MakeObject({}));
  EXPECT_NE(nf7::Value::MakeNull(), nf7::Value::MakeSharedData<CustomData1>());
}

TEST(Value, IntegerAsInteger) {
  const auto v = nf7::Value::MakeInteger(777);

  EXPECT_FALSE(v.is<nf7::Value::Null>());
  EXPECT_TRUE(v.is<nf7::Value::Integer>());
  EXPECT_FALSE(v.is<nf7::Value::Real>());
  EXPECT_FALSE(v.is<nf7::Value::Buffer>());
  EXPECT_FALSE(v.is<nf7::Value::Object>());
  EXPECT_FALSE(v.is<nf7::Value::SharedData>());

  EXPECT_EQ(v.as<nf7::Value::Integer>(), nf7::Value::Integer {777});
}
TEST(Value, IntegerAsInvalid) {
  const nf7::Value v = nf7::Value::MakeInteger(777);
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::SharedData>(), nf7::Exception);
}
TEST(Value, IntegerAsValidNum) {
  const nf7::Value v = nf7::Value::MakeInteger(777);
  EXPECT_EQ(v.num<int32_t>(), int32_t {777});
  EXPECT_EQ(v.num<double>(),  double {777});
}
TEST(Value, IntegerAsInvalidNum) {
  const nf7::Value v = nf7::Value::MakeInteger(777);
  EXPECT_THROW(v.num<int8_t>(), nf7::Exception);
  EXPECT_THROW(v.num<int8_t>(int8_t {77}), nf7::Exception);
}
TEST(Value, IntegerEqual) {
  EXPECT_EQ(nf7::Value::MakeInteger(666), nf7::Value::MakeInteger(666));
}
TEST(Value, IntegerNotEqual) {
  EXPECT_NE(nf7::Value::MakeInteger(666), nf7::Value::MakeInteger(777));
  EXPECT_NE(nf7::Value::MakeInteger(666), nf7::Value::MakeNull());
  EXPECT_NE(nf7::Value::MakeInteger(666), nf7::Value::MakeReal(0));
  EXPECT_NE(nf7::Value::MakeInteger(666), nf7::Value::MakeBuffer<uint8_t>({}));
  EXPECT_NE(nf7::Value::MakeInteger(666), nf7::Value::MakeObject({}));
  EXPECT_NE(nf7::Value::MakeInteger(666), nf7::Value::MakeSharedData<CustomData1>());
}

TEST(Value, RealAsReal) {
  const auto v = nf7::Value::MakeReal(777);

  EXPECT_FALSE(v.is<nf7::Value::Null>());
  EXPECT_FALSE(v.is<nf7::Value::Integer>());
  EXPECT_TRUE(v.is<nf7::Value::Real>());
  EXPECT_FALSE(v.is<nf7::Value::Buffer>());
  EXPECT_FALSE(v.is<nf7::Value::Object>());
  EXPECT_FALSE(v.is<nf7::Value::SharedData>());

  EXPECT_EQ(v.as<nf7::Value::Real>(), nf7::Value::Real {777});
}
TEST(Value, RealAsInvalid) {
  const auto v = nf7::Value::MakeReal(777);
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::SharedData>(), nf7::Exception);
}
TEST(Value, RealAsValidNum) {
  const auto v = nf7::Value::MakeReal(777);
  EXPECT_EQ(v.num<int32_t>(), int32_t {777});
  EXPECT_EQ(v.num<double>(),  double {777});
}
TEST(Value, RealAsInvalidNum) {
  const auto v = nf7::Value::MakeReal(777);
  EXPECT_THROW(v.num<int8_t>(), nf7::Exception);
  EXPECT_THROW(v.num<int8_t>(int8_t {77}), nf7::Exception);
}
TEST(Value, RealEqual) {
  EXPECT_EQ(nf7::Value::MakeReal(0.5), nf7::Value::MakeReal(0.5));
}
TEST(Value, RealNotEqual) {
  EXPECT_NE(nf7::Value::MakeReal(1), nf7::Value::MakeReal(0.5));
  EXPECT_NE(nf7::Value::MakeReal(1), nf7::Value::MakeNull());
  EXPECT_NE(nf7::Value::MakeReal(1), nf7::Value::MakeInteger(1));
  EXPECT_NE(nf7::Value::MakeReal(1), nf7::Value::MakeBuffer<uint8_t>({}));
  EXPECT_NE(nf7::Value::MakeReal(1), nf7::Value::MakeObject({}));
  EXPECT_NE(nf7::Value::MakeReal(1), nf7::Value::MakeSharedData<CustomData1>());
}

TEST(Value, BufferAsBuffer) {
  const auto v = nf7::Value::MakeBuffer<uint8_t>({});
  EXPECT_FALSE(v.is<nf7::Value::Null>());
  EXPECT_FALSE(v.is<nf7::Value::Integer>());
  EXPECT_FALSE(v.is<nf7::Value::Real>());
  EXPECT_TRUE(v.is<nf7::Value::Buffer>());
  EXPECT_FALSE(v.is<nf7::Value::Object>());
  EXPECT_FALSE(v.is<nf7::Value::SharedData>());
}
TEST(Value, BufferAsInvalid) {
  const auto v = nf7::Value::MakeBuffer<uint8_t>({});
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::SharedData>(), nf7::Exception);
}
TEST(Value, BufferEqual) {
  const auto v = nf7::Value::MakeBuffer<uint8_t>({});
  EXPECT_EQ(v, v);
}
TEST(Value, BufferNotEqual) {
  EXPECT_NE(nf7::Value::MakeBuffer<uint8_t>({}), nf7::Value::MakeNull());
  EXPECT_NE(nf7::Value::MakeBuffer<uint8_t>({}), nf7::Value::MakeInteger(0));
  EXPECT_NE(nf7::Value::MakeBuffer<uint8_t>({}), nf7::Value::MakeReal(0));
  EXPECT_NE(nf7::Value::MakeBuffer<uint8_t>({}),
            nf7::Value::MakeBuffer<uint8_t>({}));
  EXPECT_NE(nf7::Value::MakeBuffer<uint8_t>({}), nf7::Value::MakeObject({}));
  EXPECT_NE(nf7::Value::MakeBuffer<uint8_t>({}), nf7::Value::MakeSharedData<CustomData1>());
}

TEST(Value, ObjectAsObject) {
  const auto v = nf7::Value::MakeObject({});
  EXPECT_FALSE(v.is<nf7::Value::Null>());
  EXPECT_FALSE(v.is<nf7::Value::Integer>());
  EXPECT_FALSE(v.is<nf7::Value::Real>());
  EXPECT_FALSE(v.is<nf7::Value::Buffer>());
  EXPECT_TRUE(v.is<nf7::Value::Object>());
  EXPECT_FALSE(v.is<nf7::Value::SharedData>());
}
TEST(Value, ObjectAsInvalid) {
  const auto v = nf7::Value::MakeObject({});
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::SharedData>(), nf7::Exception);
}
TEST(Value, ObjectEqual) {
  const auto v = nf7::Value::MakeObject({});
  EXPECT_EQ(v, v);
}
TEST(Value, ObjectNotEqual) {
  EXPECT_NE(nf7::Value::MakeObject({}), nf7::Value::MakeNull());
  EXPECT_NE(nf7::Value::MakeObject({}), nf7::Value::MakeInteger(0));
  EXPECT_NE(nf7::Value::MakeObject({}), nf7::Value::MakeReal(0));
  EXPECT_NE(nf7::Value::MakeObject({}), nf7::Value::MakeBuffer<uint8_t>({}));
  EXPECT_NE(nf7::Value::MakeObject({}), nf7::Value::MakeObject({}));
  EXPECT_NE(nf7::Value::MakeObject({}), nf7::Value::MakeSharedData<CustomData1>());
}

TEST(Value, DataAsCompatibleData) {
  const auto v = nf7::Value::MakeSharedData<CustomData1>();
  EXPECT_FALSE(v.is<nf7::Value::Null>());
  EXPECT_FALSE(v.is<nf7::Value::Integer>());
  EXPECT_FALSE(v.is<nf7::Value::Real>());
  EXPECT_FALSE(v.is<nf7::Value::Buffer>());
  EXPECT_FALSE(v.is<nf7::Value::Object>());
  EXPECT_TRUE(v.is<nf7::Value::SharedData>());

  EXPECT_NE(v.data<CustomData1>(), nullptr);
}
TEST(Value, DataAsIncompatibleData) {
  const auto v = nf7::Value::MakeSharedData<CustomData1>();
  EXPECT_THROW(v.data<CustomData2>(), nf7::Exception);
}
TEST(Value, DataAsInvalid) {
  const auto v = nf7::Value::MakeSharedData<CustomData1>();
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
}
TEST(Value, DataAsIncompatibleType) {
  const auto v = nf7::Value::MakeSharedData<CustomData1>();
  EXPECT_THROW(v.as<nf7::Value::Null>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Integer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Real>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Buffer>(), nf7::Exception);
  EXPECT_THROW(v.as<nf7::Value::Object>(), nf7::Exception);
}
TEST(Value, DataEqual) {
  const auto v = nf7::Value::MakeSharedData<CustomData1>();
  EXPECT_EQ(v, v);
}
TEST(Value, DataNotEqual) {
  const auto v = nf7::Value::MakeSharedData<CustomData1>();
  EXPECT_NE(v, nf7::Value::MakeNull());
  EXPECT_NE(v, nf7::Value::MakeInteger(0));
  EXPECT_NE(v, nf7::Value::MakeReal(0));
  EXPECT_NE(v, nf7::Value::MakeBuffer<uint8_t>({}));
  EXPECT_NE(v, nf7::Value::MakeObject({}));
  EXPECT_NE(v, nf7::Value::MakeSharedData<CustomData1>());
  EXPECT_NE(v, nf7::Value::MakeSharedData<CustomData2>());
}

TEST(ValueBuffer, Make) {
  const auto  value = nf7::Value::MakeBuffer<uint8_t>({1, 2, 3, 4});
  const auto& sut   = value.as<nf7::Value::Buffer>();
  EXPECT_EQ(sut.size(), 4);
  EXPECT_EQ((std::vector<uint8_t> {sut.begin<uint8_t>(), sut.end<uint8_t>()}),
            (std::vector<uint8_t> {1, 2, 3, 4}));
}
TEST(ValueBuffer, AsStr) {
  const auto  value = nf7::Value::MakeBuffer<char>({'h', 'e', 'l', 'l'});
  const auto& sut   = value.as<nf7::Value::Buffer>();
  EXPECT_EQ(sut.size(), 4);
  EXPECT_EQ(sut.str(), "hell");
}
TEST(ValueBuffer, AsU64) {
  const auto  value = nf7::Value::MakeBuffer<uint64_t>({7777, 8888, 9999});
  const auto& sut   = value.as<nf7::Value::Buffer>();
  EXPECT_EQ(sut.size(), 24);
  EXPECT_EQ(sut.size<uint64_t>(), 3);
  EXPECT_EQ((std::vector<uint64_t> {
               sut.begin<uint64_t>(), sut.end<uint64_t>()}),
            (std::vector<uint64_t> {7777, 8888, 9999}));
}

TEST(ValueObject, MakeArray) {
  const auto value = nf7::Value::MakeArray({
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
TEST(ValueObject, ArrayOutOfBounds) {
  const auto value = nf7::Value::MakeArray({
    nf7::Value::Integer {1}, nf7::Value::Real {2.0}, nf7::Value::Integer {3},
  });
  const auto& sut = value.as<nf7::Value::Object>();
  EXPECT_THROW(sut[4], nf7::Exception);
  EXPECT_TRUE(sut.at(4).is<nf7::Value::Null>());
}
TEST(ValueObject, MakeObject) {
  const auto value = nf7::Value::MakeObject({
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
TEST(ValueObject, UnknownKey) {
  const auto value = nf7::Value::MakeObject({
    {"one",   nf7::Value::Integer {1}},
    {"two",   nf7::Value::Real {2.0}},
    {"three", nf7::Value::Integer {3}},
  });
  const auto& sut = value.as<nf7::Value::Object>();

  EXPECT_THROW(sut["four"], nf7::Exception);
  EXPECT_TRUE(sut.at("four").is<nf7::Value::Null>());
}
