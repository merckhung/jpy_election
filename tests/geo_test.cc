#include <cmath>
#include <set>

#include "glm/geometric.hpp"
#include "gtest/gtest.h"
#include "src/election/model.h"
#include "src/geo/mesh_builder.h"
#include "src/geo/projection.h"
#include "src/geo/region_tree.h"
#include "src/geo/topojson.h"
#include "tests/test_root.h"

namespace jpy::geo {
namespace {

// A 2x1 quantized rectangle split into two unit squares that share the arc
// (1,0)-(1,1). Arc coordinates are delta-encoded.
constexpr char kTinyTopo[] = R"({
  "type": "Topology",
  "transform": {"scale": [1, 1], "translate": [0, 0]},
  "arcs": [
    [[1, 0], [0, 1]],
    [[1, 1], [-1, 0], [0, -1], [1, 0]],
    [[1, 0], [1, 0], [0, 1], [-1, 0]]
  ],
  "objects": {
    "prefectures": {"type": "GeometryCollection", "geometries": [
      {"type": "Polygon", "arcs": [[0, 1]], "properties": {"code": "A"}},
      {"type": "MultiPolygon", "arcs": [[[2, -1]]], "properties": {"code": "B"}}
    ]}
  }
})";

TEST(TopoJson, DecodesQuantizedDeltaArcs) {
  Topology topo;
  std::string error;
  ASSERT_TRUE(ParseTopoJson(kTinyTopo, {}, &topo, &error)) << error;
  const auto& prefs = topo.objects["prefectures"];
  ASSERT_EQ(prefs.size(), 2u);
  EXPECT_EQ(prefs[0].properties.at("code"), "A");
  // Square A: arc 0 (1,0)->(1,1) then arc 1 (1,1)->(0,1)->(0,0)->(1,0); the
  // junction point shared by consecutive arcs is not duplicated.
  const Ring& a = prefs[0].polygons[0].rings[0];
  ASSERT_EQ(a.size(), 5u);
  EXPECT_DOUBLE_EQ(a[1].lon, 1);
  EXPECT_DOUBLE_EQ(a[1].lat, 1);
  EXPECT_DOUBLE_EQ(a[2].lon, 0);
  EXPECT_DOUBLE_EQ(a[3].lat, 0);
  // Square B: arc 2 then arc 0 reversed (~0 == -1): (1,1)->(1,0).
  ASSERT_EQ(prefs[1].polygons.size(), 1u);
  const Ring& b = prefs[1].polygons[0].rings[0];
  ASSERT_EQ(b.size(), 5u);
  EXPECT_DOUBLE_EQ(b[2].lon, 2);
  EXPECT_DOUBLE_EQ(b[2].lat, 1);
  EXPECT_DOUBLE_EQ(b.back().lon, 1);
  EXPECT_DOUBLE_EQ(b.back().lat, 0);
}

TEST(TopoJson, RejectsGarbage) {
  Topology topo;
  std::string error;
  EXPECT_FALSE(ParseTopoJson("{not json", {}, &topo, &error));
  EXPECT_FALSE(ParseTopoJson(R"({"type":"FeatureCollection"})", {}, &topo, &error));
}

TEST(Projection, KilometreScaleAcrossJapan) {
  // Tokyo Station to Osaka Station is ~403 km great-circle; Sapporo to
  // Fukuoka ~1,420 km. The conformal conic keeps scale within ~1.5%.
  const glm::vec2 tokyo = Projection::ProjectRaw({139.767, 35.681});
  const glm::vec2 osaka = Projection::ProjectRaw({135.495, 34.702});
  EXPECT_NEAR(glm::length(tokyo - osaka), 403.0, 7.0);
  const glm::vec2 sapporo = Projection::ProjectRaw({141.351, 43.069});
  const glm::vec2 fukuoka = Projection::ProjectRaw({130.418, 33.590});
  EXPECT_NEAR(glm::length(sapporo - fukuoka), 1420.0, 25.0);
  // North is up, east is right.
  EXPECT_GT(sapporo.y, tokyo.y);
  EXPECT_GT(tokyo.x, osaka.x);
}

