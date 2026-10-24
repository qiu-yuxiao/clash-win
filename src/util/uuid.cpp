#include "util/uuid.h"

#include <mutex>
#include <random>

namespace clashwin::util {

namespace {

// ← help.rs:168-178：ALPHABET 62 字符 + nanoid!(11)
const char kAlphabet[] = "0123456789"
                         "abcdefghijklmnopqrstuvwxyz"
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

std::string random_uid() {
  static std::mutex mu;
  static std::mt19937_64 rng{std::random_device{}()};

  std::string id;
  id.reserve(11);
  std::lock_guard<std::mutex> lock(mu);
  std::uniform_int_distribution<int> dist(0, 61);
  for (int i = 0; i < 11; ++i) id += kAlphabet[dist(rng)];
  return id;
}

}  // namespace

std::string gen_uid(std::string_view prefix) {
  return std::string(prefix) + random_uid();
}

}  // namespace clashwin::util
