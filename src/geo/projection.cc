#include "src/geo/projection.h"

#include <cmath>

namespace jpy::geo {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusKm = 6371.0;
constexpr double kPhi1 = 30.0 * kPi / 180.0;  // standard parallels
constexpr double kPhi2 = 44.0 * kPi / 180.0;

double TanHalf(double phi) { return std::tan(kPi / 4 + phi / 2); }

struct Lambert {
  double n, f, rho0;
  Lambert() {
    n = std::log(std::cos(kPhi1) / std::cos(kPhi2)) / std::log(TanHalf(kPhi2) / TanHalf(kPhi1));
    f = std::cos(kPhi1) * std::pow(TanHalf(kPhi1), n) / n;
    rho0 = kEarthRadiusKm * f / std::pow(TanHalf(Projection::kLat0 * kPi / 180.0), n);
  }
};

const Lambert& Lcc() {
  static const Lambert l;
  return l;
}

// Inset rules. `offset` is the degree shift of the rule's reference point
// (box centre); the translation is applied in projected km so the islands
// keep exactly the shape they would have at their true position.
constexpr Inset kInsets[] = {
    // Okinawa Island and the nearby islands -> Sea of Japan, NW of Honshu.
    {"47", "47", 126.0, 130.5, 24.0, 28.5, {5.1, 13.4}},
    // Sakishima (Miyako, Yaeyama, Senkaku): also pulled 2 degrees east.
    {"47", "47", 122.0, 126.0, 23.5, 26.5, {7.1, 13.4}},
    // Daito islands: also pulled 1.6 degrees west.
    {"47", "47", 130.5, 132.0, 24.0, 26.5, {3.5, 13.4}},
    // Ogasawara (Bonin, Volcano islands, Nishinoshima) -> next to the Izu islands.
    {"13421", "13", 139.0, 150.0, 22.0, 28.5, {-1.2, 3.9}},
};

LonLat Centre(const Inset& in) {
  return {(in.min_lon + in.max_lon) * 0.5, (in.min_lat + in.max_lat) * 0.5};
}

bool InBox(const Inset& in, const LonLat& p) {
  return p.lon >= in.min_lon && p.lon < in.max_lon && p.lat >= in.min_lat && p.lat < in.max_lat;
}

}  // namespace

glm::vec2 Projection::ProjectRaw(const LonLat& p) {
  const Lambert& l = Lcc();
  const double phi = p.lat * kPi / 180.0;
  const double rho = kEarthRadiusKm * l.f / std::pow(TanHalf(phi), l.n);
  const double theta = l.n * (p.lon - kLon0) * kPi / 180.0;
  return glm::vec2(static_cast<float>(rho * std::sin(theta)),
                   static_cast<float>(l.rho0 - rho * std::cos(theta)));
}

glm::vec2 Projection::ProjectWith(const LonLat& p, const Inset* inset) {
  const glm::vec2 raw = ProjectRaw(p);
  if (!inset) return raw;
  const LonLat c = Centre(*inset);
  const LonLat moved{c.lon + inset->offset.lon, c.lat + inset->offset.lat};
  return raw + (ProjectRaw(moved) - ProjectRaw(c));
}

glm::vec2 Projection::Project(const LonLat& p, std::string_view pref) {
  return ProjectWith(p, InsetFor(pref, p));
}

const Inset* Projection::InsetFor(std::string_view pref, const LonLat& centre) {
  for (const Inset& inset : kInsets) {
    if (pref == inset.pref && InBox(inset, centre)) return &inset;
  }
  return nullptr;
}

bool Projection::Dropped(const LonLat& centre) { return centre.lat < 22.0 || centre.lon > 150.0; }

const Inset* Projection::insets(int* count) {
  *count = static_cast<int>(sizeof(kInsets) / sizeof(kInsets[0]));
  return kInsets;
}

}  // namespace jpy::geo
