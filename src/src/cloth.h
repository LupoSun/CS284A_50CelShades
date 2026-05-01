#ifndef CLOTH_H
#define CLOTH_H

#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "CGL/CGL.h"
#include "CGL/misc.h"
#include "clothMesh.h"
#include "collision/collisionObject.h"
#include "spring.h"

using namespace CGL;
using namespace std;

enum e_orientation { HORIZONTAL = 0, VERTICAL = 1 };

struct ClothParameters {
  ClothParameters() {}
  ClothParameters(bool enable_structural_constraints,
                  bool enable_shearing_constraints,
                  bool enable_bending_constraints, double damping,
                  double density, double ks)
      : enable_structural_constraints(enable_structural_constraints),
        enable_shearing_constraints(enable_shearing_constraints),
        enable_bending_constraints(enable_bending_constraints),
        damping(damping), density(density), ks(ks) {}
  ~ClothParameters() {}

  // Global simulation parameters

  bool enable_structural_constraints = true;
  bool enable_shearing_constraints = true;
  bool enable_bending_constraints = true;

  double damping = 0.2;

  // Mass-spring parameters
  double density = 15.0;
  double ks = 5000.0;
};

struct Cloth {
  Cloth() {}
  Cloth(double width, double height, int num_width_points,
        int num_height_points, float thickness);
  ~Cloth();

  void buildGrid();

  void simulate(double frames_per_sec, double simulation_steps, ClothParameters *cp,
                vector<Vector3D> external_accelerations,
                vector<CollisionObject *> *collision_objects);

  void reset();
  void buildClothMesh();

  void build_spatial_map();
  void self_collide(PointMass &pm, double simulation_steps);
  long long hash_position(Vector3D pos);

  // Cloth properties
  double width = 0.0;
  double height = 0.0;
  int num_width_points = 0;
  int num_height_points = 0;
  double thickness = 0.0;
  e_orientation orientation = HORIZONTAL;

  // Cloth components
  vector<PointMass> point_masses;
  vector<vector<int>> pinned;
  vector<Spring> springs;
  ClothMesh *clothMesh = nullptr;

  // Spatial hashing
  unordered_map<long long, vector<PointMass *> *> map;
};

#endif /* CLOTH_H */
