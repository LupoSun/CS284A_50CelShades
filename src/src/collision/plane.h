#ifndef COLLISIONOBJECT_PLANE_H
#define COLLISIONOBJECT_PLANE_H

#include <nanogui/nanogui.h>

#include "collisionObject.h"

using namespace nanogui;
using namespace CGL;
using namespace std;

struct Plane : public CollisionObject {
public:
  Plane(const Vector3D &point, const Vector3D &normal, double friction,
        double length)
      : Plane(point, normal, friction, length, length) {}

  Plane(const Vector3D &point, const Vector3D &normal, double friction,
        double width, double height)
      : point(point), normal(normal.unit()), friction(friction),
        width(width), height(height) {}

  void render(GLShader &shader) override;
  void collide(PointMass &pm) override;
  bool bounds(Vector3D &min_bound, Vector3D &max_bound) const override;

  Vector3D point;
  Vector3D normal;

  double friction;
  double width;
  double height;
};

#endif /* COLLISIONOBJECT_PLANE_H */
