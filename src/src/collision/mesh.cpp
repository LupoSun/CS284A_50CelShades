#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <nanogui/nanogui.h>

#include "mesh.h"

using namespace nanogui;
using namespace CGL;
using namespace std;

namespace {

constexpr double kSurfaceOffset = 0.0001;
constexpr double kEpsilon = 1e-8;

string trim(const string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

string stripComment(const string& s) {
    size_t hash = s.find('#');
    return trim(hash == string::npos ? s : s.substr(0, hash));
}

int resolveObjIndex(int raw, int count) {
    if (raw > 0) return raw - 1;
    if (raw < 0) return count + raw;
    return -1;
}

double min3(double a, double b, double c) {
    return std::min(a, std::min(b, c));
}

double max3(double a, double b, double c) {
    return std::max(a, std::max(b, c));
}

Vector3D transformNormal(const Vector3D& n, const Vector3D& scale) {
    Vector3D out(
        fabs(scale.x) > kEpsilon ? n.x / scale.x : n.x,
        fabs(scale.y) > kEpsilon ? n.y / scale.y : n.y,
        fabs(scale.z) > kEpsilon ? n.z / scale.z : n.z);
    return out.norm() > kEpsilon ? out.unit() : Vector3D(0.0, 1.0, 0.0);
}

} // namespace

Mesh::Mesh(const string& path, double friction, const Vector3D& scale,
           const Vector3D& translate, bool collision_enabled)
    : path(path), friction(friction), scale(scale), translate(translate),
      m_collision_enabled(collision_enabled), vertex_count(0) {

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
        << " | collision: " << (m_collision_enabled ? "on" : "off")
        << endl;
}

void Mesh::collide(PointMass& pm) {
    if (!m_collision_enabled || collision_triangles.empty()) {
        return;
    }

    Vector3D start = pm.last_position;
    Vector3D end = pm.position;
    Vector3D motion = end - start;

    if (motion.norm() < kEpsilon) {
        return;
    }

    double best_t = std::numeric_limits<double>::infinity();
    const CachedTriangle* best_tri = nullptr;
    Vector3D best_hit;
    double best_sign = 1.0;

    for (const CachedTriangle& tri : collision_triangles) {
        if (!segmentIntersectsAABB(start, end, tri)) {
            continue;
        }

        double last_side = dot(start - tri.p0, tri.normal);
        double curr_side = dot(end - tri.p0, tri.normal);

        if (last_side * curr_side > 0.0) {
            continue;
        }

        double denom = dot(motion, tri.normal);
        if (fabs(denom) < kEpsilon) {
            continue;
        }

        double t = -last_side / denom;
        if (t < 0.0 || t > 1.0 || t >= best_t) {
            continue;
        }

        Vector3D hit = start + motion * t;
        if (!pointInTriangle(hit, tri)) {
            continue;
        }

        best_t = t;
        best_tri = &tri;
        best_hit = hit;
        best_sign = last_side >= 0.0 ? 1.0 : -1.0;
    }

    if (best_tri == nullptr) {
        return;
    }

    Vector3D target = best_hit + best_tri->normal * (best_sign * kSurfaceOffset);
    Vector3D correction = target - start;
    pm.position = start + correction * (1.0 - friction);

    if (dot(pm.position - best_tri->p0, best_tri->normal) * best_sign <= 0.0) {
        pm.position = target;
    }
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

    if (shader.attrib("in_tangent", false) >= 0) {
        shader.uploadAttrib("in_tangent", tangents);
    }

    if (shader.attrib("in_color", false) >= 0) {
        shader.uploadAttrib("in_color", colors);
    }

    shader.drawArray(GL_TRIANGLES, 0, vertex_count);
}

bool Mesh::bounds(Vector3D& min_bound, Vector3D& max_bound) const {
    if (!m_has_bounds) {
        return false;
    }
    min_bound = m_bounds_min;
    max_bound = m_bounds_max;
    return true;
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
        line = stripComment(line);
        if (line.empty()) {
            continue;
        }

        stringstream ss(line);
        string type;
        ss >> type;

        if (type == "newmtl") {
            current_material = trim(line.substr(type.size()));
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

    m_has_mtl = false;
    material_colors.clear();

    string obj_dir = getDirectory(path);
    string current_material = "";
    string line;

    while (getline(file, line)) {
        line = stripComment(line);
        if (line.empty()) {
            continue;
        }

        stringstream ss(line);
        string type;
        ss >> type;

        if (type == "mtllib") {
            string mtl_name = trim(line.substr(type.size()));
            string mtl_path = obj_dir + mtl_name;

            if (!mtl_name.empty() && loadMTL(mtl_path)) {
                m_has_mtl = true;
            }
        }
        else if (type == "usemtl") {
            current_material = trim(line.substr(type.size()));
        }
        else if (type == "v") {
            double x, y, z;
            ss >> x >> y >> z;
            obj_positions.push_back(Vector3D(
                x * scale.x + translate.x,
                y * scale.y + translate.y,
                z * scale.z + translate.z));
        }
        else if (type == "vn") {
            double x, y, z;
            ss >> x >> y >> z;
            obj_normals.push_back(transformNormal(Vector3D(x, y, z), scale));
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

    try {
        if (parts.size() >= 1 && !parts[0].empty()) {
            index.v = resolveObjIndex(stoi(parts[0]), (int)obj_positions.size());
        }

        if (parts.size() >= 2 && !parts[1].empty()) {
            index.vt = resolveObjIndex(stoi(parts[1]), (int)obj_uvs.size());
        }

        if (parts.size() >= 3 && !parts[2].empty()) {
            index.vn = resolveObjIndex(stoi(parts[2]), (int)obj_normals.size());
        }
    } catch (const std::exception&) {
        return ObjIndex();
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

    if (n.norm() < kEpsilon) {
        return Vector3D(0.0, 1.0, 0.0);
    }

    return n.unit();
}

bool Mesh::pointInTriangle(const Vector3D& p, const CachedTriangle& tri) const {
    Vector3D v0 = tri.p1 - tri.p0;
    Vector3D v1 = tri.p2 - tri.p0;
    Vector3D v2 = p - tri.p0;

    double d00 = dot(v0, v0);
    double d01 = dot(v0, v1);
    double d11 = dot(v1, v1);
    double d20 = dot(v2, v0);
    double d21 = dot(v2, v1);
    double denom = d00 * d11 - d01 * d01;

    if (fabs(denom) < kEpsilon) {
        return false;
    }

    double v = (d11 * d20 - d01 * d21) / denom;
    double w = (d00 * d21 - d01 * d20) / denom;
    double u = 1.0 - v - w;
    const double eps = -1e-5;

    return u >= eps && v >= eps && w >= eps;
}

bool Mesh::segmentIntersectsAABB(
    const Vector3D& a,
    const Vector3D& b,
    const CachedTriangle& tri) const {

    Vector3D seg_min(
        std::min(a.x, b.x) - kSurfaceOffset,
        std::min(a.y, b.y) - kSurfaceOffset,
        std::min(a.z, b.z) - kSurfaceOffset);
    Vector3D seg_max(
        std::max(a.x, b.x) + kSurfaceOffset,
        std::max(a.y, b.y) + kSurfaceOffset,
        std::max(a.z, b.z) + kSurfaceOffset);

    return seg_max.x >= tri.bbox_min.x && seg_min.x <= tri.bbox_max.x &&
           seg_max.y >= tri.bbox_min.y && seg_min.y <= tri.bbox_max.y &&
           seg_max.z >= tri.bbox_min.z && seg_min.z <= tri.bbox_max.z;
}

void Mesh::buildRenderBuffers() {
    vertex_count = (int)obj_triangles.size() * 3;
    m_has_bounds = false;

    positions = MatrixXf(4, vertex_count);
    normals = MatrixXf(4, vertex_count);
    uvs = MatrixXf(2, vertex_count);
    tangents = MatrixXf(4, vertex_count);
    colors = MatrixXf(4, vertex_count);
    collision_triangles.clear();

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

        Vector3D tri_min(
            min3(p0.x, p1.x, p2.x),
            min3(p0.y, p1.y, p2.y),
            min3(p0.z, p1.z, p2.z));
        Vector3D tri_max(
            max3(p0.x, p1.x, p2.x),
            max3(p0.y, p1.y, p2.y),
            max3(p0.z, p1.z, p2.z));

        if (!m_has_bounds) {
            m_bounds_min = tri_min;
            m_bounds_max = tri_max;
            m_has_bounds = true;
        }
        else {
            m_bounds_min.x = std::min(m_bounds_min.x, tri_min.x);
            m_bounds_min.y = std::min(m_bounds_min.y, tri_min.y);
            m_bounds_min.z = std::min(m_bounds_min.z, tri_min.z);
            m_bounds_max.x = std::max(m_bounds_max.x, tri_max.x);
            m_bounds_max.y = std::max(m_bounds_max.y, tri_max.y);
            m_bounds_max.z = std::max(m_bounds_max.z, tri_max.z);
        }

        Vector3D face_normal = computeFaceNormal(p0, p1, p2);

        CachedTriangle cached;
        cached.p0 = p0;
        cached.p1 = p1;
        cached.p2 = p2;
        cached.normal = face_normal;
        cached.bbox_min = Vector3D(
            min3(p0.x, p1.x, p2.x) - kSurfaceOffset,
            min3(p0.y, p1.y, p2.y) - kSurfaceOffset,
            min3(p0.z, p1.z, p2.z) - kSurfaceOffset);
        cached.bbox_max = Vector3D(
            max3(p0.x, p1.x, p2.x) + kSurfaceOffset,
            max3(p0.y, p1.y, p2.y) + kSurfaceOffset,
            max3(p0.z, p1.z, p2.z) + kSurfaceOffset);
        collision_triangles.push_back(cached);

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

                if (n.norm() > kEpsilon) {
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

            tangents(0, col) = 1.0f;
            tangents(1, col) = 0.0f;
            tangents(2, col) = 0.0f;
            tangents(3, col) = 0.0f;

            colors(0, col) = (float)mat_color.x;
            colors(1, col) = (float)mat_color.y;
            colors(2, col) = (float)mat_color.z;
            colors(3, col) = 1.0f;

            col++;
        }
    }

    vertex_count = col;
}
