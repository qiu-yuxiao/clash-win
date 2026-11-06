// IconW 实现（T10 + F-D11）：
// 优先级 1：内嵌 @mui/icons-material 真实 SVG path（F-D11，2026-11-06）——
//   d 属性逐字取自 ClashVerge 工作区 node_modules/@mui/icons-material/*.js，
//   24×24 viewBox，解析为闭合环后 fill_path 偶奇填充（与 SVG 默认渲染一致）。
//   覆盖 UI 实际使用的全部矢量图标：标题栏三键（Minimize/CropSquare/
//   FilterNone/Close）、快捷条（SignalWifi0-4Bar/WifiOff/PushPinRounded/
//   SettingsRounded）、抽屉关闭（CloseRounded）、节点表头（BoltOutlined/
//   AccessTimeOutlined/SortOutlined/SortByAlphaOutlined/InboxRounded）。
// 优先级 2：手绘折线表（未覆盖名字的兜底）。
// 优先级 3：Segoe Fluent/MDL2 字形（glyph_for）。
#include "icon_w.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace clashwin::ui {

namespace {

std::string normalize_name(const std::string& mui_name) {
  std::string key;
  key.reserve(mui_name.size());
  for (char c : mui_name) {
    if (c == '_' || c == ' ' || c == '-') continue;
    key += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  }
  for (const char* suffix : {"rounded", "outlined", "sharp", "twotone"}) {
    size_t n = std::strlen(suffix);
    if (key.size() > n && key.compare(key.size() - n, n, suffix) == 0) {
      key.resize(key.size() - n);
      break;
    }
  }
  return key;
}

void add_circle(std::vector<PointF>& out, float cx, float cy, float r, int seg = 32,
                float a0 = 0.0f, float a1 = 6.2831853f) {
  for (int i = 0; i <= seg; ++i) {
    float a = a0 + (a1 - a0) * static_cast<float>(i) / seg;
    out.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
  }
}

// ---------- SVG path 解析器（F-D11） ----------
// 支持 M m L l H h V v C c S s Q q T t A a Z z；数字可紧邻（如 ".996.996"、
// "0 0 0-1.41"）。输出：扁平化闭合环（原始 24×24 坐标）。填充时未闭合子路径
// 按闭合处理（SVG fill 语义），此处统一补终点即闭合。
class SvgPathParser {
 public:
  explicit SvgPathParser(const char* d) : s_(d) {}

