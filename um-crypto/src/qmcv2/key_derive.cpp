#include "um-crypto/qmcv2/key_derive.h"
#include "../internal/str_helper.h"
#include "um-crypto/qmcv2.h"
#include "um-crypto/tc_tea.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <string>
#include <vector>

#include <boost/beast/core/detail/base64.hpp>

namespace base64 = boost::beast::detail::base64;

namespace umc::qmcv2 {
inline Vec<u8> EKeyDeriveBase::DeriveTEAKey(const Vec<u8> ekey) const {
  Vec<u8> tea_key(16);
  Vec<u8> simple_key(8);
  MakeSimpleKey(simple_key);

  for (int i = 0; i < 16; i += 2) {
    tea_key[i + 0] = simple_key[i / 2];
    tea_key[i + 1] = ekey[i / 2];
  }

  return tea_key;
}

bool EKeyDeriveBase::FromEKey(Vec<u8>& out, const Str ekey_b64) const {
  std::string ekey_str(ekey_b64);
  RemoveWhitespace(ekey_str);

  std::vector<uint8_t> ekey(base64::decoded_size(ekey_str.size()));
  auto result = base64::decode(ekey.data(), ekey_str.data(), ekey_str.size());
  ekey.resize(result.first);

  return FromEKey(out, ekey);
}

bool EKeyDeriveBase::FromEKey(Vec<u8>& out, const Vec<u8> ekey) const {
  const auto ekey_len = ekey.size();

  if (ekey_len < 8) {
    out.resize(0);
    return false;
  }

  auto tea_key = DeriveTEAKey(ekey);
  out.resize(ekey_len);
  memcpy(out.data(), ekey.data(), 8u);

  size_t tea_decrypted_len;
  if (!::umc::tc_tea::cbc_decrypt(&out[8], tea_decrypted_len, &ekey[8],
                                  ekey_len - 8, tea_key.data())) {
    out.resize(0);
    return false;
  };

  out.resize(8 + tea_decrypted_len);
  return true;
}

void SimpleEKeyDerive::MakeSimpleKey(Vec<u8>& out) const {
  double seed = static_cast<double>(this->seed);
  for (auto& byte : out) {
    byte = static_cast<uint8_t>(fabs(tan(seed)) * 100.0);
    seed += 0.1;
  }
}

}  // namespace umc::qmcv2