TEST(Projection, InsetsMoveOutlyingIslandsOnly) {
  const LonLat naha{127.68, 26.21};
  EXPECT_NE(Projection::Project(naha, "47"), Projection::ProjectRaw(naha));
  // Okinawa is drawn in the Sea of Japan, north-west of Honshu.
  EXPECT_GT(Projection::Project(naha, "47").y, Projection::ProjectRaw({135.0, 38.5}).y);
  const LonLat chichijima{142.19, 27.09};
  EXPECT_NE(Projection::Project(chichijima, "13"), Projection::ProjectRaw(chichijima));
  const LonLat hachijo{139.79, 33.11};  // Tokyo, kept in place
  EXPECT_EQ(Projection::Project(hachijo, "13"), Projection::ProjectRaw(hachijo));
  const LonLat tokyo{139.76, 35.68};
  EXPECT_EQ(Projection::Project(tokyo, "13"), Projection::ProjectRaw(tokyo));
  const LonLat amami{129.5, 28.3};  // Kagoshima, kept in place
  EXPECT_EQ(Projection::Project(amami, "46"), Projection::ProjectRaw(amami));
  EXPECT_TRUE(Projection::Dropped({153.98, 24.29}));  // Minami-Torishima
  EXPECT_TRUE(Projection::Dropped({136.08, 20.42}));  // Okinotorishima
  EXPECT_FALSE(Projection::Dropped(chichijima));
}

class JapanMapTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    std::string json;
    ASSERT_TRUE(election::ReadFile("data/map/japan.topo.json", &json));
    std::string error;
    tree_ = new RegionTree();
    ASSERT_TRUE(tree_->LoadTopoJson(json, &error)) << error;
  }
  static void TearDownTestSuite() { delete tree_; }
  static RegionTree* tree_;
};

RegionTree* JapanMapTest::tree_ = nullptr;

TEST_F(JapanMapTest, Hierarchy) {
  const Region& nation = tree_->nation();
  EXPECT_EQ(nation.code, "JP");
  EXPECT_EQ(nation.name_ja, "日本");
  EXPECT_EQ(nation.children.size(), 47u);
  int districts = 0, units = 0;
  for (int c : nation.children) {
    EXPECT_EQ(tree_->region(c).level, Level::kCounty);
    districts += static_cast<int>(tree_->region(c).children.size());
    for (int d : tree_->region(c).children) {
      EXPECT_EQ(tree_->region(d).level, Level::kTown);
      EXPECT_EQ(tree_->region(d).district_code, tree_->region(d).code);
      units += static_cast<int>(tree_->region(d).children.size());
      for (int u : tree_->region(d).children) {
        EXPECT_EQ(tree_->region(u).district_code, tree_->region(d).code);
        EXPECT_EQ(tree_->region(u).county_code, tree_->region(c).code);
      }
    }
  }
  EXPECT_EQ(districts, 289);
  EXPECT_GT(units, 1900);
}

TEST_F(JapanMapTest, NamesAndCodes) {
  const int tokyo = tree_->FindByCode("13");
  ASSERT_GE(tokyo, 0);
  EXPECT_EQ(tree_->region(tokyo).name_ja, "東京都");
  EXPECT_EQ(tree_->region(tokyo).name_en, "Tokyo");
  const int d = tree_->FindByCode("13-01");
  ASSERT_GE(d, 0);
  EXPECT_EQ(tree_->region(d).name_ja, "東京1区");
  EXPECT_EQ(tree_->region(d).level, Level::kTown);
  const int chiyoda = tree_->FindByCode("13101");
  ASSERT_GE(chiyoda, 0);
  EXPECT_EQ(tree_->region(chiyoda).name_ja, "千代田区");
  EXPECT_EQ(tree_->region(chiyoda).parent, d);
}

