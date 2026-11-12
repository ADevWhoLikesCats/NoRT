#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>

struct ObjVertexIn {
    glm::vec3 pos;
    glm::vec3 nrm;
};

// Match main.cpp's Vertex layout exactly.
struct ObjVertexOut {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    float     metallic;
    float     roughness;
};

struct MeshData {
    std::vector<ObjVertexOut> verts;
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
    glm::vec3 center;   // (aabbMin + aabbMax) / 2
    float     radius;   // half the diagonal length
};

inline MeshData loadOBJ(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("failed to open OBJ: " + path);

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<ObjVertexOut> out;
    glm::vec3 bbMin( 1e30f);
    glm::vec3 bbMax(-1e30f);

    // Default material — no source in the OBJ.
    const glm::vec3 defaultColor(0.80f, 0.75f, 0.70f);
    const float defaultMetallic  = 0.0f;
    const float defaultRoughness = 0.60f;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        if (tok == "v") {
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
            bbMin = glm::min(bbMin, p);
            bbMax = glm::max(bbMax, p);
        } else if (tok == "vn") {
            glm::vec3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (tok == "f") {
            std::vector<std::pair<int,int>> face;  // (posIdx, nrmIdx), 0-based, -1 if absent
            std::string chunk;
            while (ss >> chunk) {
                int pi = -1, ni = -1;
                // Format: v, v//vn, v/vt/vn, v/vt
                size_t s1 = chunk.find('/');
                if (s1 == std::string::npos) {
                    pi = std::stoi(chunk) - 1;
                } else {
                    pi = std::stoi(chunk.substr(0, s1)) - 1;
                    size_t s2 = chunk.find('/', s1 + 1);
                    if (s2 != std::string::npos && s2 + 1 < chunk.size()) {
                        ni = std::stoi(chunk.substr(s2 + 1)) - 1;
                    }
                }
                face.push_back({pi, ni});
            }
            if (face.size() < 3) continue;
            // Fan triangulate: (0,1,2), (0,2,3), (0,3,4), ...
            for (size_t i = 1; i + 1 < face.size(); ++i) {
                for (int k = 0; k < 3; ++k) {
                    int idx = (k == 0) ? 0 : (int)i + k - 1;
                    auto [pi, ni] = face[idx];
                    ObjVertexOut v{};
                    v.pos      = positions.at(pi);
                    v.normal   = (ni >= 0 && ni < (int)normals.size()) ? normals[ni] : glm::vec3(0,1,0);
                    v.color    = defaultColor;
                    v.metallic = defaultMetallic;
                    v.roughness= defaultRoughness;
                    out.push_back(v);
                }
            }
        }
    }

    MeshData m;
    m.verts   = std::move(out);
    m.aabbMin = bbMin;
    m.aabbMax = bbMax;
    m.center  = (bbMin + bbMax) * 0.5f;
    m.radius  = glm::length(bbMax - bbMin) * 0.5f;

    std::cout << "loaded " << path << ": " << positions.size() << " verts, "
              << m.verts.size() << " tri-corner entries (" << m.verts.size() / 3 << " tris)\n";
    std::cout << "  bounds [" << bbMin.x << "," << bbMin.y << "," << bbMin.z << "] -> ["
              << bbMax.x << "," << bbMax.y << "," << bbMax.z << "]  radius=" << m.radius << "\n";
    return m;
}
