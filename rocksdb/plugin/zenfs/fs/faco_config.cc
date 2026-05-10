// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#if !defined(ROCKSDB_LITE) && !defined(OS_WIN)

#include "faco_config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace ROCKSDB_NAMESPACE {

namespace {

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

size_t SkipSpace(const std::string& text, size_t pos) {
  while (pos < text.size() && IsSpace(text[pos])) {
    ++pos;
  }
  return pos;
}

std::string QuotedKey(const std::string& key) { return "\"" + key + "\""; }

size_t FindMatching(const std::string& text, size_t open_pos, char open_ch,
                    char close_ch) {
  bool in_string = false;
  bool escaped = false;
  int depth = 0;
  for (size_t i = open_pos; i < text.size(); ++i) {
    const char c = text[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\' && in_string) {
      escaped = true;
      continue;
    }
    if (c == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (c == open_ch) {
      ++depth;
    } else if (c == close_ch) {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::string::npos;
}

std::optional<std::string> FindSection(const std::string& text,
                                       const std::string& section) {
  const size_t key_pos = text.find(QuotedKey(section));
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  size_t colon = text.find(':', key_pos + section.size() + 2);
  if (colon == std::string::npos) {
    return std::nullopt;
  }
  size_t open = text.find('{', colon + 1);
  if (open == std::string::npos) {
    return std::nullopt;
  }
  const size_t close = FindMatching(text, open, '{', '}');
  if (close == std::string::npos || close <= open) {
    return std::nullopt;
  }
  return text.substr(open + 1, close - open - 1);
}

std::optional<std::string> FindRawValue(const std::string& section,
                                        const std::string& key) {
  const size_t key_pos = section.find(QuotedKey(key));
  if (key_pos == std::string::npos) {
    return std::nullopt;
  }
  size_t colon = section.find(':', key_pos + key.size() + 2);
  if (colon == std::string::npos) {
    return std::nullopt;
  }
  size_t begin = SkipSpace(section, colon + 1);
  if (begin >= section.size()) {
    return std::nullopt;
  }
  if (section[begin] == '[') {
    const size_t close = FindMatching(section, begin, '[', ']');
    if (close == std::string::npos) {
      return std::nullopt;
    }
    return section.substr(begin, close - begin + 1);
  }

  size_t end = begin;
  while (end < section.size() && section[end] != ',' &&
         section[end] != '\n' && section[end] != '\r') {
    ++end;
  }
  while (end > begin && IsSpace(section[end - 1])) {
    --end;
  }
  return section.substr(begin, end - begin);
}

std::optional<float> ReadFloat(const std::string& section,
                               const std::string& key) {
  const auto raw = FindRawValue(section, key);
  if (!raw) {
    return std::nullopt;
  }
  char* end = nullptr;
  const float parsed = std::strtof(raw->c_str(), &end);
  if (end == raw->c_str()) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<int> ReadInt(const std::string& section,
                           const std::string& key) {
  const auto raw = FindRawValue(section, key);
  if (!raw) {
    return std::nullopt;
  }
  char* end = nullptr;
  const long parsed = std::strtol(raw->c_str(), &end, 10);
  if (end == raw->c_str()) {
    return std::nullopt;
  }
  return static_cast<int>(parsed);
}

std::optional<uint64_t> ReadUint64(const std::string& section,
                                   const std::string& key) {
  const auto raw = FindRawValue(section, key);
  if (!raw) {
    return std::nullopt;
  }
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(raw->c_str(), &end, 10);
  if (end == raw->c_str()) {
    return std::nullopt;
  }
  return static_cast<uint64_t>(parsed);
}

std::optional<bool> ReadBool(const std::string& section,
                             const std::string& key) {
  const auto raw = FindRawValue(section, key);
  if (!raw) {
    return std::nullopt;
  }
  if (*raw == "true" || *raw == "1") {
    return true;
  }
  if (*raw == "false" || *raw == "0") {
    return false;
  }
  return std::nullopt;
}

std::vector<std::string> SplitArray(const std::string& raw) {
  std::vector<std::string> values;
  if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') {
    return values;
  }
  std::string token;
  for (size_t i = 1; i + 1 < raw.size(); ++i) {
    if (raw[i] == ',') {
      values.push_back(token);
      token.clear();
    } else if (!IsSpace(raw[i])) {
      token.push_back(raw[i]);
    }
  }
  if (!token.empty()) {
    values.push_back(token);
  }
  return values;
}

std::optional<std::array<int, 3>> ReadIntArray3(const std::string& section,
                                                const std::string& key) {
  const auto raw = FindRawValue(section, key);
  if (!raw) {
    return std::nullopt;
  }
  const auto tokens = SplitArray(*raw);
  if (tokens.size() != 3) {
    return std::nullopt;
  }
  std::array<int, 3> out{};
  for (size_t i = 0; i < tokens.size(); ++i) {
    char* end = nullptr;
    const long parsed = std::strtol(tokens[i].c_str(), &end, 10);
    if (end == tokens[i].c_str()) {
      return std::nullopt;
    }
    out[i] = static_cast<int>(parsed);
  }
  return out;
}

std::optional<std::array<uint64_t, 7>> ReadUint64Array7(
    const std::string& section, const std::string& key) {
  const auto raw = FindRawValue(section, key);
  if (!raw) {
    return std::nullopt;
  }
  const auto tokens = SplitArray(*raw);
  if (tokens.size() != 7) {
    return std::nullopt;
  }
  std::array<uint64_t, 7> out{};
  for (size_t i = 0; i < tokens.size(); ++i) {
    char* end = nullptr;
    const unsigned long long parsed =
        std::strtoull(tokens[i].c_str(), &end, 10);
    if (end == tokens[i].c_str()) {
      return std::nullopt;
    }
    out[i] = static_cast<uint64_t>(parsed);
  }
  return out;
}

std::optional<std::string> ReadWholeFile(const std::string& path) {
  std::ifstream in(path);
  if (!in.good()) {
    return std::nullopt;
  }
  std::ostringstream oss;
  oss << in.rdbuf();
  return oss.str();
}

}  // namespace

FacoConfig FacoConfig::LoadFromEnv() {
  const char* explicit_path = std::getenv("FACO_CONFIG_PATH");
  if (explicit_path != nullptr && *explicit_path != '\0') {
    return LoadFromFile(explicit_path);
  }
  return LoadFromFile("faco.json");
}

FacoConfig FacoConfig::LoadFromFile(const std::string& path) {
  auto text = ReadWholeFile(path);
  if (!text) {
    return FacoConfig();
  }
  return ParseForTest(*text, path);
}

FacoConfig FacoConfig::ParseForTest(const std::string& text,
                                    const std::string& source) {
  FacoConfig cfg;
  cfg.loaded_ = true;
  cfg.source_ = source;

  if (auto section = FindSection(text, "frag_state")) {
    cfg.frag_state.ema_alpha = ReadFloat(*section, "ema_alpha");
    cfg.frag_state.tick_interval_us =
        ReadUint64(*section, "tick_interval_us");
  }

  if (auto section = FindSection(text, "budget_ctrl")) {
    cfg.budget_ctrl.B_min = ReadInt(*section, "B_min");
    cfg.budget_ctrl.B_max = ReadInt(*section, "B_max");
    cfg.budget_ctrl.Kp = ReadFloat(*section, "Kp");
    cfg.budget_ctrl.Ki = ReadFloat(*section, "Ki");
    cfg.budget_ctrl.P_target = ReadFloat(*section, "P_target");
    cfg.budget_ctrl.theta_zvdr = ReadFloat(*section, "theta_zvdr");
    cfg.budget_ctrl.update_interval_us =
        ReadUint64(*section, "update_interval_us");
  }

  if (auto section = FindSection(text, "reorg_planner")) {
    cfg.reorg_planner.w1 = ReadFloat(*section, "w1");
    cfg.reorg_planner.w2 = ReadFloat(*section, "w2");
    cfg.reorg_planner.w3 = ReadFloat(*section, "w3");
    cfg.reorg_planner.w4 = ReadFloat(*section, "w4");
    cfg.reorg_planner.WA_factor = ReadFloat(*section, "WA_factor");
    cfg.reorg_planner.T_horizon_us =
        ReadUint64(*section, "T_horizon_us");
    cfg.reorg_planner.tau_trigger_init =
        ReadFloat(*section, "tau_trigger_init");
  }

  if (auto section = FindSection(text, "lacr")) {
    cfg.lacr.enable_l1 = ReadBool(*section, "enable_l1");
    cfg.lacr.enable_l2 = ReadBool(*section, "enable_l2");
    cfg.lacr.compaction_synergy_factor =
        ReadFloat(*section, "compaction_synergy_factor");
    cfg.lacr.lifetime_ema_alpha =
        ReadFloat(*section, "lifetime_ema_alpha");
    cfg.lacr.lifetime_short_threshold_us =
        ReadUint64(*section, "lifetime_short_threshold_us");
    cfg.lacr.lifetime_medium_threshold_us =
        ReadUint64(*section, "lifetime_medium_threshold_us");
    cfg.lacr.warmup_samples_per_level =
        ReadUint64(*section, "warmup_samples_per_level");
    cfg.lacr.pool_ratio_initial =
        ReadIntArray3(*section, "pool_ratio_initial");
    cfg.lacr.default_lifetime_us_per_level =
        ReadUint64Array7(*section, "default_lifetime_us_per_level");

    cfg.lacr.w_synergy = ReadFloat(*section, "w_synergy");
    cfg.lacr.w_waste = ReadFloat(*section, "w_waste");
    cfg.lacr.w_latency = ReadFloat(*section, "w_latency");
    cfg.lacr.active_compaction_penalty_bytes =
        ReadUint64(*section, "active_compaction_penalty_bytes");
    cfg.lacr.recent_invalidation_bonus_bytes =
        ReadUint64(*section, "recent_invalidation_bonus_bytes");
  }

  return cfg;
}

std::string FacoConfig::DebugString() const {
  std::ostringstream oss;
  oss << "FacoConfig{loaded=" << (loaded_ ? 1 : 0)
      << ", source=" << source_;
  if (frag_state.ema_alpha) {
    oss << ", frag_state.ema_alpha=" << *frag_state.ema_alpha;
  }
  if (budget_ctrl.B_min) {
    oss << ", budget_ctrl.B_min=" << *budget_ctrl.B_min;
  }
  if (budget_ctrl.B_max) {
    oss << ", budget_ctrl.B_max=" << *budget_ctrl.B_max;
  }
  if (reorg_planner.tau_trigger_init) {
    oss << ", reorg_planner.tau_trigger_init="
        << *reorg_planner.tau_trigger_init;
  }
  if (lacr.enable_l1) {
    oss << ", lacr.enable_l1=" << (*lacr.enable_l1 ? 1 : 0);
  }
  if (lacr.enable_l2) {
    oss << ", lacr.enable_l2=" << (*lacr.enable_l2 ? 1 : 0);
  }
  oss << "}";
  return oss.str();
}

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && !defined(OS_WIN)
