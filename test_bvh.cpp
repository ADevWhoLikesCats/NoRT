#include <functional>
// Standalone BVH correctness test. No Vulkan, no GPU.
// Builds a BVH from the car OBJ and validates structure.

#include <glm/glm.hpp>
#include <vector>
#include <iostream>
#include <cmath>
#include <algorithm>
#include "obj_loader.hpp"

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    float     metallic;
    float     roughness;
    glm::vec2 uv;
};

struct BVHNode {
    glm::vec4 aabbMin;
    glm::vec4 aabbMax;
    int32_t   leftOrStart;
    int32_t   rightOrCount;
};

std::vector<BVHNode> buildBVH(const std::vector<Vertex>& inVerts,
                              std::vector<Vertex>&       outVerts);

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cout << "FAIL: " << msg << "\n"; failures++; } \
} while(0)

bool aabbContains(glm::vec3 p, glm::vec4 bmin, glm::vec4 bmax, float eps = 1e-4f) {
    return p.x >= bmin.x - eps && p.x <= bmax.x + eps &&
           p.y >= bmin.y - eps && p.y <= bmax.y + eps &&
           p.z >= bmin.z - eps && p.z <= bmax.z + eps;
}

int depthOf(const std::vector<BVHNode>& nodes, int idx) {
    if (idx < 0 || idx >= (int)nodes.size()) return -1;
    const BVHNode& n = nodes[idx];
    if (n.rightOrCount < 0) return 0;
    int l = depthOf(nodes, n.leftOrStart);
    int r = depthOf(nodes, n.rightOrCount);
    return 1 + std::max(l, r);
}

int main() {
    auto mesh = loadOBJ("Lamborghini_Aventador.obj");
    std::vector<Vertex> verts;
    for (auto& o : mesh.verts) {
        Vertex v{};
        v.pos = o.pos; v.normal = o.normal; v.color = o.color;
        v.metallic = o.metallic; v.roughness = o.roughness; v.uv = o.uv;
        verts.push_back(v);
    }

    const size_t triCount = verts.size() / 3;
    std::cout << "Loaded " << triCount << " tris (" << verts.size() << " verts)\n";

    std::vector<Vertex> ordered;
    auto nodes = buildBVH(verts, ordered);

    int failures = 0;

    CHECK(ordered.size() == verts.size(), "permuted vertex count must match input");
    CHECK(nodes.size() > 1, "BVH must have more than root node for >4 tris");

    // 1. Every triangle appears in exactly one leaf
    std::vector<int> triInLeaf(triCount, 0);
    int leafCount = 0;
    for (size_t i = 0; i < nodes.size(); i++) {
        const auto& n = nodes[i];
        if (n.rightOrCount >= 0) continue;   // internal
        leafCount++;
        int start = n.leftOrStart;
        int count = -n.rightOrCount;
        CHECK(count >= 1, "leaf must have >=1 tri, node " << i);
        CHECK(start >= 0 && start + count <= (int)triCount, "leaf range out of bounds, node " << i);
        for (int t = start; t < start + count; t++) {
            triInLeaf[t]++;
        }
    }
    std::cout << "  Leaf count: " << leafCount << "\n";

    int unassigned = 0, multAssigned = 0;
    for (int i = 0; i < triCount; i++) {
        if (triInLeaf[i] == 0) unassigned++;
        if (triInLeaf[i] > 1)  multAssigned++;
    }
    CHECK(unassigned == 0, unassigned << " triangles not in any leaf");
    CHECK(multAssigned == 0, multAssigned << " triangles in multiple leaves");

    // 2. Every triangle inside its leaf AABB
    int outsideAABB = 0;
    for (size_t i = 0; i < nodes.size(); i++) {
        const auto& n = nodes[i];
        if (n.rightOrCount >= 0) continue;
        int start = n.leftOrStart, count = -n.rightOrCount;
        for (int t = start; t < start + count; t++) {
            for (int k = 0; k < 3; k++) {
                glm::vec3 p = ordered[t*3 + k].pos;
                if (!aabbContains(p, n.aabbMin, n.aabbMax)) {
                    outsideAABB++;
                    if (outsideAABB <= 5) {
                        std::cout << "  tri " << t << " vert " << k << " outside leaf AABB\n";
                        std::cout << "    p = (" << p.x << "," << p.y << "," << p.z << ")\n";
                        std::cout << "    bmin = (" << n.aabbMin.x << "," << n.aabbMin.y << "," << n.aabbMin.z << ")\n";
                        std::cout << "    bmax = (" << n.aabbMax.x << "," << n.aabbMax.y << "," << n.aabbMax.z << ")\n";
                    }
                }
            }
        }
    }
    CHECK(outsideAABB == 0, outsideAABB << " vertices outside their leaf AABB");

    // 3. Parent AABBs contain child AABBs
    int childrenOutside = 0;
    for (size_t i = 0; i < nodes.size(); i++) {
        const auto& n = nodes[i];
        if (n.rightOrCount < 0) continue;
        for (int c : {n.leftOrStart, n.rightOrCount}) {
            if (c < 0 || c >= (int)nodes.size()) { childrenOutside++; continue; }
            const auto& ch = nodes[c];
            bool ok = ch.aabbMin.x >= n.aabbMin.x && ch.aabbMin.y >= n.aabbMin.y && ch.aabbMin.z >= n.aabbMin.z &&
                      ch.aabbMax.x <= n.aabbMax.x && ch.aabbMax.y <= n.aabbMax.y && ch.aabbMax.z <= n.aabbMax.z;
            if (!ok) {
                childrenOutside++;
                if (childrenOutside <= 5) {
                    std::cout << "  node " << i << " does not contain child " << c << "\n";
                }
            }
        }
    }
    CHECK(childrenOutside == 0, childrenOutside << " children outside parent AABB");

    // 4. Depth
    int depth = depthOf(nodes, 0);
    std::cout << "  Depth: " << depth << "\n";
    CHECK(depth <= 64, "tree depth " << depth << " exceeds shader stack (64)");
    CHECK(depth >= 1,  "tree should have at least one split for " << triCount << " tris");

    // 5. Original triangles all represented (no geometry lost)
    // Build a hash of original triangle centroids, compare to permuted.
    auto centroid = [](glm::vec3 a, glm::vec3 b, glm::vec3 c) { return (a + b + c) / 3.0f; };
    std::vector<glm::vec3> origCentroids, permCentroids;
    for (size_t i = 0; i < triCount; i++) {
        origCentroids.push_back(centroid(verts[i*3].pos, verts[i*3+1].pos, verts[i*3+2].pos));
        permCentroids.push_back(centroid(ordered[i*3].pos, ordered[i*3+1].pos, ordered[i*3+2].pos));
    }
    // Sort and compare as multisets
    auto cmpVec = [](const glm::vec3& a, const glm::vec3& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.y != b.y) return a.y < b.y;
        return a.z < b.z;
    };
    std::sort(origCentroids.begin(), origCentroids.end(), cmpVec);
    std::sort(permCentroids.begin(), permCentroids.end(), cmpVec);
    int mismatch = 0;
    for (size_t i = 0; i < triCount; i++) {
        if (glm::length(origCentroids[i] - permCentroids[i]) > 1e-3f) mismatch++;
    }
    CHECK(mismatch == 0, mismatch << " triangle centroids differ after permutation (geometry lost)");

    std::cout << "\n" << (failures == 0 ? "PASS" : "FAIL") << " (" << failures << " failures)\n";
    return failures == 0 ? 0 : 1;
}

