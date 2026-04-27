#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <nanogui/nanogui.h>

#include "mesh.h"

using namespace nanogui;
using namespace CGL;
using namespace std;

Mesh::Mesh(const string& path, double friction)
    : path(path), friction(friction), vertex_count(0) {

    bool success = loadOBJ(path);

    if (!success) {
        cerr << "[Mesh] Failed to load OBJ: " << path << endl;
        return;
    }

    buildRenderBuffers();

    cout << "[Mesh] Loaded OBJ: " << path
        << " | vertices: " << obj_positions.size()
        << " | triangles: " << obj_triangles.size()
        << " | materials: " << material_colors.size()
        << endl;
}

void Mesh::collide(PointMass& pm) {
    // Display only for now. No collision.
    return;
}

void Mesh::render(GLShader& shader) {
    if (vertex_count <= 0) {
        return;
    }

    if (shader.attrib("in_position", false) >= 0) {
        shader.uploadAttrib("in_position", positions);
    }

    if (shader.attrib("in_normal", false) >= 0) {
        shader.uploadAttrib("in_normal", normals);
    }

    if (shader.attrib("in_uv", false) >= 0) {
        shader.uploadAttrib("in_uv", uvs);
    }

    if (shader.attrib("in_color", false) >= 0) {
        shader.uploadAttrib("in_color", colors);
    }

    shader.drawArray(GL_TRIANGLES, 0, vertex_count);
}

string Mesh::getDirectory(const string& filepath) {
    size_t slash_pos = filepath.find_last_of("/\\");

    if (slash_pos == string::npos) {
        return "";
    }

    return filepath.substr(0, slash_pos + 1);
}

bool Mesh::loadMTL(const string& mtl_path) {
    ifstream file(mtl_path);

    if (!file.good()) {
        cerr << "[Mesh] Cannot open MTL file: " << mtl_path << endl;
        return false;
    }

    string line;
    string current_material;

    while (getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        if (line[0] == '#') {
            continue;
        }

        stringstream ss(line);
        string type;
        ss >> type;

        if (type == "newmtl") {
            ss >> current_material;
        }
        else if (type == "Kd") {
            double r, g, b;
            ss >> r >> g >> b;

            if (!current_material.empty()) {
                material_colors[current_material] = Vector3D(r, g, b);
            }
        }
    }

    file.close();

    cout << "[Mesh] Loaded MTL: " << mtl_path
        << " | material colors: " << material_colors.size()
        << endl;

    return true;
}

bool Mesh::loadOBJ(const string& path) {
    ifstream file(path);

    if (!file.good()) {
        cerr << "[Mesh] Cannot open file: " << path << endl;
        return false;
    }

    // Reset MTL state for this OBJ
    m_has_mtl = false;

    string obj_dir = getDirectory(path);
    string current_material = "";

    string line;

    while (getline(file, line)) {
        if (line.empty()) {
            continue;
        }

        if (line[0] == '#') {
            continue;
        }

        stringstream ss(line);
        string type;
        ss >> type;

        if (type == "mtllib") {
            string mtl_name;
            ss >> mtl_name;

            string mtl_path = obj_dir + mtl_name;

            // Only mark as true if MTL was loaded successfully
            if (loadMTL(mtl_path)) {
                m_has_mtl = true;
            }
        }
        else if (type == "usemtl") {
            ss >> current_material;
        }
        else if (type == "v") {
            double x, y, z;
            ss >> x >> y >> z;
            obj_positions.push_back(Vector3D(x, y, z));
        }
        else if (type == "vn") {
            double x, y, z;
            ss >> x >> y >> z;

            Vector3D n(x, y, z);
            if (n.norm() > 1e-8) {
                n = n.unit();
            }

            obj_normals.push_back(n);
        }
        else if (type == "vt") {
            double u, v;
            ss >> u >> v;
            obj_uvs.push_back(UVCoord(u, v));
        }
        else if (type == "f") {
            vector<ObjIndex> face_indices;
            string token;

            while (ss >> token) {
                face_indices.push_back(parseObjIndex(token));
            }

            // Triangulate polygon face by fan:
            // f 1 2 3 4 -> (1,2,3), (1,3,4)
            if (face_indices.size() >= 3) {
                for (int i = 1; i + 1 < (int)face_indices.size(); i++) {
                    Triangle tri;
                    tri.a = face_indices[0];
                    tri.b = face_indices[i];
                    tri.c = face_indices[i + 1];
                    tri.material_name = current_material;

                    obj_triangles.push_back(tri);
                }
            }
        }
    }

    file.close();

    return obj_positions.size() > 0 && obj_triangles.size() > 0;
}

