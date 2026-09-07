#include "pack.h"

#include <algorithm>
#include <cctype>

namespace tabulous {
namespace {

std::string trim(const std::string &s) {
  size_t b = 0, e = s.size();
  // Also strips the \r that a CRLF file leaves on every line.
  while (b < e && (unsigned char)s[b] <= ' ') b++;
  while (e > b && (unsigned char)s[e - 1] <= ' ') e--;
  return s.substr(b, e - b);
}

std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

}  // namespace

Difficulty parseDifficulty(const std::string &text, bool *ok) {
  const std::string t = lower(trim(text));
  if (ok) *ok = true;
  if (t == "easy") return Difficulty::Easy;
  if (t == "hard") return Difficulty::Hard;
  if (t == "medium" || t.empty()) return Difficulty::Medium;
  if (ok) *ok = false;
  return Difficulty::Medium;
}

uint32_t parseHexColor(const std::string &text, bool *ok) {
  std::string t = trim(text);
  if (!t.empty() && t[0] == '#') t.erase(0, 1);
  if (t.size() != 6) {
    if (ok) *ok = false;
    return 0;
  }
  uint32_t value = 0;
  for (char c : t) {
    int digit;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    else {
      if (ok) *ok = false;
      return 0;
    }
    value = (value << 4) | (uint32_t)digit;
  }
  if (ok) *ok = true;
  return value;
}

void Pack::parse(const std::string &source, const std::string &fallback_name) {
  meta_ = PackMeta{};
  meta_.name = fallback_name;
  phrases_.clear();

  size_t pos = 0;
  while (pos <= source.size()) {
    size_t nl = source.find('\n', pos);
    if (nl == std::string::npos) nl = source.size();
    const std::string line = trim(source.substr(pos, nl - pos));
    pos = nl + 1;

    if (line.empty()) continue;

    if (line[0] == '#') {
      const size_t colon = line.find(':');
      if (colon == std::string::npos) continue;  // a plain comment
      const std::string key = lower(trim(line.substr(1, colon - 1)));
      const std::string value = trim(line.substr(colon + 1));
      if (key == "name") {
        meta_.name = value;
      } else if (key == "icon") {
        meta_.icon = value;
      } else if (key == "color") {
        bool ok = false;
        const uint32_t c = parseHexColor(value, &ok);
        if (ok) meta_.color = c;
      }
      continue;
    }

    Phrase phrase;
    const size_t bar = line.find('|');
    if (bar == std::string::npos) {
      phrase.text = line;
    } else {
      phrase.text = trim(line.substr(0, bar));
      phrase.difficulty = parseDifficulty(line.substr(bar + 1));
    }
    if (phrase.text.empty()) continue;
    phrases_.push_back(std::move(phrase));
  }

  if (meta_.name.empty()) meta_.name = fallback_name;
}

std::vector<size_t> Pack::indicesUpTo(Difficulty max) const {
  std::vector<size_t> out;
  out.reserve(phrases_.size());
  for (size_t i = 0; i < phrases_.size(); i++) {
    if ((uint8_t)phrases_[i].difficulty <= (uint8_t)max) out.push_back(i);
  }
  return out;
}

// ---------------------------------------------------------------- ShuffleBag

uint32_t ShuffleBag::rand32() {
  // xorshift32 — tiny, and deterministic for a given seed so tests can assert
  // exact sequences.
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

void ShuffleBag::shuffle() {
  for (size_t i = order_.size(); i > 1; i--) {
    const size_t j = rand32() % i;
    std::swap(order_[i - 1], order_[j]);
  }
  // Starting a new cycle with the item that just came up reads as a repeat
  // even though the bag is technically correct, so push it out of first place.
  if (has_last_ && order_.size() > 1 && order_.front() == last_drawn_) {
    std::swap(order_[0], order_[1 + (rand32() % (order_.size() - 1))]);
  }
  cursor_ = 0;
}

void ShuffleBag::reset(size_t count, uint32_t seed) {
  order_.resize(count);
  for (size_t i = 0; i < count; i++) order_[i] = i;
  rng_ = seed ? seed : 1;  // xorshift is stuck at zero
  has_last_ = false;
  last_drawn_ = 0;
  just_wrapped_ = false;
  shuffle();
}

size_t ShuffleBag::next() {
  if (order_.empty()) {
    just_wrapped_ = false;
    return 0;
  }
  just_wrapped_ = false;
  if (cursor_ >= order_.size()) {
    shuffle();
    just_wrapped_ = true;
  }
  const size_t value = order_[cursor_++];
  last_drawn_ = value;
  has_last_ = true;
  return value;
}

}  // namespace tabulous
