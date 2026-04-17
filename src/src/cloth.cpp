#include <iostream>
#include <math.h>
#include <random>
#include <vector>

#include "cloth.h"
#include "collision/plane.h"
#include "collision/sphere.h"

using namespace std;

Cloth::Cloth(double width, double height, int num_width_points,
             int num_height_points, float thickness) {
  this->width = width;
  this->height = height;
  this->num_width_points = num_width_points;
  this->num_height_points = num_height_points;
  this->thickness = thickness;

  buildGrid();
  buildClothMesh();
}

Cloth::~Cloth() {
  point_masses.clear();
  springs.clear();

  if (clothMesh) {
    delete clothMesh;
  }
}

void Cloth::buildGrid() {
  // TODO (Part 1): Build a grid of masses and springs.
    // SHAOYI start
    point_masses.clear();
    springs.clear();

    double dx = width / (num_width_points - 1.0);
    double dy = height / (num_height_points - 1.0);

    // step 1: point_masses (row-major order)
    // reminder: PointMass(Vector3D position, bool pinned)
    for (int y = 0; y < num_height_points; y++) {
        for (int x = 0; x < num_width_points; x++) {

            // set up pin
            bool is_pinned = false;

            if (y < pinned.size()) {
                for (int px : pinned[y]) {
                    if (px == x) {
                        is_pinned = true;
                        break;
                    }
                }
            }

            // set up position
            Vector3D pos;
            if (orientation == HORIZONTAL) {
                pos = Vector3D(x * dx, 1.0, y * dy);
            }
            else {
                double z_offset = ((double)rand() / RAND_MAX) * 2.0 / 1000.0 - 1.0 / 1000.0; // random numbers [-1/1000, 1/1000]
                pos = Vector3D(x * dx, y * dy, z_offset);
            }

            point_masses.emplace_back(pos, is_pinned); // construct the point_mass and put it into point_masses

        }

    }

    // step 2: springs
    // reminder: Spring(PointMass *a, PointMass *b, e_spring_type spring_type)
    auto index = [this](int x, int y) { // a helper for getting point index
        return y * num_width_points + x;
        };
    for (int y = 0; y < num_height_points; y++) {
        for (int x = 0; x < num_width_points; x++) {

            int curr = index(x, y);

            // structural
            if (x > 0) {
                springs.emplace_back(&point_masses[curr], &point_masses[index(x - 1, y)], STRUCTURAL);
            }
            if (y > 0) {
                springs.emplace_back(&point_masses[curr], &point_masses[index(x, y - 1)], STRUCTURAL);
            }

            // shearing
            if (x > 0 && y > 0) {
                springs.emplace_back(&point_masses[curr], &point_masses[index(x - 1, y - 1)], SHEARING);
            }
            if (x < num_width_points - 1 && y > 0) {
                springs.emplace_back(&point_masses[curr], &point_masses[index(x + 1, y - 1)], SHEARING);
            }

            // bending
            if (x > 1) {
                springs.emplace_back(&point_masses[curr], &point_masses[index(x - 2, y)], BENDING);
            }
            if (y > 1) {
                springs.emplace_back(&point_masses[curr], &point_masses[index(x, y - 2)], BENDING);
            }
        }
    }
    // SHAOYI end

}

