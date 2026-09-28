// Map projection: lon/lat -> local planar kilometres (x east, y north).
//
// Japan spans ~26 degrees of longitude and ~22 of latitude, so a single
// equirectangular projection would stretch Hokkaido and squash Okinawa. We
// use a spherical Lambert conformal conic projection (standard parallels
// 30N/44N, origin 137E 37N), the usual choice for maps of the whole country.
//
// Like most Japanese election maps, the far-flung islands are drawn as
// insets moved closer to the main islands:
//  * Okinawa Prefecture is moved as a block into the empty Sea of Japan
//    north-west of Honshu; within the block the Sakishima islands and the
//    Daito islands are pulled towards Okinawa Island.
//  * The Ogasawara islands of Ogasawara Village (south of 28N) are moved up
//    next to the Izu islands.
//  * Remote specks (Okinotorishima, Minami-Torishima: lat < 22 or lon > 150)
//    are dropped; they would only blow up the national bounding box.
// Offsets apply to whole polygons (decided by the polygon's bounding-box
// centre) so no polygon is ever torn apart.
#pragma once

#include <string_view>

#include "glm/vec2.hpp"
#include "src/geo/topojson.h"

namespace jpy::geo {

struct Inset {
  const char* code;     // region framed as an inset ("47" Okinawa, "13421" Ogasawara)
  const char* pref;     // prefecture whose polygons may move
  double min_lon, max_lon, min_lat, max_lat;  // polygons centred in this box move
  LonLat offset;        // degrees added to every point of such a polygon
};

class Projection {
 public:
  static constexpr double kLon0 = 137.0;
  static constexpr double kLat0 = 37.0;

  // Projects a point of prefecture `pref` (insets are chosen by the point).
  static glm::vec2 Project(const LonLat& p, std::string_view pref);

  // Plain projection without inset offsets.
  static glm::vec2 ProjectRaw(const LonLat& p);

  // Projects `p` shifted by `inset` (nullptr = no shift).
  static glm::vec2 ProjectWith(const LonLat& p, const Inset* inset);

  // The inset that moves a polygon of `pref` centred at `centre`, or nullptr.
  static const Inset* InsetFor(std::string_view pref, const LonLat& centre);

  // True for polygons that are not drawn at all (remote specks).
  static bool Dropped(const LonLat& centre);

  // All inset rules (several rules may share one framed `code`).
  static const Inset* insets(int* count);
};

}  // namespace jpy::geo
