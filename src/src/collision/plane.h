#ifndef COLLISIONOBJECT_PLANE_H
#define COLLISIONOBJECT_PLANE_H

#include <nanogui/nanogui.h>

#include "../clothMesh.h"
#include "collisionObject.h"

using namespace nanogui;
using namespace CGL;
using namespace std;

struct Plane : public CollisionObject {
public:
  Plane(const Vector3D &point, const Vector3D &normal, double friction,
        double length)
      : point(point), normal(normal.unit()), friction(friction),
        length(length) {}

  void render(GLShader &shader) override;
  void collide(PointMass &pm) override;
  bool bounds(Vector3D &min_bound, Vector3D &max_bound) const override;

  Vector3D point;
  Vector3D normal;

  double friction;
  double length;
};

#endif /* COLLISIONOBJECT_PLANE_H */
