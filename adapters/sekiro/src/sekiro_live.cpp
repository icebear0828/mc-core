#include "sekiro_live.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace sekiro::live {

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool finiteAndBounded(const native::FVector3& v) {
    return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z) &&
           std::fabs(v.X) < layout::kMaxCoordinate && std::fabs(v.Y) < layout::kMaxCoordinate &&
           std::fabs(v.Z) < layout::kMaxCoordinate;
}

float dot(const native::FVector3& a, const native::FVector3& b) {
    return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}

} // namespace

Pattern Pattern::parse(std::string_view text) {
    Pattern p;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size()) break;
        size_t j = i;
        while (j < text.size() && text[j] != ' ') ++j;
        const std::string_view tok = text.substr(i, j - i);
        i = j;
        if (tok == "?" || tok == "??") {
            p.bytes.push_back(0);
            p.mask.push_back(0);
        } else if (tok.size() == 2 && hexValue(tok[0]) >= 0 && hexValue(tok[1]) >= 0) {
            p.bytes.push_back(static_cast<uint8_t>(hexValue(tok[0]) * 16 + hexValue(tok[1])));
            p.mask.push_back(1);
        } else {
            return Pattern{};
        }
    }
    return p;
}

std::optional<std::vector<uint32_t>> findPattern(const IMemoryReader& reader, uintptr_t base, size_t size,
                                                 const Pattern& pattern, size_t chunk) {
    std::vector<uint32_t> hits;
    const size_t n = pattern.bytes.size();
    if (n == 0 || size < n || chunk < n) {
        return hits;
    }
    std::vector<uint8_t> buf(chunk + n - 1);
    for (size_t off = 0; off < size; off += chunk) {
        // Overlap by n-1 so matches straddling a chunk boundary are found exactly once.
        const size_t want = std::min(chunk + n - 1, size - off);
        if (!reader.read(base + off, buf.data(), want)) {
            return std::nullopt;
        }
        if (want < n) continue;
        const size_t last_start = std::min(want - n, chunk - 1); // starts owned by this chunk
        for (size_t i = 0; i <= last_start; ++i) {
            bool ok = true;
            for (size_t k = 0; k < n; ++k) {
                if (pattern.mask[k] && buf[i + k] != pattern.bytes[k]) {
                    ok = false;
                    break;
                }
            }
            if (ok) hits.push_back(static_cast<uint32_t>(off + i));
        }
    }
    return hits;
}

LiveBinder::LiveBinder(const IMemoryReader& reader, uintptr_t image_base, size_t image_size, size_t chunk)
    : reader_(reader), base_(image_base), size_(image_size), chunk_(chunk) {}

bool LiveBinder::readPointer(uintptr_t address, uintptr_t& out) const {
    uint64_t v = 0;
    if (!reader_.read(address, &v, sizeof(v))) return false;
    out = static_cast<uintptr_t>(v);
    return true;
}

BindStatus LiveBinder::scan() {
    if (status_ == BindStatus::Bound) return status_;

    auto resolve = [&](uint32_t match_rva) -> std::optional<uint32_t> {
        int32_t rel = 0;
        if (!reader_.read(base_ + match_rva + layout::kRipDispOffset, &rel, sizeof(rel))) return std::nullopt;
        const int64_t target = static_cast<int64_t>(match_rva) + layout::kRipInstrLength + rel;
        if (target < 0 || static_cast<size_t>(target) + sizeof(uint64_t) > size_) return std::nullopt;
        return static_cast<uint32_t>(target);
    };

    const auto wcm_hits = findPattern(reader_, base_, size_, Pattern::parse(layout::kWorldChrManAob), chunk_);
    if (!wcm_hits || wcm_hits->empty()) {
        status_ = BindStatus::Searching;
        return status_;
    }
    if (wcm_hits->size() > 1) {
        status_ = BindStatus::Ambiguous; // refuse to guess between several globals
        return status_;
    }
    const auto wcm = resolve(wcm_hits->front());
    if (!wcm) {
        status_ = BindStatus::Searching;
        return status_;
    }

    const auto cam_hits = findPattern(reader_, base_, size_, Pattern::parse(layout::kCameraAob), chunk_);
    std::vector<uint32_t> cams;
    if (cam_hits) {
        std::set<uint32_t> unique;
        for (uint32_t h : *cam_hits) {
            if (const auto g = resolve(h)) unique.insert(*g);
        }
        cams.assign(unique.begin(), unique.end());
    }
    if (cams.empty()) {
        status_ = BindStatus::Searching;
        return status_;
    }

    wcm_global_rva_ = *wcm;
    camera_global_rvas_ = std::move(cams);
    status_ = BindStatus::Bound;
    return status_;
}

