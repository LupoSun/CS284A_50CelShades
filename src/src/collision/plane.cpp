#include "iostream"
#include <cmath>
#include <nanogui/nanogui.h>

#include "../clothMesh.h"
#include "../clothSimulator.h"
#include "plane.h"

using namespace std;
using namespace CGL;

#define SURFACE_OFFSET 0.0001

static void computeTangents(const Vector3D &normal, Vector3D &u, Vector3D &v) {
  Vector3D arbitrary(1, 0, 0);
  if (fabs(dot(normal, arbitrary)) > 0.9)
    arbitrary = Vector3D(0, 0, 1);
  u = cross(normal, arbitrary);
  u.normalize();
  v = cross(normal, u);
  v.normalize();
}

void Plane::collide(PointMass &pm) {
    double last_side = dot(pm.last_position - point, normal);
    double curr_side = dot(pm.position - point, normal);

    // Only collide when the point mass has crossed through the plane
    if (last_side * curr_side <= 0) {
        Vector3D offset = pm.position - point;
        Vector3D u, v;
        computeTangents(normal, u, v);

        double proj_u = dot(offset, u);
        double proj_v = dot(offset, v);
        double half = length / 2.0;

        if (fabs(proj_u) > half || fabs(proj_v) > half)
            return;

        // Push back to the side the point came from
        double sign = (last_side >= 0) ? 1.0 : -1.0;
        Vector3D tangent_point = pm.position - normal * curr_side + normal * sign * SURFACE_OFFSET;
        Vector3D correction = tangent_point - pm.last_position;
        pm.position = pm.last_position + correction * (1 - friction);

        if (dot(pm.position - point, normal) * sign <= 0) {
            pm.position = tangent_point;
        }
    }
}

void Plane::render(GLShader &shader) {
  Vector3f sPoint(point.x, point.y, point.z);
  Vector3f sNormal(normal.x, normal.y, normal.z);
  Vector3f sParallel(1, 0, 0);
  if (fabs(sNormal.dot(sParallel)) > 0.9f)
    sParallel = Vector3f(0, 0, 1);
  Vector3f sCross = sNormal.cross(sParallel).normalized();
  sParallel = sCross.cross(sNormal).normalized();

  float half = (float)(length / 2.0);

  MatrixXf positions(3, 4);
  MatrixXf normals(3, 4);
  MatrixXf tangents(4, 4);

  positions.col(0) << sPoint + half * (sCross + sParallel);
  positions.col(1) << sPoint + half * (sCross - sParallel);
  positions.col(2) << sPoint + half * (-sCross + sParallel);
  positions.col(3) << sPoint + half * (-sCross - sParallel);

  normals.col(0) << sNormal;
  normals.col(1) << sNormal;
  normals.col(2) << sNormal;
  normals.col(3) << sNormal;

  for (int i = 0; i < 4; ++i) {
    tangents.col(i) << sParallel.x(), sParallel.y(), sParallel.z(), 0.0f;
  }

  float uvScale = (float)length;
  MatrixXf uvs(2, 4);
  uvs.col(0) << uvScale, uvScale;
  uvs.col(1) << uvScale, 0.0f;
  uvs.col(2) << 0.0f, uvScale;
  uvs.col(3) << 0.0f, 0.0f;

  shader.uploadAttrib("in_position", positions);
  if (shader.attrib("in_normal", false) != -1) {
    shader.uploadAttrib("in_normal", normals);
  }
  if (shader.attrib("in_uv", false) != -1) {
    shader.uploadAttrib("in_uv", uvs);
  }
  if (shader.attrib("in_tangent", false) != -1) {
    shader.uploadAttrib("in_tangent", tangents, false);
  }

  shader.drawArray(GL_TRIANGLE_STRIP, 0, 4);
}