void Cloth::simulate(double frames_per_sec, double simulation_steps, ClothParameters *cp,
                     vector<Vector3D> external_accelerations,
                     vector<CollisionObject *> *collision_objects) {
  double mass = width * height * cp->density / num_width_points / num_height_points;
  double delta_t = 1.0f / frames_per_sec / simulation_steps;

  // TODO (Part 2): Compute total force acting on each point mass.
  // SHAOYI start
  //// step 1: compute and add external force
  Vector3D total_external_acceleration(0, 0, 0); // reset

  for (const auto& a : external_accelerations) {
      total_external_acceleration += a;
  }

  Vector3D external_force = total_external_acceleration * mass;

  for (auto& pm : point_masses) {
      pm.forces = Vector3D(0, 0, 0);
      pm.forces += external_force;
  }

  //// step 2: add spring correction forces to each spring with hook's law
  for (const auto& s : springs) {

      // check disabled
      bool enabled = false;
      if (s.spring_type == STRUCTURAL && cp->enable_structural_constraints) {
          enabled = true;
      }
      else if (s.spring_type == SHEARING && cp->enable_shearing_constraints) {
          enabled = true;
      }
      else if (s.spring_type == BENDING && cp->enable_bending_constraints) {
          enabled = true;
      }

      if (enabled) {
          // calculate length
          Vector3D delta = s.pm_b->position - s.pm_a->position; // distance between points
          double current_length = delta.norm();
          if (current_length == 0) continue;
          double extension = current_length - s.rest_length;

          double ks = cp->ks;
          if (s.spring_type == BENDING) {
              ks *= 0.2;
          }
          
          Vector3D force = delta / current_length * (ks * extension);

          // Equal and opposite forces
          s.pm_a->forces += force;
          s.pm_b->forces -= force;
      }

      // SHAOYI end
  }


  // TODO (Part 2): Use Verlet integration to compute new point mass positions
  // SHAOYI start
  // simply use the given equation to calculate the new position
  double damping_factor = 1.0 - cp->damping / 100.0;

  for (auto& pm : point_masses) {
      if (pm.pinned) continue;

      Vector3D current_position = pm.position;
      Vector3D acceleration = pm.forces / mass;

      pm.position = pm.position
          + (pm.position - pm.last_position) * damping_factor
          + acceleration * delta_t * delta_t;

      pm.last_position = current_position;

      // TODO (Part 3): Handle collisions with other primitives.
      for (CollisionObject* obj : *collision_objects) {
          obj->collide(pm);
      }
  }
  // SHAOYI end

  // TODO (Part 4): Handle self-collisions.
  // TAO START
  build_spatial_map();
  for (PointMass &pm : point_masses) {
    self_collide(pm, simulation_steps);
  }
  // TAO END




  // TODO (Part 2): Constrain the changes to be such that the spring does not change
  // in length more than 10% per timestep [Provot 1995].
  // SHAOYI start
  for (const auto& s : springs) {
      // calculate current distance and compare it with max_length
      Vector3D delta = s.pm_b->position - s.pm_a->position; // a vector
      double current_length = delta.norm(); // a scalar
      double max_length = 1.1 * s.rest_length;

      if (current_length <= max_length || current_length == 0) continue;


      Vector3D correction = delta / current_length * (current_length - max_length); // unit vector * correction length

      bool a_pinned = s.pm_a->pinned;
      bool b_pinned = s.pm_b->pinned;

      if (!a_pinned && !b_pinned) {
          s.pm_a->position += correction * 0.5;
          s.pm_b->position -= correction * 0.5;
      }
      else if (a_pinned && !b_pinned) {
          s.pm_b->position -= correction;
      }
      else if (!a_pinned && b_pinned) {
          s.pm_a->position += correction;
      }
      // if both pinned: do nothing
  }
  // SHAOYI end

}

void Cloth::build_spatial_map() {
  for (const auto &entry : map) {
    delete(entry.second);
  }
  map.clear();

  // TODO (Part 4): Build a spatial map out of all of the point masses.
  // TAO START
  for (PointMass &pm : point_masses) {
    long long key = hash_position(pm.position);
    if (map.find(key) == map.end()) {
      map[key] = new vector<PointMass *>();
    }
    map[key]->push_back(&pm);
  }
  // TAO END
}

void Cloth::self_collide(PointMass &pm, double simulation_steps) {
  // TODO (Part 4): Handle self-collision for a given point mass.
  // TAO START
  if (pm.pinned) return;

  long long key = hash_position(pm.position);

  if (map.find(key) == map.end()) return;

  vector<PointMass *> *candidates = map[key];

  Vector3D total_correction(0, 0, 0);
  int num_corrections = 0;

  for (PointMass *other : *candidates) {
    if (other == &pm) continue;

    Vector3D delta = pm.position - other->position;
    double dist = delta.norm();

    if (dist < 2.0 * thickness) {
      if (dist == 0) continue; 

      Vector3D direction = delta / dist; 
      Vector3D correction = direction * (2.0 * thickness - dist);

      total_correction += correction;
      num_corrections++;
    }
  }
  if (num_corrections > 0) {
    Vector3D avg_correction = total_correction / num_corrections / simulation_steps;
    pm.position += avg_correction;
  }
  // TAO END
}

long long Cloth::hash_position(Vector3D pos) {
  // TODO (Part 4): Hash a 3D position into a unique float identifier that represents membership in some 3D box volume.
  // TAO START
  double w = 3.0 * width / num_width_points;
  double h = 3.0 * height / num_height_points;
  double t = max(w, h);

  long long xi = (long long) floor(pos.x / w);
  long long yi = (long long) floor(pos.y / h);
  long long zi = (long long) floor(pos.z / t);

  return (xi * 73856093ll) ^ (yi * 19349663ll) ^ (zi * 83492791ll);
  // TAO END
}

///////////////////////////////////////////////////////
/// YOU DO NOT NEED TO REFER TO ANY CODE BELOW THIS ///
///////////////////////////////////////////////////////

void Cloth::reset() {
  PointMass *pm = &point_masses[0];
  for (int i = 0; i < point_masses.size(); i++) {
    pm->position = pm->start_position;
    pm->last_position = pm->start_position;
    pm++;
  }
}