  std::vector<std::vector<PointF>> parse() {
    std::vector<std::vector<PointF>> loops;
    if (!s_) return loops;
    PointF cur{0, 0}, sub_start{0, 0};
    PointF last_c2{0, 0}, last_q1{0, 0};
    char prev = 0;
    bool has_prev = false;
    while (true) {
      skip_ws();
      char ch = peek();
      if (ch == 0) break;
      char cmd;
      if (is_cmd_char(ch)) {
        cmd = ch;
        ++i_;
      } else if (has_prev) {
        // 隐式重复：M/m 之后的坐标对按 L/l 处理，其余重复前命令
        if (prev == 'M') cmd = 'L';
        else if (prev == 'm') cmd = 'l';
        else cmd = prev;
      } else {
        break;
      }
      switch (cmd) {
        case 'M': case 'm': {
          float x, y;
          if (!num(x) || !num(y)) return loops;
          cur = {cur.x + (cmd == 'm' ? x : 0) + (cmd == 'M' ? x - cur.x : 0),
                 cur.y + (cmd == 'm' ? y : 0) + (cmd == 'M' ? y - cur.y : 0)};
          sub_start = cur;
          loops.emplace_back();
          loops.back().push_back(cur);
          break;
        }
        case 'L': case 'l': {
          float x, y;
          if (!num(x) || !num(y) || loops.empty()) return loops;
          if (cmd == 'l') { cur.x += x; cur.y += y; } else { cur = {x, y}; }
          loops.back().push_back(cur);
          break;
        }
        case 'H': case 'h': {
          float x;
          if (!num(x) || loops.empty()) return loops;
          cur.x = cmd == 'h' ? cur.x + x : x;
          loops.back().push_back(cur);
          break;
        }
        case 'V': case 'v': {
          float y;
          if (!num(y) || loops.empty()) return loops;
          cur.y = cmd == 'v' ? cur.y + y : y;
          loops.back().push_back(cur);
          break;
        }
        case 'C': case 'c': {
          float a, b, c2x, c2y, x, y;
          if (!num(a) || !num(b) || !num(c2x) || !num(c2y) || !num(x) ||
              !num(y) || loops.empty())
            return loops;
          PointF p1 = cmd == 'c' ? PointF{cur.x + a, cur.y + b} : PointF{a, b};
          PointF p2 = cmd == 'c' ? PointF{cur.x + c2x, cur.y + c2y}
                                 : PointF{c2x, c2y};
          PointF p3 = cmd == 'c' ? PointF{cur.x + x, cur.y + y} : PointF{x, y};
          flatten_cubic(loops.back(), cur, p1, p2, p3);
          last_c2 = p2;
          cur = p3;
          break;
        }
        case 'S': case 's': {
          float c2x, c2y, x, y;
          if (!num(c2x) || !num(c2y) || !num(x) || !num(y) || loops.empty())
            return loops;
          // 第一控制点 = 前一 cubic 第二控制点对当前点镜像（无前值则取当前点）
          PointF p1{2 * cur.x - last_c2.x, 2 * cur.y - last_c2.y};
          PointF p2 = cmd == 's' ? PointF{cur.x + c2x, cur.y + c2y}
                                 : PointF{c2x, c2y};
          PointF p3 = cmd == 's' ? PointF{cur.x + x, cur.y + y} : PointF{x, y};
          flatten_cubic(loops.back(), cur, p1, p2, p3);
          last_c2 = p2;
          cur = p3;
          break;
        }
        case 'Q': case 'q': {
          float qx, qy, x, y;
          if (!num(qx) || !num(qy) || !num(x) || !num(y) || loops.empty())
            return loops;
          PointF q1 = cmd == 'q' ? PointF{cur.x + qx, cur.y + qy} : PointF{qx, qy};
          PointF p2 = cmd == 'q' ? PointF{cur.x + x, cur.y + y} : PointF{x, y};
          // 二次 → 三次（c = p0 ± 2/3(q1-p0)、p2 + 2/3(q1-p2)）
          flatten_cubic(loops.back(), cur,
                        {cur.x + 2.0f / 3.0f * (q1.x - cur.x),
                         cur.y + 2.0f / 3.0f * (q1.y - cur.y)},
                        {p2.x + 2.0f / 3.0f * (q1.x - p2.x),
                         p2.y + 2.0f / 3.0f * (q1.y - p2.y)},
                        p2);
          last_q1 = q1;
          cur = p2;
          break;
        }
        case 'T': case 't': {
          float x, y;
          if (!num(x) || !num(y) || loops.empty()) return loops;
          PointF q1{2 * cur.x - last_q1.x, 2 * cur.y - last_q1.y};
          PointF p2 = cmd == 't' ? PointF{cur.x + x, cur.y + y} : PointF{x, y};
          flatten_cubic(loops.back(), cur,
                        {cur.x + 2.0f / 3.0f * (q1.x - cur.x),
                         cur.y + 2.0f / 3.0f * (q1.y - cur.y)},
                        {p2.x + 2.0f / 3.0f * (q1.x - p2.x),
                         p2.y + 2.0f / 3.0f * (q1.y - p2.y)},
                        p2);
          last_q1 = q1;
          cur = p2;
          break;
        }
        case 'A': case 'a': {
          float rx, ry, rot, x, y;
          bool large, sweep;
          if (!num(rx) || !num(ry) || !num(rot) || !flag(large) ||
              !flag(sweep) || !num(x) || !num(y) || loops.empty())
            return loops;
          PointF p2 = cmd == 'a' ? PointF{cur.x + x, cur.y + y} : PointF{x, y};
          flatten_arc(loops.back(), cur, p2, rx, ry, rot, large, sweep);
          cur = p2;
          break;
        }
        case 'Z': case 'z': {
          if (!loops.empty()) {
            cur = sub_start;
            loops.back().push_back(cur);  // 显式闭合（描边兜底可见）
          }
          break;
        }
        default:
          return loops;  // 未知命令：返回已解析部分
      }
      prev = cmd;
      has_prev = true;
    }
    return loops;
  }