Mesh::ObjIndex Mesh::parseObjIndex(const string& token) {
    ObjIndex index;

    vector<string> parts;
    string current;

    for (char ch : token) {
        if (ch == '/') {
            parts.push_back(current);
            current.clear();
        }
        else {
            current.push_back(ch);
        }
    }

    parts.push_back(current);

    // OBJ uses 1-based indices.
    // Supported formats:
    // v
    // v/vt
    // v//vn
    // v/vt/vn

    if (parts.size() >= 1 && !parts[0].empty()) {
        index.v = stoi(parts[0]) - 1;
    }

    if (parts.size() >= 2 && !parts[1].empty()) {
        index.vt = stoi(parts[1]) - 1;
    }

    if (parts.size() >= 3 && !parts[2].empty()) {
        index.vn = stoi(parts[2]) - 1;
    }

    return index;
}

Vector3D Mesh::computeFaceNormal(
    const Vector3D& a,
    const Vector3D& b,
    const Vector3D& c) {

    Vector3D e1 = b - a;
    Vector3D e2 = c - a;

    Vector3D n = cross(e1, e2);

    if (n.norm() < 1e-8) {
        return Vector3D(0.0, 1.0, 0.0);
    }

    return n.unit();
}

void Mesh::buildRenderBuffers() {
    vertex_count = (int)obj_triangles.size() * 3;

    positions = MatrixXf(4, vertex_count);
    normals = MatrixXf(4, vertex_count);
    uvs = MatrixXf(2, vertex_count);
    colors = MatrixXf(4, vertex_count);

    int col = 0;

    for (const Triangle& tri : obj_triangles) {
        ObjIndex ids[3] = { tri.a, tri.b, tri.c };

        if (tri.a.v < 0 || tri.b.v < 0 || tri.c.v < 0) {
            continue;
        }

        if (tri.a.v >= (int)obj_positions.size() ||
            tri.b.v >= (int)obj_positions.size() ||
            tri.c.v >= (int)obj_positions.size()) {
            continue;
        }

        Vector3D p0 = obj_positions[tri.a.v];
        Vector3D p1 = obj_positions[tri.b.v];
        Vector3D p2 = obj_positions[tri.c.v];

        Vector3D face_normal = computeFaceNormal(p0, p1, p2);

        Vector3D mat_color(0.8, 0.8, 0.8);

        auto it_mat = material_colors.find(tri.material_name);
        if (it_mat != material_colors.end()) {
            mat_color = it_mat->second;
        }

        for (int i = 0; i < 3; i++) {
            ObjIndex id = ids[i];

            Vector3D p = obj_positions[id.v];

            Vector3D n = face_normal;
            if (id.vn >= 0 && id.vn < (int)obj_normals.size()) {
                n = obj_normals[id.vn];

                if (n.norm() > 1e-8) {
                    n = n.unit();
                }
                else {
                    n = face_normal;
                }
            }

            UVCoord uv;
            if (id.vt >= 0 && id.vt < (int)obj_uvs.size()) {
                uv = obj_uvs[id.vt];
            }

            positions(0, col) = (float)p.x;
            positions(1, col) = (float)p.y;
            positions(2, col) = (float)p.z;
            positions(3, col) = 1.0f;

            normals(0, col) = (float)n.x;
            normals(1, col) = (float)n.y;
            normals(2, col) = (float)n.z;
            normals(3, col) = 0.0f;

            uvs(0, col) = (float)uv.u;
            uvs(1, col) = (float)uv.v;

            colors(0, col) = (float)mat_color.x;
            colors(1, col) = (float)mat_color.y;
            colors(2, col) = (float)mat_color.z;
            colors(3, col) = 1.0f;

            col++;
        }
    }

    vertex_count = col;
}