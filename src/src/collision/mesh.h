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
    Mesh(const string& path, double friction = 0.0);

    void collide(PointMass& pm) override;
    void render(GLShader& shader) override;

    bool m_has_mtl = false;

    virtual bool isMesh() const override { return true; }
    virtual bool hasMtl() const override { return m_has_mtl; }

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

    struct UVCoord {
        double u;
        double v;

        UVCoord() : u(0.0), v(0.0) {}
        UVCoord(double u, double v) : u(u), v(v) {}
    };

    string path;
    double friction;

    vector<Vector3D> obj_positions;
    vector<Vector3D> obj_normals;
    vector<UVCoord> obj_uvs;
    vector<Triangle> obj_triangles;

    nanogui::MatrixXf positions;
    nanogui::MatrixXf normals;
    nanogui::MatrixXf uvs;

    int vertex_count;

    bool loadOBJ(const string& path);
    ObjIndex parseObjIndex(const string& token);
    void buildRenderBuffers();
    Vector3D computeFaceNormal(const Vector3D& a, const Vector3D& b, const Vector3D& c);

    unordered_map<string, Vector3D> material_colors;

    nanogui::MatrixXf colors;

    bool loadMTL(const string& mtl_path);
    string getDirectory(const string& filepath);
};

#endif