 private:
  static bool is_cmd_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
  }
  char peek() const { return s_[i_]; }
  void skip_ws() {
    while (s_[i_] == ' ' || s_[i_] == ',' || s_[i_] == '\t' || s_[i_] == '\n' ||
           s_[i_] == '\r')
      ++i_;
  }
  bool num(float& out) {
    skip_ws();
    size_t start = i_;
    if (s_[i_] == '+' || s_[i_] == '-') ++i_;
    bool digits = false, dot = false;
    while (s_[i_]) {
      char ch = s_[i_];
      if (ch >= '0' && ch <= '9') {
        digits = true;
        ++i_;
      } else if (ch == '.' && !dot) {
        dot = true;
        ++i_;
      } else if ((ch == 'e' || ch == 'E') && digits) {
        size_t save = i_;
        ++i_;
        if (s_[i_] == '+' || s_[i_] == '-') ++i_;
        if (s_[i_] >= '0' && s_[i_] <= '9') {
          while (s_[i_] >= '0' && s_[i_] <= '9') ++i_;
        } else {
          i_ = save;
          break;
        }
      } else {
        break;
      }
    }
    if (!digits) {
      i_ = start;
      return false;
    }
    out = static_cast<float>(
        std::strtod(std::string(s_ + start, i_ - start).c_str(), nullptr));
    return true;
  }
  bool flag(bool& out) {
    skip_ws();
    if (s_[i_] == '0') {
      out = false;
      ++i_;
      return true;
    }
    if (s_[i_] == '1') {
      out = true;
      ++i_;
      return true;
    }
    return false;
  }

  static constexpr int kSeg = 12;  // 每段曲线扁平化细分数（≤20px 下足够圆滑）

  static void flatten_cubic(std::vector<PointF>& out, PointF p0, PointF p1,
                            PointF p2, PointF p3) {
    for (int k = 1; k <= kSeg; ++k) {
      float t = static_cast<float>(k) / kSeg, u = 1.0f - t;
      float w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t,
            w3 = t * t * t;
      out.push_back({w0 * p0.x + w1 * p1.x + w2 * p2.x + w3 * p3.x,
                     w0 * p0.y + w1 * p1.y + w2 * p2.y + w3 * p3.y});
    }
  }

