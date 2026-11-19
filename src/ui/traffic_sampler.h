#pragma once
// TrafficDataSampler（1:1 移植 ClashVerge src/utils/traffic-sampler.ts）。
// 原始环形窗口 + 5:1 均值压缩队列，支撑 EnhancedTrafficGraph 的 1m / 5m / 10m 查询。
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace clashwin::ui {

struct TrafficDataPoint {
  double up = 0.0;
  double down = 0.0;
  int64_t timestamp = 0;
};

struct TrafficSamplerStats {
  size_t raw_buffer_size = 0;
  size_t compressed_buffer_size = 0;
  size_t compression_queue_size = 0;
  size_t total_memory_points = 0;
};

struct TrafficSamplerConfig {
  // src: hooks/use-traffic-monitor.ts:60-62 rawDataMinutes / compressedDataMinutes / compressionRatio
  int raw_data_minutes = 10;
  int compressed_data_minutes = 60;
  int compression_ratio = 5;
};

class TrafficDataSampler {
 public:
  explicit TrafficDataSampler(TrafficSamplerConfig cfg = {}) : config_(cfg) {}

  void add_data_point(const TrafficDataPoint& point, int64_t now_ms) {
    TrafficDataPoint pt = point;
    if (pt.timestamp <= 0) pt.timestamp = now_ms;
    raw_buffer_.push_back(pt);
    compression_queue_.push_back(pt);

    if (config_.compression_ratio > 0 &&
        static_cast<int>(compression_queue_.size()) >=
            config_.compression_ratio) {
      compress_chunk(static_cast<size_t>(config_.compression_ratio));
    }
    cleanup_expired(now_ms);
  }

  std::vector<TrafficDataPoint> get_data_for_time_range(int minutes,
                                                        int64_t now_ms) const {
    const int64_t cutoff =
        now_ms - static_cast<int64_t>(minutes) * 60LL * 1000LL;
    std::vector<TrafficDataPoint> out;
    if (minutes <= config_.raw_data_minutes) {
      out.reserve(raw_buffer_.size());
      for (const auto& p : raw_buffer_) {
        if (p.timestamp > cutoff) out.push_back(p);
      }
      return out;
    }

    const int64_t raw_cutoff =
        now_ms - static_cast<int64_t>(config_.raw_data_minutes) * 60LL * 1000LL;
    for (const auto& p : compressed_buffer_) {
      if (p.timestamp > cutoff && p.timestamp <= raw_cutoff) out.push_back(p);
    }
    for (const auto& p : raw_buffer_) {
      if (p.timestamp > raw_cutoff) out.push_back(p);
    }
    return out;
  }

  TrafficSamplerStats get_stats() const {
    return {
        raw_buffer_.size(),
        compressed_buffer_.size(),
        compression_queue_.size(),
        raw_buffer_.size() + compressed_buffer_.size(),
    };
  }

  void clear() {
    raw_buffer_.clear();
    compressed_buffer_.clear();
    compression_queue_.clear();
  }

  const TrafficSamplerConfig& config() const { return config_; }

 private:
  void compress_chunk(size_t count) {
    if (count == 0 || compression_queue_.size() < count) return;
    double sum_up = 0.0;
    double sum_down = 0.0;
    double sum_ts = 0.0;
    for (size_t i = 0; i < count; ++i) {
      const auto& p = compression_queue_.front();
      sum_up += p.up;
      sum_down += p.down;
      sum_ts += static_cast<double>(p.timestamp);
      compression_queue_.pop_front();
    }
    const double denom = static_cast<double>(count);
    TrafficDataPoint avg;
    avg.up = sum_up / denom;
    avg.down = sum_down / denom;
    avg.timestamp = static_cast<int64_t>(sum_ts / denom);
    compressed_buffer_.push_back(avg);
  }

  void cleanup_expired(int64_t now_ms) {
    const int64_t raw_cutoff =
        now_ms - static_cast<int64_t>(config_.raw_data_minutes) * 60LL * 1000LL;
    while (!raw_buffer_.empty() && raw_buffer_.front().timestamp <= raw_cutoff) {
      raw_buffer_.pop_front();
    }
    const int64_t comp_cutoff =
        now_ms -
        static_cast<int64_t>(config_.compressed_data_minutes) * 60LL * 1000LL;
    while (!compressed_buffer_.empty() &&
           compressed_buffer_.front().timestamp <= comp_cutoff) {
      compressed_buffer_.pop_front();
    }
  }

  TrafficSamplerConfig config_;
  std::deque<TrafficDataPoint> raw_buffer_;
  std::deque<TrafficDataPoint> compressed_buffer_;
  std::deque<TrafficDataPoint> compression_queue_;
};

}  // namespace clashwin::ui