TEST_F(JapanMapTest, AreasArePlausible) {
  // Japan's land area is ~378,000 km^2 (the simplified outline loses a bit).
  EXPECT_NEAR(tree_->nation().area_km2, 375000, 20000) << tree_->nation().area_km2;
  const Region& tokyo = tree_->region(tree_->FindByCode("13"));
  EXPECT_NEAR(tokyo.area_km2, 2194, 150) << tokyo.area_km2;
  // Nation box: Hokkaido to Kyushu plus the insets, roughly 2,000 km square.
  const glm::vec2 size = tree_->nation().size();
  EXPECT_GT(size.x, 1500);
  EXPECT_LT(size.x, 2600);
  EXPECT_GT(size.y, 1500);
  EXPECT_LT(size.y, 2600);
}

TEST_F(JapanMapTest, PickingFindsTheRightRegion) {
  const glm::vec2 p = Projection::Project({139.7528, 35.6852}, "13");  // Imperial Palace
  const int pref = tree_->ChildAt(0, p);
  ASSERT_GE(pref, 0);
  EXPECT_EQ(tree_->region(pref).code, "13");
  const int district = tree_->ChildAt(pref, p);
  ASSERT_GE(district, 0);
  const int unit = tree_->ChildAt(district, p);
  ASSERT_GE(unit, 0);
  EXPECT_EQ(tree_->region(unit).code, "13101");
  EXPECT_EQ(tree_->region(district).code, "13-01");
  EXPECT_TRUE(RegionTree::Contains(tree_->region(unit), tree_->region(unit).label));
  EXPECT_EQ(tree_->AncestorAt(unit, Level::kCounty), pref);
  EXPECT_EQ(tree_->Path(unit).size(), 4u);
  // Naha, drawn in the Okinawa inset.
  const glm::vec2 naha = Projection::Project({127.68, 26.21}, "47");
  const int okinawa = tree_->ChildAt(0, naha);
  ASSERT_GE(okinawa, 0);
  EXPECT_EQ(tree_->region(okinawa).code, "47");
}

TEST_F(JapanMapTest, InsetsDoNotOverlapTheMainIslands) {
  const Region& okinawa = tree_->region(tree_->FindByCode("47"));
  for (int c : tree_->nation().children) {
    if (c == okinawa.id) continue;
    const Region& r = tree_->region(c);
    // Every prefecture's label point must not fall inside the Okinawa inset.
    EXPECT_FALSE(RegionTree::Contains(okinawa, r.label)) << r.code;
  }
}

TEST_F(JapanMapTest, LabelPointsAreInside) {
  int outside = 0;
  for (int c : tree_->nation().children) {
    for (int d : tree_->region(c).children) {
      if (!RegionTree::Contains(tree_->region(d), tree_->region(d).label)) ++outside;
    }
  }
  EXPECT_EQ(outside, 0);
}

TEST_F(JapanMapTest, MeshTopAreaMatchesPolygonArea) {
  const Region& pref = tree_->region(tree_->FindByCode("13"));
  const MapMesh mesh = BuildMapMesh(*tree_, pref.children);
  EXPECT_EQ(mesh.region_ids.size(), pref.children.size());
  double top_area = 0, area = 0;
  for (int d : pref.children) area += tree_->region(d).area_km2;
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const MapVertex& a = mesh.vertices[mesh.indices[i]];
    const MapVertex& b = mesh.vertices[mesh.indices[i + 1]];
    const MapVertex& c = mesh.vertices[mesh.indices[i + 2]];
    if (a.nz < 0.5f || b.nz < 0.5f || c.nz < 0.5f) continue;  // walls
    top_area += std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5;
  }
  EXPECT_NEAR(top_area, area, area * 0.01);
  EXPECT_FALSE(mesh.line_vertices.empty());
  EXPECT_EQ(mesh.line_vertices.size() % 2, 0u);
}

}  // namespace
}  // namespace jpy::geo