  // 端点圆弧 → 中心参数化（SVG 规范 F.6.5）→ 逐段 ≤90° 三次贝塞尔
  static void flatten_arc(std::vector<PointF>& out, PointF p1, PointF p2,
                          float rx, float ry, float rot_deg, bool large,
                          bool sweep) {
    if (rx <= 0 || ry <= 0) {
      out.push_back(p2);
      return;
    }
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    float phi = rot_deg * 3.14159265358979f / 180.0f;
    float cp = std::cos(phi), sp = std::sin(phi);
    float dx2 = (p1.x - p2.x) / 2, dy2 = (p1.y - p2.y) / 2;
    float x1p = cp * dx2 + sp * dy2, y1p = -sp * dx2 + cp * dy2;
    // 半径过小时按规范放大
    float lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
    if (lam > 1.0f) {
      float s = std::sqrt(lam);
      rx *= s;
      ry *= s;
    }
    float sign = (large != sweep) ? 1.0f : -1.0f;
    float den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    float num_ = rx * rx * ry * ry - den;
    float co = sign * std::sqrt(den > 0 ? (num_ > 0 ? num_ / den : 0.0f) : 0.0f);
    float cxp = co * rx * y1p / ry;
    float cyp = -co * ry * x1p / rx;
    float cx = cp * cxp - sp * cyp + (p1.x + p2.x) / 2;
    float cy = sp * cxp + cp * cyp + (p1.y + p2.y) / 2;
    auto ang = [&](float ux, float uy, float vx, float vy) {
      float dot = ux * vx + uy * vy;
      float len = std::sqrt(ux * ux + uy * uy) * std::sqrt(vx * vx + vy * vy);
      float a = std::acos(std::max(-1.0f, std::min(1.0f, dot / (len ? len : 1))));
      if (ux * vy - uy * vx < 0) a = -a;
      return a;
    };
    float th1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    float dth = ang((x1p - cxp) / rx, (y1p - cyp) / ry,
                    (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dth > 0) dth -= 6.283185307179586f;
    if (sweep && dth < 0) dth += 6.283185307179586f;
    int nseg = static_cast<int>(std::ceil(std::fabs(dth) / 1.5707963267948966f));
    if (nseg < 1) nseg = 1;
    float delta = dth / nseg;
    float t = 4.0f / 3.0f * std::tan(delta / 4.0f);
    float th = th1;
    for (int k = 0; k < nseg; ++k) {
      float th2 = th + delta;
      float c1x = cx + rx * std::cos(th) - rx * t * std::sin(th);
      float c1y = cy + ry * std::sin(th) + ry * t * std::cos(th);
      float c2x = cx + rx * std::cos(th2) + rx * t * std::sin(th2);
      float c2y = cy + ry * std::sin(th2) - ry * t * std::cos(th2);
      float ex = cx + rx * std::cos(th2);
      float ey = cy + ry * std::sin(th2);
      flatten_cubic(out, p1, {c1x, c1y}, {c2x, c2y}, {ex, ey});
      th = th2;
      p1 = {ex, ey};
    }
  }

  const char* s_;
  size_t i_ = 0;
};

// ---------- MUI 真实 SVG path 表（F-D11） ----------
// d 属性逐字取自 ClashVerge node_modules/@mui/icons-material/*.js；
// 每个 <path> 子元素一条记录（SignalWifi1-3Bar 有 fillOpacity .3 的背景扇形）。
struct MuiDef {
  float opacity;
  const char* d;
};

const std::unordered_map<std::string, std::vector<MuiDef>>& mui_table() {
  static const std::unordered_map<std::string, std::vector<MuiDef>> kTable = {
      // 标题栏三键（window-controller.tsx）+ 关闭键。
      // "winclose" = Close（直角 X，标题栏；避开 normalize 对 "close" 的
      //   Rounded 归并）；"close" = CloseRounded（圆角 X，抽屉关闭）。
      {"winclose",
       {{1.0f,
         "M19 6.41 17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 "
         "13.41 17.59 19 19 17.59 13.41 12z"}}},
      {"close",
       {{1.0f,
         "M18.3 5.71a.996.996 0 0 0-1.41 0L12 10.59 7.11 5.7a.996.996 0 0 "
         "0-1.41 0c-.39.39-.39 1.02 0 1.41L10.59 12 5.7 16.89c-.39.39-.39 "
         "1.02 0 1.41s1.02.39 1.41 0L12 13.41l4.89 4.89c.39.39 1.02.39 1.41 "
         "0s.39-1.02 0-1.41L13.41 12l4.89-4.89c.38-.38.38-1.02 0-1.4"}}},
      {"minimize", {{1.0f, "M6 19h12v2H6z"}}},
      {"maximize",
       {{1.0f,
         "M18 4H6c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h12c1.1 0 2-.9 "
         "2-2V6c0-1.1-.9-2-2-2m0 14H6V6h12z"}}},
      {"cropsquare",
       {{1.0f,
         "M18 4H6c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h12c1.1 0 2-.9 "
         "2-2V6c0-1.1-.9-2-2-2m0 14H6V6h12z"}}},
      {"restore",
       {{1.0f,
         "M3 5H1v16c0 1.1.9 2 2 2h16v-2H3zm18-4H7c-1.1 0-2 .9-2 2v14c0 "
         "1.1.9 2 2 2h14c1.1 0 2-.9 2-2V3c0-1.1-.9-2-2-2m0 16H7V3h14z"}}},
      {"filternone",
       {{1.0f,
         "M3 5H1v16c0 1.1.9 2 2 2h16v-2H3zm18-4H7c-1.1 0-2 .9-2 2v14c0 "
         "1.1.9 2 2 2h14c1.1 0 2-.9 2-2V3c0-1.1-.9-2-2-2m0 16H7V3h14z"}}},
      {"settings",
       {{1.0f,
         "M19.5 12c0-.23-.01-.45-.03-.68l1.86-1.41c.4-.3.51-.86.26-1.3l-1.87-"
         "3.23c-.25-.44-.79-.62-1.25-.42l-2.15.91c-.37-.26-.76-.49-1.17-.68l-."
         "29-2.31c-.06-.5-.49-.88-.99-.88h-3.73c-.51 0-.94.38-1 .88l-.29 "
         "2.31c-.41.19-.8.42-1.17.68l-2.15-.91c-.46-.2-1-.02-1.25.42L2.41 "
         "8.62c-.25.44-.14.99.26 1.3l1.86 1.41c-.02.22-.03.44-.03.67s.01.45."
         "03.68l-1.86 1.41c-.4.3-.51.86-.26 1.3l1.87 3.23c.25.44.79.62 "
         "1.25.42l2.15-.91c.37.26.76.49 1.17.68l.29 2.31c.06.5.49.88.99.88h3."
         "73c.5 0 .93-.38.99-.88l.29-2.31c.41-.19.8-.42 1.17-.68l2.15.91c.46."
         "2 1 .02 1.25-.42l1.87-3.23c.25-.44.14-.99-.26-1.3l-1.86-1.41c.03-."
         "23.04-.45.04-.68m-7.46 3.5c-1.93 0-3.5-1.57-3.5-3.5s1.57-3.5 3.5-3."
         "5 3.5 1.57 3.5 3.5-1.57 3.5-3.5 3.5"}}},
      {"pushpin",
       {{1.0f,
         "M19 12.87c0-.47-.34-.85-.8-.98C16.93 11.54 16 10.38 16 9V4h1c.55 0 "
         "1-.45 1-1s-.45-1-1-1H7c-.55 0-1 .45-1 1s.45 1 1 1h1v5c0 "
         "1.38-.93 2.54-2.2 2.89-.46.13-.8.51-.8.98V13c0 .55.45 1 1 "
         "1h4.98l.02 7c0 .55.45 1 1 1s1-.45 1-1l-.02-7H18c.55 0 1-.45 1-1z"}}},
      // 延迟徽章信号族（style-helpers.tsx getSignalIcon 六态）。
      // "wifi" 为初始占位名（quick_bar set_icon("wifi")），等价 SignalWifi3Bar。
      {"wifi",
       {{0.3f,
         "M12.01 21.49L23.64 7c-.45-.34-4.93-4-11.64-4C5.28 3 .81 6.66.36 "
         "7l11.63 14.49.01.01.01-.01z"},
        {1.0f,
         "M3.53 10.95l8.46 10.54.01.01.01-.01 8.46-10.54C20.04 10.62 16.81 8 "
         "12 8c-4.81 0-8.04 2.62-8.47 2.95z"}}},
      {"signalwifi0bar",
       {{1.0f,
         "M12 6c3.33 0 6.49 1.08 9.08 3.07L12 18.17l-9.08-9.1C5.51 7.08 8.67 "
         "6 12 6m0-2C7.31 4 3.07 5.9 0 8.98L12 21 24 8.98C20.93 5.9 16.69 4 "
         "12 4"}}},
      {"signalwifi1bar",
       {{0.3f,
         "M12.01 21.49L23.64 7c-.45-.34-4.93-4-11.64-4C5.28 3 .81 6.66.36 "
         "7l11.63 14.49.01.01.01-.01z"},
        {1.0f,
         "M6.67 14.86L12 21.49v.01l.01-.01 5.33-6.63C17.06 14.65 15.03 13 12 "
         "13s-5.06 1.65-5.33 1.86z"}}},
      {"signalwifi2bar",
       {{0.3f,
         "M12.01 21.49L23.64 7c-.45-.34-4.93-4-11.64-4C5.28 3 .81 6.66.36 "
         "7l11.63 14.49.01.01.01-.01z"},
        {1.0f,
         "M4.79 12.52l7.2 8.98H12l.01-.01 7.2-8.98C18.85 12.24 16.1 10 12 "
         "10s-6.85 2.24-7.21 2.52z"}}},
      {"signalwifi3bar",
       {{0.3f,
         "M12.01 21.49L23.64 7c-.45-.34-4.93-4-11.64-4C5.28 3 .81 6.66.36 "
         "7l11.63 14.49.01.01.01-.01z"},
        {1.0f,
         "M3.53 10.95l8.46 10.54.01.01.01-.01 8.46-10.54C20.04 10.62 16.81 8 "
         "12 8c-4.81 0-8.04 2.62-8.47 2.95z"}}},
      {"signalwifi4bar",
       {{1.0f,
         "M12.01 21.49 23.64 7c-.45-.34-4.93-4-11.64-4C5.28 3 .81 6.66.36 "
         "7l11.63 14.49.01.01z"}}},
      {"wifioff",
       {{1.0f,
         "M22.99 9C19.15 5.16 13.8 3.76 8.84 4.78l2.52 2.52c3.47-.17 6.99 "
         "1.05 9.63 3.7zm-4 4c-1.29-1.29-2.84-2.13-4.49-2.56l3.53 3.53zM2 "
         "3.05 5.07 6.1C3.6 6.82 2.22 7.78 1 9l1.99 2c1.24-1.24 2.67-2.16 "
         "4.2-2.77l2.24 2.24C7.81 10.89 6.27 11.73 5 13v.01L6.99 15c1.36-1.36 "
         "3.14-2.04 4.92-2.06L18.98 20l1.27-1.26L3.29 1.79zM9 17l3 3 3-3c-1."
         "65-1.66-4.34-1.66-6 0"}}},
      // 节点表头（proxy-head.tsx）
      {"bolt",
       {{1.0f,
         "M11 21h-1l1-7H7.5c-.88 0-.33-.75-.31-.78C8.48 10.94 10.42 7.54 "
         "13.01 3h1l-1 7h3.51c.4 0 .62.19.4.66C12.97 17.55 11 21 11 21"}}},
      {"history",
       {{1.0f,
         "M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 "
         "12S17.52 2 11.99 2M12 20c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 "
         "8-3.58 8-8 8m.5-13H11v6l5.25 3.15.75-1.23-4.5-2.67z"}}},
      {"sort", {{1.0f, "M3 18h6v-2H3zM3 6v2h18V6zm0 7h12v-2H3z"}}},
      {"sortbyalpha",
       {{1.0f,
         "M14.94 4.66h-4.72l2.36-2.36zm-4.69 14.71h4.66l-2.33 2.33zM6.1 6.27 "
         "1.6 17.73h1.84l.92-2.45h5.11l.92 2.45h1.84L7.74 "
         "6.27zm-1.13 7.37 1.94-5.18 1.94 5.18zm10.76 2.5h6.12v1.59h-8.53v-1."
         "29l5.92-8.56h-5.88v-1.6h8.3v1.26z"}}},
      {"inbox",
       {{1.0f,
         "M19 3H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 "
         "2-2V5c0-1.1-.9-2-2-2m0 2v9h-3.56c-.36 0-.68.19-.86.5-.52.9-1.47 "
         "1.5-2.58 1.5s-2.06-.6-2.58-1.5c-.18-.31-.51-.5-.86-.5H5V5z"}}},
  };
  return kTable;
}

}  // namespace

