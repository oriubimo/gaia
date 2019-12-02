// Copyright 2019, Beeri 15.  All rights reserved.
// Author: Roman Gershman (romange@gmail.com)
//
#include "mr/operator_executor.h"

#include "base/logging.h"

namespace mr3 {
using namespace boost;
using namespace std;

void OperatorExecutor::RegisterContext(RawContext* context) {
  context->finalized_maps_ = finalized_maps_;
}

void OperatorExecutor::FinalizeContext(long items_cnt, RawContext* raw_context) {
  raw_context->Flush();
  parse_errors_.fetch_add(raw_context->parse_errors(), std::memory_order_relaxed);

  std::lock_guard<fibers::mutex> lk(mu_);
  for (const auto& k_v : raw_context->metric_map_) {
    metric_map_[string(k_v.first)] += k_v.second;
  }
  metric_map_["fn-calls"] += items_cnt;
  metric_map_["fn-writes"] += raw_context->item_writes();

  // Merge frequency maps. We aggregate counters for all the contexts.
  for (auto& k_v : raw_context->freq_maps_) {
    auto& any = freq_maps_[k_v.first];
    any.Add(k_v.second);
  }
}

void OperatorExecutor::ExtractFreqMap(function<void(string, detail::FreqMapWrapper&&)> cb) {
  for (auto& k_v : freq_maps_) {
    cb(k_v.first, std::move(k_v.second));
  }
  freq_maps_.clear();
}

void OperatorExecutor::Init(const RawContext::FreqMapRegistry& prev_maps) {
  finalized_maps_ = &prev_maps;
  InitInternal();
}

void OperatorExecutor::SetMetaData(const pb::Input::FileSpec& fs, RawContext* context) {
  using FS = pb::Input::FileSpec;
  switch (fs.metadata_case()) {
    case FS::METADATA_NOT_SET:
      context->metadata_.emplace<absl::monostate>();
    break;
    case FS::kStrval:
      context->metadata_ = fs.strval();
    break;
    case FS::kI64Val:
      context->metadata_ = fs.i64val();
    break;
    default:
      LOG(FATAL) << "Invalid file spec tag " << fs.ShortDebugString();
  }
}

}  // namespace mr3