void Cloth::buildClothMesh() {
  if (point_masses.size() == 0) return;

  ClothMesh *clothMesh = new ClothMesh();
  vector<Triangle *> triangles;

  // Create vector of triangles
  for (int y = 0; y < num_height_points - 1; y++) {
    for (int x = 0; x < num_width_points - 1; x++) {
      PointMass *pm = &point_masses[y * num_width_points + x];
      // Get neighboring point masses:
      /*                      *
       * pm_A -------- pm_B   *
       *             /        *
       *  |         /   |     *
       *  |        /    |     *
       *  |       /     |     *
       *  |      /      |     *
       *  |     /       |     *
       *  |    /        |     *
       *      /               *
       * pm_C -------- pm_D   *
       *                      *
       */
      
      float u_min = x;
      u_min /= num_width_points - 1;
      float u_max = x + 1;
      u_max /= num_width_points - 1;
      float v_min = y;
      v_min /= num_height_points - 1;
      float v_max = y + 1;
      v_max /= num_height_points - 1;
      
      PointMass *pm_A = pm                       ;
      PointMass *pm_B = pm                    + 1;
      PointMass *pm_C = pm + num_width_points    ;
      PointMass *pm_D = pm + num_width_points + 1;
      
      Vector3D uv_A = Vector3D(u_min, v_min, 0);
      Vector3D uv_B = Vector3D(u_max, v_min, 0);
      Vector3D uv_C = Vector3D(u_min, v_max, 0);
      Vector3D uv_D = Vector3D(u_max, v_max, 0);
      
      
      // Both triangles defined by vertices in counter-clockwise orientation
      triangles.push_back(new Triangle(pm_A, pm_C, pm_B, 
                                       uv_A, uv_C, uv_B));
      triangles.push_back(new Triangle(pm_B, pm_C, pm_D, 
                                       uv_B, uv_C, uv_D));
    }
  }

  // For each triangle in row-order, create 3 edges and 3 internal halfedges
  for (int i = 0; i < triangles.size(); i++) {
    Triangle *t = triangles[i];

    // Allocate new halfedges on heap
    Halfedge *h1 = new Halfedge();
    Halfedge *h2 = new Halfedge();
    Halfedge *h3 = new Halfedge();

    // Allocate new edges on heap
    Edge *e1 = new Edge();
    Edge *e2 = new Edge();
    Edge *e3 = new Edge();

    // Assign a halfedge pointer to the triangle
    t->halfedge = h1;

    // Assign halfedge pointers to point masses
    t->pm1->halfedge = h1;
    t->pm2->halfedge = h2;
    t->pm3->halfedge = h3;

    // Update all halfedge pointers
    h1->edge = e1;
    h1->next = h2;
    h1->pm = t->pm1;
    h1->triangle = t;

    h2->edge = e2;
    h2->next = h3;
    h2->pm = t->pm2;
    h2->triangle = t;

    h3->edge = e3;
    h3->next = h1;
    h3->pm = t->pm3;
    h3->triangle = t;
  }

  // Go back through the cloth mesh and link triangles together using halfedge
  // twin pointers

  // Convenient variables for math
  int num_height_tris = (num_height_points - 1) * 2;
  int num_width_tris = (num_width_points - 1) * 2;

  bool topLeft = true;
  for (int i = 0; i < triangles.size(); i++) {
    Triangle *t = triangles[i];

    if (topLeft) {
      // Get left triangle, if it exists
      if (i % num_width_tris != 0) { // Not a left-most triangle
        Triangle *temp = triangles[i - 1];
        t->pm1->halfedge->twin = temp->pm3->halfedge;
      } else {
        t->pm1->halfedge->twin = nullptr;
      }

      // Get triangle above, if it exists
      if (i >= num_width_tris) { // Not a top-most triangle
        Triangle *temp = triangles[i - num_width_tris + 1];
        t->pm3->halfedge->twin = temp->pm2->halfedge;
      } else {
        t->pm3->halfedge->twin = nullptr;
      }

      // Get triangle to bottom right; guaranteed to exist
      Triangle *temp = triangles[i + 1];
      t->pm2->halfedge->twin = temp->pm1->halfedge;
    } else {
      // Get right triangle, if it exists
      if (i % num_width_tris != num_width_tris - 1) { // Not a right-most triangle
        Triangle *temp = triangles[i + 1];
        t->pm3->halfedge->twin = temp->pm1->halfedge;
      } else {
        t->pm3->halfedge->twin = nullptr;
      }

      // Get triangle below, if it exists
      if (i + num_width_tris - 1 < 1.0f * num_width_tris * num_height_tris / 2.0f) { // Not a bottom-most triangle
        Triangle *temp = triangles[i + num_width_tris - 1];
        t->pm2->halfedge->twin = temp->pm3->halfedge;
      } else {
        t->pm2->halfedge->twin = nullptr;
      }

      // Get triangle to top left; guaranteed to exist
      Triangle *temp = triangles[i - 1];
      t->pm1->halfedge->twin = temp->pm2->halfedge;
    }

    topLeft = !topLeft;
  }

  clothMesh->triangles = triangles;
  this->clothMesh = clothMesh;
}