bool IconW::mui_paths_for(const std::string& mui_name,
                          std::vector<MuiLoopSet>& out) {
  out.clear();
  std::string key = normalize_name(mui_name);
  const auto& table = mui_table();
  auto it = table.find(key);
  if (it == table.end()) return false;
  for (const auto& def : it->second) {
    MuiLoopSet ls;
    ls.opacity = def.opacity;
    ls.loops = SvgPathParser(def.d).parse();
    if (!ls.loops.empty()) out.push_back(std::move(ls));
  }
  return !out.empty();
}

// SVG path 扁平化后的折线集合（归一化 0..1，y 向下）。
// F-D11 后仅作未入 MUI 表的名字兜底；常用名已全部由 MUI path 表接管。
static bool build_vector_paths(const std::string& key,
                               std::vector<std::vector<PointF>>& out) {
  out.clear();
  if (key == "clear") {
    out.push_back({{0.18f, 0.18f}, {0.82f, 0.82f}});
    out.push_back({{0.82f, 0.18f}, {0.18f, 0.82f}});
    return true;
  }
  return false;
}

// ---- IconW 成员定义 ----

IconW::IconW(Widget* parent) : Widget(parent) {}

wchar_t IconW::glyph_for(const std::string& mui_name) {
  static const std::unordered_map<std::string, wchar_t> kMap = {
      {"close", 0xE711},        // Cancel（X）
      {"clear", 0xE711},
      {"minimize", 0xE921},     // ChromeMinimize
      {"maximize", 0xE922},     // ChromeMaximize（CropSquare）
      {"cropsquare", 0xE922},
      {"restore", 0xE923},      // ChromeRestore（FilterNone）
      {"filternone", 0xE923},
      {"refresh", 0xE72C},
      {"delete", 0xE74D},
      {"info", 0xE946},
      {"help", 0xE897},         // Help（HelpOutline 近似）
      {"helpoutline", 0xE897},
      {"inbox", 0xE715},        // Mail 近似（原版空态图标）
      {"keyboardreturn", 0xE751},
      {"arrowupward", 0xE74A},
      {"arrowdownward", 0xE74B},
      {"keyboardarrowup", 0xE70E},    // ChevronUp
      {"keyboardarrowdown", 0xE70D},  // ChevronDown
      {"code", 0xE943},
      {"viewmodule", 0xE71D},   // AllApps 近似
      {"home", 0xE80F},
      {"lockopen", 0xE785},     // LockOpened
      {"pushpin", 0xE718},      // Pinned
      {"settings", 0xE713},
      {"search", 0xE721},
      {"chevronright", 0xE76C},
      {"chevronleft", 0xE76B},
      {"check", 0xE73E},
      {"sync", 0xE777},
      {"bolt", 0xE945},         // LightningBolt 近似（原版 ⚡ 组测速）
      {"wifi", 0xE701},         // Wifi（快捷条延迟徽章）
      {"history", 0xE81C},      // History 时钟（排序态 1）
      {"sort", 0xE8CB},         // Sort（排序态 0）
      {"sortbyalpha", 0xE8CC},  // Font 字形（SortByAlpha 近似，字体无对应码点）
      {"play", 0xE768},
      {"pause", 0xE769},
  };
  std::string key = normalize_name(mui_name);
  auto it = kMap.find(key);
  return it != kMap.end() ? it->second : 0;
}

