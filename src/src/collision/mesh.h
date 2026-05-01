#ifndef MESH_H
#define MESH_H

#include <string>
#include <vector>
#include <nanogui/nanogui.h>
#include <unordered_map>

#include "collisionObject.h"
#include "CGL/CGL.h"

using namespace CGL;
using namespace std;

class Mesh : public CollisionObject {
public:
    Mesh(const string& path, double friction = 0.0,
         const Vector3D& scale = Vector3D(1.0, 1.0, 1.0),
         const Vector3D& translate = Vector3D(0.0, 0.0, 0.0),
         bool collision_enabled = false);

    void collide(PointMass& pm) override;
    void render(GLShader& shader) override;
    bool loaded() const { return vertex_count > 0; }

    bool m_has_mtl = false;

    virtual bool isMesh() const override { return true; }
    virtual bool hasMtl() const override { return m_has_mtl; }
    virtual bool collisionEnabled() const override { return m_collision_enabled; }
    virtual void setCollisionEnabled(bool enabled) override { m_collision_enabled = enabled; }
    virtual bool bounds(Vector3D& min_bound, Vector3D& max_bound) const override;

private:
    struct ObjIndex {
        int v;
        int vt;
        int vn;

        ObjIndex() : v(-1), vt(-1), vn(-1) {}
    };

    struct Triangle {
        ObjIndex a;
        ObjIndex b;
        ObjIndex c;
        string material_name;
    };

    struct CachedTriangle {
        Vector3D p0;
        Vector3D p1;
        Vector3D p2;
        Vector3D normal;
        Vector3D bbox_min;
        Vector3D bbox_max;
    };

    struct UVCoord {
        double u;
        double v;

        UVCoord() : u(0.0), v(0.0) {}
        UVCoord(double u, double v) : u(u), v(v) {}
    };

    string path;
    double friction;
    Vector3D scale;
    Vector3D translate;
    bool m_collision_enabled;
    bool m_has_bounds = false;
    Vector3D m_bounds_min;
    Vector3D m_bounds_max;

    vector<Vector3D> obj_positions;
    vector<Vector3D> obj_normals;
    vector<UVCoord> obj_uvs;
    vector<Triangle> obj_triangles;
    vector<CachedTriangle> collision_triangles;

    nanogui::MatrixXf positions;
    nanogui::MatrixXf normals;
    nanogui::MatrixXf uvs;
    nanogui::MatrixXf tangents;

    int vertex_count;

    bool loadOBJ(const string& path);
    ObjIndex parseObjIndex(const string& token);
    void buildRenderBuffers();
    Vector3D computeFaceNormal(const Vector3D& a, const Vector3D& b, const Vector3D& c);
    bool pointInTriangle(const Vector3D& p, const CachedTriangle& tri) const;
    bool segmentIntersectsAABB(const Vector3D& a, const Vector3D& b, const CachedTriangle& tri) const;

    unordered_map<string, Vector3D> material_colors;

    nanogui::MatrixXf colors;

    bool loadMTL(const string& mtl_path);
    string getDirectory(const string& filepath);
};

#endif