bool LiveBinder::readCamera(uintptr_t object, LiveSample& out) const {
    float m[16];
    if (object == 0 || !reader_.read(object + layout::kCameraMatrix, m, sizeof(m))) return false;
    for (float v : m) {
        if (!std::isfinite(v)) return false;
    }
    const native::FVector3 right{m[0], m[1], m[2]};
    const native::FVector3 up{m[4], m[5], m[6]};
    const native::FVector3 fwd{m[8], m[9], m[10]};
    const native::FVector3 pos{m[12], m[13], m[14]};

    for (const native::FVector3& r : {right, up, fwd}) {
        if (std::fabs(std::sqrt(dot(r, r)) - 1.0f) > layout::kBasisTolerance) return false;
    }
    if (std::fabs(dot(right, up)) > layout::kBasisTolerance || std::fabs(dot(right, fwd)) > layout::kBasisTolerance ||
        std::fabs(dot(up, fwd)) > layout::kBasisTolerance) {
        return false;
    }
    if (std::fabs(m[15] - 1.0f) > 1e-3f || !finiteAndBounded(pos)) return false;

    out.cam_right = right;
    out.cam_up = up;
    out.cam_forward = fwd;
    out.cam_pos = pos;

    float fov = 0.0f;
    out.cam_fov_y = (reader_.read(object + layout::kCameraFov, &fov, sizeof(fov)) && std::isfinite(fov) &&
                     fov >= layout::kMinFov && fov <= layout::kMaxFov)
                        ? fov
                        : 0.0f;
    return true;
}

SampleStatus LiveBinder::sample(LiveSample& out) const {
    if (status_ != BindStatus::Bound) return SampleStatus::NotBound;

    uintptr_t world = 0;
    if (!readPointer(base_ + wcm_global_rva_, world) || world == 0) return SampleStatus::NotInWorld;
    uintptr_t player = 0;
    if (!readPointer(world + layout::kPlayerInsInWorldChrMan, player) || player == 0) return SampleStatus::NotInWorld;

    native::FVector3 pos, copy;
    float a[3], b[3];
    if (!reader_.read(player + layout::kChrPosition, a, sizeof(a)) ||
        !reader_.read(player + layout::kChrPositionCopy, b, sizeof(b))) {
        return SampleStatus::Invalid;
    }
    pos = {a[0], a[1], a[2]};
    copy = {b[0], b[1], b[2]};
    if (!finiteAndBounded(pos) || !finiteAndBounded(copy)) return SampleStatus::Invalid;
    // Both copies exactly at the origin: the object exists but has not been placed yet (seen for one
    // frame while a world loads). Not a position we should drive anything from.
    if (pos.X == 0.0f && pos.Y == 0.0f && pos.Z == 0.0f && copy.X == 0.0f && copy.Y == 0.0f && copy.Z == 0.0f) {
        return SampleStatus::NotInWorld;
    }
    if ((pos - copy).Length() > layout::kMaxPositionCopyDelta) return SampleStatus::Invalid;

    LiveSample s;
    s.player_pos = pos;

    // Try the candidate that worked last time first, then the others.
    const size_t count = camera_global_rvas_.size();
    for (size_t k = 0; k < count; ++k) {
        const size_t idx = (camera_hint_ + k) % count;
        uintptr_t object = 0;
        if (readPointer(base_ + camera_global_rvas_[idx], object) && readCamera(object, s)) {
            camera_hint_ = idx;
            out = s;
            return SampleStatus::Ok;
        }
    }
    return SampleStatus::Invalid;
}

void LiveMirror::update(const LiveSample& sample, float dt, native::ChrIns& player, native::ChrCam& camera) {
    native::FVector3 velocity{};
    if (have_previous_ && dt > 1e-4f) {
        velocity = (sample.player_pos - previous_) * (1.0f / dt);
        if (velocity.Length() > layout::kMaxPlausibleSpeed) velocity = {}; // teleport, not motion
    }
    previous_ = sample.player_pos;
    have_previous_ = true;

    player.Position = sample.player_pos;
    player.Velocity = velocity;
    camera.Position = sample.cam_pos;
    camera.Forward = sample.cam_forward;
}

} // namespace sekiro::live