// -----------------------------------------------------------------------------
// BVH builder. Adapted from the earlier 8b attempt, with fixes based on what
// the test is designed to catch.
// -----------------------------------------------------------------------------
std::vector<BVHNode> buildBVH(const std::vector<Vertex>& inVerts,
                              std::vector<Vertex>&       outVerts) {
    struct Tri {
        glm::vec3 v0, v1, v2;
        glm::vec3 centroid;
        glm::vec3 bmin, bmax;
        int       origIndex;
    };
    const size_t triCount = inVerts.size() / 3;
    std::vector<Tri> tris(triCount);
    for (size_t i = 0; i < triCount; ++i) {
        tris[i].v0 = inVerts[i*3+0].pos;
        tris[i].v1 = inVerts[i*3+1].pos;
        tris[i].v2 = inVerts[i*3+2].pos;
        tris[i].centroid = (tris[i].v0 + tris[i].v1 + tris[i].v2) / 3.0f;
        tris[i].bmin = glm::min(glm::min(tris[i].v0, tris[i].v1), tris[i].v2);
        tris[i].bmax = glm::max(glm::max(tris[i].v0, tris[i].v1), tris[i].v2);
        tris[i].origIndex = (int)i;
    }

    std::vector<BVHNode> nodes;
    nodes.reserve(triCount * 2 + 1);
    nodes.emplace_back();

    std::function<void(int, size_t, size_t)> build = [&](int idx, size_t lo, size_t hi) {
        glm::vec3 bmin( 1e30f), bmax(-1e30f);
        glm::vec3 cmin( 1e30f), cmax(-1e30f);
        for (size_t i = lo; i < hi; ++i) {
            bmin = glm::min(bmin, tris[i].bmin);
            bmax = glm::max(bmax, tris[i].bmax);
            cmin = glm::min(cmin, tris[i].centroid);
            cmax = glm::max(cmax, tris[i].centroid);
        }
        // Inflate by tiny epsilon so boundary points are always inside.
        bmin -= glm::vec3(1e-5f);
        bmax += glm::vec3(1e-5f);
        nodes[idx].aabbMin = glm::vec4(bmin, 0.0f);
        nodes[idx].aabbMax = glm::vec4(bmax, 0.0f);

        size_t count = hi - lo;
        if (count <= 4) {
            nodes[idx].leftOrStart  = (int)lo;
            nodes[idx].rightOrCount = -(int)count;
            return;
        }

        glm::vec3 ext = cmax - cmin;
        int axis = (ext.x > ext.y) ? ((ext.x > ext.z) ? 0 : 2)
                                   : ((ext.y > ext.z) ? 1 : 2);

        size_t mid = lo + count / 2;
        std::nth_element(tris.begin() + lo, tris.begin() + mid, tris.begin() + hi,
                         [axis](const Tri& a, const Tri& b) {
                             return a.centroid[axis] < b.centroid[axis];
                         });

        int leftIdx  = (int)nodes.size(); nodes.emplace_back();
        int rightIdx = (int)nodes.size(); nodes.emplace_back();
        nodes[idx].leftOrStart  = leftIdx;
        nodes[idx].rightOrCount = rightIdx;

        build(leftIdx, lo, mid);
        build(rightIdx, mid, hi);
    };

    if (triCount > 0) build(0, 0, triCount);

    outVerts.resize(inVerts.size());
    for (size_t i = 0; i < triCount; ++i) {
        int srcTri = tris[i].origIndex;
        outVerts[i*3+0] = inVerts[srcTri*3+0];
        outVerts[i*3+1] = inVerts[srcTri*3+1];
        outVerts[i*3+2] = inVerts[srcTri*3+2];
    }
    return nodes;
}
