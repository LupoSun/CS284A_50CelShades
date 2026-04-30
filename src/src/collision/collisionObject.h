#ifndef COLLISIONOBJECT
#define COLLISIONOBJECT

#include <nanogui/nanogui.h>

#include "../clothMesh.h"

using namespace CGL;
using namespace std;
using namespace nanogui;

class CollisionObject {
public:
  virtual ~CollisionObject() = default;

  virtual void render(GLShader &shader) = 0;
  virtual void collide(PointMass &pm) = 0;

  virtual bool isMesh() const { return false; }
  virtual bool hasMtl() const { return false; }
  virtual bool collisionEnabled() const { return true; }
  virtual void setCollisionEnabled(bool enabled) { (void)enabled; }

private:
  double friction;
};

#endif /* COLLISIONOBJECT */
