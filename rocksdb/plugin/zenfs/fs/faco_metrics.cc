// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#if !defined(ROCKSDB_LITE) && !defined(OS_WIN)

#include "faco_metrics.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <map>
#include <sstream>

#include "frag_state_table.h"

namespace ROCKSDB_NAMESPACE {

namespace {

const char* KindName(FacoMetricKind kind) {
  switch (kind) {
    case FacoMetricKind::kGauge:
      return "gauge";
    case FacoMetricKind::kCounter:
      return "counter";
    case FacoMetricKind::kHistogram:
      return "histogram";
  }
  return "gauge";
}

std::vector<std::string> Split(const std::string& text, char sep) {
  std::vector<std::string> values;
  std::string token;
  for (char c : text) {
    if (c == sep) {
      values.push_back(token);
      token.clear();
    } else {
      token.push_back(c);
    }
  }
  values.push_back(token);
  return values;
}

double ToDouble(const std::string& value, double default_value = 0.0) {
  char* end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  if (end == value.c_str()) {
    return default_value;
  }
  return parsed;
}

double Percentile(std::vector<double> values, double p) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  if (values.size() == 1) {
    return values[0];
  }
  const double pos = p * static_cast<double>(values.size() - 1);
  const size_t lo = static_cast<size_t>(pos);
  const size_t hi = std::min(values.size() - 1, lo + 1);
  const double frac = pos - static_cast<double>(lo);
  return values[lo] * (1.0 - frac) + values[hi] * frac;
}

std::map<std::string, std::string> ParseKeyValueLines(
    const std::string& text) {
  std::map<std::string, std::string> values;
  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) {
    const size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    values[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return values;
}

std::map<std::string, std::string> ParseBraceDebug(const std::string& debug) {
  std::map<std::string, std::string> values;
  const size_t open = debug.find('{');
  const size_t close = debug.rfind('}');
  if (open == std::string::npos || close == std::string::npos ||
      close <= open) {
    return values;
  }
  const std::string body = debug.substr(open + 1, close - open - 1);
  for (const std::string& part : Split(body, ',')) {
    const size_t eq = part.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = part.substr(0, eq);
    std::string value = part.substr(eq + 1);
    while (!key.empty() && key.front() == ' ') {
      key.erase(key.begin());
    }
    while (!value.empty() && value.front() == ' ') {
      value.erase(value.begin());
    }
    values[key] = value;
  }
  return values;
}

std::string JsonEscape(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (char c : in) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace

void FacoMetricsSnapshot::AddGauge(const std::string& name, double value) {
  samples_.push_back(FacoMetricSample{name, FacoMetricKind::kGauge, value});
}

void FacoMetricsSnapshot::AddCounter(const std::string& name, double value) {
  samples_.push_back(FacoMetricSample{name, FacoMetricKind::kCounter, value});
}

void FacoMetricsSnapshot::AddHistogram(const std::string& name,
                                       double value) {
  samples_.push_back(FacoMetricSample{name, FacoMetricKind::kHistogram, value});
}

void FacoMetricsSnapshot::AddFragmentationMetrics(
    const FragmentationStateTable* frag) {
  if (frag == nullptr) {
    return;
  }

  std::vector<double> zvdr;
  std::vector<double> invalid_ratio;
  std::vector<double> rbd;

  std::istringstream csv(frag->ExportCsv());
  std::string line;
  bool first = true;
  while (std::getline(csv, line)) {
    if (first) {
      first = false;
      continue;
    }
    if (line.empty()) {
      continue;
    }
    const std::vector<std::string> fields = Split(line, ',');
    if (fields.size() < 6) {
      continue;
    }
    const double valid_ratio = ToDouble(fields[2]);
    invalid_ratio.push_back(std::max(0.0, 1.0 - valid_ratio));
    zvdr.push_back(ToDouble(fields[4]));
    rbd.push_back(ToDouble(fields[5]));
  }

  AddHistogram("faco.frag.zvdr.p50", Percentile(zvdr, 0.50));
  AddHistogram("faco.frag.zvdr.p99", Percentile(zvdr, 0.99));
  AddHistogram("faco.frag.zvdr.max",
               zvdr.empty() ? 0.0 : *std::max_element(zvdr.begin(), zvdr.end()));
  AddHistogram("faco.frag.invalid_ratio.p50",
               Percentile(invalid_ratio, 0.50));
  AddHistogram("faco.frag.invalid_ratio.p99",
               Percentile(invalid_ratio, 0.99));
  AddGauge("faco.frag.rbd.max",
           rbd.empty() ? 0.0 : *std::max_element(rbd.begin(), rbd.end()));
}

void FacoMetricsSnapshot::AddBudgetMetrics(const std::string& debug) {
  const auto kv = ParseBraceDebug(debug);
  auto add = [&](const char* src, const char* dst) {
    auto it = kv.find(src);
    if (it != kv.end()) {
      AddGauge(dst, ToDouble(it->second));
    }
  };
  add("budget", "faco.budget.alloc_current");
  add("p_frag", "faco.budget.p_frag");
}

void FacoMetricsSnapshot::AddReorgMetrics(const std::string& debug) {
  const auto kv = ParseBraceDebug(debug);
  auto add_counter = [&](const char* src, const char* dst) {
    auto it = kv.find(src);
    if (it != kv.end()) {
      AddCounter(dst, ToDouble(it->second));
    }
  };
  auto add_gauge = [&](const char* src, const char* dst) {
    auto it = kv.find(src);
    if (it != kv.end()) {
      AddGauge(dst, ToDouble(it->second));
    }
  };

  add_counter("executed_plans", "faco.reorg.invocations_total");
  add_counter("migrated_bytes", "faco.reorg.bytes_migrated_total");
  add_gauge("tau_effective", "faco.reorg.tau_trigger_current");
  add_counter("rejected_plans", "faco.reorg.skipped_low_benefit");
  add_counter("cooldown_skip_count", "faco.reorg.cooldown_skip_count");
  add_counter("tiny_plan_skip_count", "faco.reorg.tiny_plan_skip_count");
  add_gauge("max_net_seen", "faco.reorg.max_net_seen");
}

void FacoMetricsSnapshot::AddLacrMetrics(const std::string& debug) {
  const auto kv = ParseBraceDebug(debug);
  auto add_gauge = [&](const char* src, const char* dst) {
    auto it = kv.find(src);
    if (it != kv.end()) {
      AddGauge(dst, ToDouble(it->second));
    }
  };

  add_gauge("active_zones", "faco.lacr.synergy_zones_pending");
  add_gauge("active_compaction_files",
            "faco.lacr.active_compaction_files");
  add_gauge("recent_zones", "faco.lacr.recent_compaction_zones");
  add_gauge("trace_samples", "faco.lacr.compaction_events_total");
}

void FacoMetricsSnapshot::AddRuntimeMetrics(const std::string& kv_text) {
  const auto kv = ParseKeyValueLines(kv_text);
  for (const auto& entry : kv) {
    AddCounter("faco.runtime." + entry.first, ToDouble(entry.second));
  }
}

std::string FacoMetricsSnapshot::ToText() const {
  std::ostringstream oss;
  oss << std::setprecision(12);
  for (const FacoMetricSample& sample : samples_) {
    oss << sample.name << " type=" << KindName(sample.kind)
        << " value=" << sample.value << "\n";
  }
  return oss.str();
}

std::string FacoMetricsSnapshot::ToJson() const {
  std::ostringstream oss;
  oss << std::setprecision(12);
  oss << "{\n  \"metrics\": [\n";
  for (size_t i = 0; i < samples_.size(); ++i) {
    const FacoMetricSample& sample = samples_[i];
    oss << "    {\"name\":\"" << JsonEscape(sample.name) << "\","
        << "\"type\":\"" << KindName(sample.kind) << "\","
        << "\"value\":" << sample.value << "}";
    if (i + 1 < samples_.size()) {
      oss << ",";
    }
    oss << "\n";
  }
  oss << "  ]\n}\n";
  return oss.str();
}

std::string FacoMetricsSnapshot::ToPrometheusText() const {
  std::ostringstream oss;
  oss << std::setprecision(12);
  for (const FacoMetricSample& sample : samples_) {
    std::string prometheus_name = sample.name;
    std::replace(prometheus_name.begin(), prometheus_name.end(), '.', '_');
    oss << "# TYPE " << prometheus_name << " " << KindName(sample.kind)
        << "\n";
    oss << prometheus_name << " " << sample.value << "\n";
  }
  return oss.str();
}

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && !defined(OS_WIN)
