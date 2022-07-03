#pragma once

#include <cassert>

#include <miniaudio.h>
#include <yas/serialize.hpp>

#include "nf7.hh"


namespace yas::detail {

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    ma_device_type> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const ma_device_type& t) {
    switch (t) {
    case ma_device_type_playback:
      ar("playback");
      break;
    case ma_device_type_capture:
      ar("capture");
      break;
    default:
      assert(false);
    }
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, ma_device_type& t) {
    std::string v;
    ar(v);
    if (v == "playback") {
      t = ma_device_type_playback;
    } else if (v == "capture") {
      t = ma_device_type_capture;
    } else {
      throw nf7::DeserializeException("unknown device type");
    }
    return ar;
  }
};

template <size_t F>
struct serializer<
    type_prop::not_a_fundamental,
    ser_case::use_internal_serializer,
    F,
    ma_device_config> {
 public:
  template <typename Archive>
  static Archive& save(Archive& ar, const ma_device_config& v) {
    serialize(ar, v);
    return ar;
  }
  template <typename Archive>
  static Archive& load(Archive& ar, ma_device_config& v) {
    serialize(ar, v);
    if (v.sampleRate == 0) {
      throw nf7::DeserializeException("invalid sample rate");
    }
    return ar;
  }

 private:
  static void serialize(auto& ar, auto& v) {
    ar(v.deviceType);
    ar(v.sampleRate);
    if (v.deviceType == ma_device_type_playback) {
      ar(v.playback.format);
      ar(v.playback.channels);
    } else if (v.deviceType == ma_device_type_capture) {
      ar(v.capture.format);
      ar(v.capture.channels);
    } else {
      assert(false);
    }
  }
};

}  // namespace yas::detail
