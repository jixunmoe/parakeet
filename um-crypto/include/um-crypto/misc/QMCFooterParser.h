#pragma once

#include "QMCKeyDeriver.h"
#include "um-crypto/types.h"

#include <memory>

namespace umc::misc::tencent {

struct QMCFooterParseResult {
  Str ekey_b64;
  Vec<u8> key;
  usize eof_bytes_ignore;
};

class QMCFooterParser {
 public:
  /**
   * @brief Parse a given block of footer data.
   *
   * @param p_in Data pointer
   * @param len  Size of the data
   * @return std::unique_ptr<QMCFooterParseResult>
   * @return nullptr - Could not parse / not enough data
   */
  virtual std::unique_ptr<QMCFooterParseResult> Parse(const u8* p_in,
                                                      usize len) const = 0;

  static std::unique_ptr<QMCFooterParser> Create(
      std::shared_ptr<QMCKeyDeriver> key_deriver);
};

}  // namespace umc::misc::tencent
