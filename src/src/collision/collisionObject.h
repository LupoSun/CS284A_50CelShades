#ifndef COLLISIONOBJECT
#define COLLISIONOBJECT

#include <nanogui/nanogui.h>

#include "../clothMesh.h"

using namespace CGL;
using namespace std;
using namespace nanogui;

class CollisionObject {
public:
  virtual void render(GLShader &shader) = 0;
  virtual void collide(PointMass &pm) = 0;

  virtual bool isMesh() const { return false; }
  virtual bool hasMtl() const { return false; }

private:
  double friction;
};

#endif /* COLLISIONOBJECT */
