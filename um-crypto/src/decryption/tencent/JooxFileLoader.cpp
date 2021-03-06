#include "um-crypto/decryption/tencent/JooxFileLoader.h"
#include "um-crypto/endian.h"

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/sha.h>

#include <cassert>

#include <iostream>
#include "um-crypto/utils/hex.h"

namespace umc::decryption::tencent {

// Private implementation

namespace detail_joox_v4 {

constexpr usize kMagicSize = 4;
constexpr usize kVer4HeaderSize = 12; /* 'E!04' + u64_be(file size) */

constexpr u32 kMagicJooxV4 = 0x45'21'30'34;  // 'E!04'

// Input block + padding 16 bytes (of 0x10)
constexpr usize kAESBlockSize = 0x10;
constexpr usize kEncryptionBlockSize = 0x100000;  // 1MiB
constexpr usize kDecryptionBlockSize = kEncryptionBlockSize + 0x10;
constexpr usize kBlockCountPerIteration = kEncryptionBlockSize / kAESBlockSize;

enum class State {
  kWaitForHeader = 0,
  kSeekToBody,
  kFastFirstPageDecryption,
  kDecryptPaddingBlock,
};

class JooxFileLoaderImpl : public JooxFileLoader {
 public:
  JooxFileLoaderImpl(const Str& install_uuid, const JooxSalt& salt)
      : uuid_(install_uuid), salt_(salt) {}

 private:
  CryptoPP::ECB_Mode<CryptoPP::AES>::Decryption aes_;

  Str uuid_;
  JooxSalt salt_;
  State state_ = State::kWaitForHeader;
  usize block_count_ = 0;

  inline void SetupKey() {
    u8 derived[CryptoPP::SHA1::DIGESTSIZE];
    CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA1> pbkdf;
    CryptoPP::byte unused = 0;
    pbkdf.DeriveKey(derived, sizeof(derived), 0 /* unused */,
                    reinterpret_cast<const u8*>(uuid_.c_str()), uuid_.size(),
                    salt_.data(), salt_.size(), 1000, 0);

    aes_.SetKey(derived, kAESBlockSize);
  }

  bool Write(const u8* in, usize len) override {
    buf_out_.reserve(buf_out_.size() + len);

    while (len) {
      switch (state_) {
        case State::kWaitForHeader:
          if (ReadUntilOffset(in, len, kMagicSize)) {
            if (ReadBigEndian<u32>(buf_in_.data()) != kMagicJooxV4) {
              error_ = "file header magic not found";
              return false;
            }
            state_ = State::kSeekToBody;
          }
          break;
        case State::kSeekToBody:
          if (ReadUntilOffset(in, len, kVer4HeaderSize)) {
            ConsumeInput(kVer4HeaderSize);
            SetupKey();

            state_ = State::kFastFirstPageDecryption;
          }
          break;
        case State::kFastFirstPageDecryption:
          // Always reserve last 16 bytes, as it could be the padding.
          while (ReadBlock(in, len, kAESBlockSize * 2)) {
            DecryptAesBlock();
            block_count_++;
            if (block_count_ == kBlockCountPerIteration) {
              state_ = State::kDecryptPaddingBlock;
              break;
            }
          }
          break;
        case State::kDecryptPaddingBlock:
          if (ReadBlock(in, len, kAESBlockSize)) {
            if (!DecryptPaddingBlock()) {
              return false;
            }
            state_ = State::kFastFirstPageDecryption;
            block_count_ = 0;
          }
          break;
      }
    }

    return true;
  }

  inline void DecryptAesBlock() {
    auto p_out = ExpandOutputBuffer(kAESBlockSize);

    aes_.ProcessData(p_out, buf_in_.data(), kAESBlockSize);

    ConsumeInput(kAESBlockSize);
  }

  inline bool DecryptPaddingBlock() {
    u8 block[kAESBlockSize];
    aes_.ProcessData(block, buf_in_.data(), kAESBlockSize);

    // Trim data. It should be 1 <= trim <= 16.
    u8 trim = block[kAESBlockSize - 1];
    if (trim == 0 || trim > 16) {
      error_ = "pkcs5 padding validation failed: out of range";
      return false;
    }

    usize len = kAESBlockSize - trim;

    u8 zero_sum = 0;
    for (usize i = len; i < kAESBlockSize; i++) {
      zero_sum |= block[i] ^ trim;
    }

    if (zero_sum != 0) {
      error_ = "pkcs5 padding validation failed: mismatch padding";
      return false;
    }

    buf_out_.insert(buf_out_.end(), block, block + len);

    ConsumeInput(kAESBlockSize);
    return true;
  }

  bool End() override {
    if (InErrorState()) return false;
    if (buf_in_.size() == 0) return true;

    if (buf_in_.size() != kAESBlockSize) {
      error_ = "unexpected file EOF";
      return false;
    }

    // Last block.
    return DecryptPaddingBlock();
  }
};

}  // namespace detail_joox_v4

// Public interface

std::unique_ptr<JooxFileLoader> JooxFileLoader::Create(const Str& install_uuid,
                                                       const JooxSalt& salt) {
  return std::make_unique<detail_joox_v4::JooxFileLoaderImpl>(install_uuid,
                                                              salt);
}

}  // namespace umc::decryption::tencent