bool IconW::has_vector_path(const std::string& mui_name) {
  std::vector<MuiLoopSet> tmp;
  if (mui_paths_for(mui_name, tmp)) return true;
  std::vector<std::vector<PointF>> tmp2;
  return build_vector_paths(normalize_name(mui_name), tmp2);
}

void IconW::set_icon(const std::string& mui_name) {
  glyph_ = glyph_for(mui_name);
  name_ = normalize_name(mui_name);
  invalidate();
}

void IconW::set_glyph(wchar_t g) {
  glyph_ = g;
  invalidate();
}

SizeF IconW::on_measure(float max_w, float max_h) {
  (void)max_w; (void)max_h;
  return {box_w > 0 ? box_w : size_, box_h > 0 ? box_h : size_};
}

void IconW::on_paint(IRenderTarget& rt) {
  const float w = bounds().w, h = bounds().h;
  // 旋转（绕盒中心；src: _layout.tsx:1362-1364 transform: rotate(45deg)）
  auto rotated = [&](const std::vector<std::vector<PointF>>& loops,
                     std::vector<std::vector<PointF>>& out) {
    out = loops;
    if (rotate_deg == 0.0f) return;
    const float cx = w * 0.5f, cy = h * 0.5f;
    const float rad = rotate_deg * 3.14159265f / 180.0f;
    const float ca = std::cos(rad), sa = std::sin(rad);
    for (auto& poly : out) {
      for (auto& p : poly) {
        float rx = p.x - cx, ry = p.y - cy;
        p.x = cx + rx * ca - ry * sa;
        p.y = cy + rx * sa + ry * ca;
      }
    }
  };

  // 优先级 1：MUI 真实 SVG path（F-D11）。24×24 viewBox 整盒映射
  //   （MUI <svg> 即按 fontSize 撑满，无额外缩放），每条 <path> 子元素
  //   独立按其 fill-opacity 填充（SignalWifi1-3Bar 背景扇形 .3）。
  std::vector<MuiLoopSet> mui;
  if (!name_.empty() && mui_paths_for(name_, mui)) {
    const float s = w < h ? w : h;
    const float scale = s / 24.0f;
    const float ox = (w - s) * 0.5f, oy = (h - s) * 0.5f;
    std::vector<std::vector<PointF>> scaled, out;
    auto emit_fill = [&](const std::vector<std::vector<PointF>>& loops,
                         const Color4f& c) {
      rotated(loops, out);
      rt.fill_path(out, Brush{c});
    };
    for (const auto& ls : mui) {
      scaled.clear();
      for (const auto& poly : ls.loops) {
        std::vector<PointF> pts;
        pts.reserve(poly.size());
        for (const auto& p : poly)
          pts.push_back({ox + p.x * scale, oy + p.y * scale});
        scaled.push_back(std::move(pts));
      }
      if (glow_) {
        // src: _layout.tsx:1359-1361 drop-shadow(0 0 3px rgba(255,59,48,.85))
        //   + drop-shadow(0 1px 1px rgba(255,255,255,.45))；
        //   无高斯模糊核 → 8 向 2.5px 偏移填充 + 白色下沿近似
        Color4f g = glow_color_;
        for (int k = 0; k < 8; ++k) {
          const float a = k * 3.14159265f / 4.0f;
          std::vector<std::vector<PointF>> off;
          for (const auto& poly : scaled) {
            std::vector<PointF> pts;
            for (const auto& p : poly)
              pts.push_back({p.x + std::cos(a) * 2.5f, p.y + std::sin(a) * 2.5f});
            off.push_back(std::move(pts));
          }
          rt.fill_path(off, Brush{g});
        }
        Color4f wcol{1, 1, 1, 0.45f};
        std::vector<std::vector<PointF>> off;
        for (const auto& poly : scaled) {
          std::vector<PointF> pts;
          for (const auto& p : poly) pts.push_back({p.x, p.y + 1.0f});
          off.push_back(std::move(pts));
        }
        rt.fill_path(off, Brush{wcol});
      }
      Color4f c = color_;
      c.a *= ls.opacity;
      emit_fill(scaled, c);
    }
    return;
  }

  // 优先级 2：手绘折线（描边）——未入 MUI 表的名字兜底
  std::vector<std::vector<PointF>> paths;
  if (!name_.empty() && build_vector_paths(name_, paths)) {
    float s = w < h ? w : h;
    float side = s * 0.80f;
    float ox = (w - side) * 0.5f, oy = (h - side) * 0.5f;
    Pen pen;
    pen.color = color_;
    pen.width = size_ / 12.0f > 1.0f ? size_ / 12.0f : 1.0f;
    std::vector<PointF> pts;
    auto emit = [&](const Pen& use_pen, float dx, float dy) {
      for (const auto& poly : paths) {
        pts.clear();
        pts.reserve(poly.size());
        for (const auto& p : poly) {
          float x = ox + p.x * side, y = oy + p.y * side;
          if (rotate_deg != 0.0f) {
            // src: _layout.tsx:1362-1364 transform: rotate(45deg)（绕盒中心）
            const float cx = w * 0.5f, cy = h * 0.5f;
            const float rad = rotate_deg * 3.14159265f / 180.0f;
            const float ca = std::cos(rad), sa = std::sin(rad);
            const float rx = x - cx, ry = y - cy;
            x = cx + rx * ca - ry * sa;
            y = cy + rx * sa + ry * ca;
          }
          pts.push_back({x + dx, y + dy});
        }
        if (pts.size() >= 2)
          rt.draw_polyline(pts.data(), static_cast<int>(pts.size()), use_pen);
      }
    };
    if (glow_) {
      // src: _layout.tsx:1359-1361 drop-shadow(0 0 3px rgba(255,59,48,.85))
      //   + drop-shadow(0 1px 1px rgba(255,255,255,.45))；
      //   无高斯模糊核 → 8 向 2px/3px 环形重绘近似
      Pen g; g.color = glow_color_; g.width = pen.width;
      for (int k = 0; k < 8; ++k) {
        const float a = k * 3.14159265f / 4.0f;
        emit(g, std::cos(a) * 2.5f, std::sin(a) * 2.5f);
      }
      Pen w2; w2.color = Color4f::hex(0xFFFFFF, 0.45f); w2.width = pen.width;
      emit(w2, 0.0f, 1.0f);
    }
    emit(pen, 0.0f, 0.0f);
    return;
  }
  // 优先级 3：字体字形（未列入矢量表的图标保持原行为）
  if (!glyph_) return;
  TextStyle ts;
  ts.font_family = font_fallback_ ? L"Segoe MDL2 Assets" : L"Segoe Fluent Icons";
  ts.size = size_;
  ts.color = color_;
  ts.align_h = AlignH::Center;
  ts.align_v = AlignV::Center;
  rt.draw_text(std::wstring(1, glyph_), {0, 0, bounds().w, bounds().h}, ts);
}

bool IconW::on_mouse(const MouseEvent& e) {
  if (!on_click) return false;
  switch (e.type) {
    case MouseEventType::Enter: hover_ = true; invalidate(); return true;
    case MouseEventType::Leave: hover_ = false; pressed_ = false; invalidate(); return true;
    case MouseEventType::Down: pressed_ = true; invalidate(); return true;
    case MouseEventType::Up:
      // 【CLICKFIX】e.pos 局部坐标 vs 局部矩形（原 bounds() 父坐标致点击永不触发）
      if (pressed_ && RectF{0, 0, bounds().w, bounds().h}.contains(e.pos.x, e.pos.y) &&
          on_click)
        on_click();
      pressed_ = false;
      invalidate();
      return true;
    default: return false;
  }
}

}  // namespace clashwin::ui
