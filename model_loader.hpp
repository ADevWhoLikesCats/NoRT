#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include "tiny_gltf.h"

struct ModelVertex {
    glm::vec4 pos;      // xyz=position
    glm::vec4 albedo;   // rgb=baseColorFactor
    glm::vec4 mr;       // x=metallic, y=roughness
    glm::vec2 uv;
    glm::vec2 _pad;
};
static_assert(sizeof(ModelVertex) == 64, "ModelVertex must be 64 bytes to match bvh.comp Vtx");

struct ModelMaterial {
    std::string name = "default";
    glm::vec4   baseColorFactor{1.0f};
    glm::vec3   emissiveFactor{0.0f};
    float       metallicFactor  = 1.0f;
    float       roughnessFactor = 1.0f;
    int albedoSrc = -1, mrSrc = -1, normalSrc = -1, emissiveSrc = -1;
};

struct SceneNode {
    std::string name;
    std::vector<int> children;
    glm::mat4 localTransform{1.0f};
    glm::mat4 worldTransform{1.0f};
    int mesh = -1;
};

struct SubMesh {
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    int      material    = -1;
};

struct ModelData {
    std::vector<ModelVertex>   vertices;
    std::vector<ModelMaterial> materials;
    std::vector<SubMesh>       submeshes;
    std::vector<SceneNode>     nodes;
    struct RawImage {
        std::string name;
        int width = 0, height = 0, components = 0;
        std::vector<uint8_t> pixels;
    };
    std::vector<RawImage> images;
    glm::vec3 aabbMin{ 1e30f}, aabbMax{-1e30f}, center{0.0f};
    float radius = 1.0f;
};

namespace gltf_detail {

inline glm::mat4 nodeLocalMatrix(const tinygltf::Node& node) {
    if (node.matrix.size() == 16) {
        glm::mat4 m;
        std::memcpy(glm::value_ptr(m), node.matrix.data(), sizeof(float) * 16);
        return m;
    }
    glm::mat4 T(1.0f), R(1.0f), S(1.0f);
    if (node.translation.size() == 3)
        T = glm::translate(T, glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
    if (node.rotation.size() == 4) {
        glm::quat q((float)node.rotation[3], (float)node.rotation[0],
                    (float)node.rotation[1], (float)node.rotation[2]);
        R = glm::mat4_cast(q);
    }
    if (node.scale.size() == 3)
        S = glm::scale(S, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    return T * R * S;
}

template <typename T>
inline const T* accessorData(const tinygltf::Model& model, const tinygltf::Accessor& acc) {
    if (acc.bufferView < 0) return nullptr;
    const auto& view = model.bufferViews[acc.bufferView];
    const auto& buf  = model.buffers[view.buffer];
    return reinterpret_cast<const T*>(buf.data.data() + view.byteOffset + acc.byteOffset);
}

inline uint32_t readIndex(const tinygltf::Model& model, const tinygltf::Accessor& acc, size_t i) {
    const auto& view = model.bufferViews[acc.bufferView];
    const uint8_t* base = model.buffers[view.buffer].data.data() + view.byteOffset + acc.byteOffset;
    switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  return reinterpret_cast<const uint8_t*>(base)[i];
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return reinterpret_cast<const uint16_t*>(base)[i];
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   return reinterpret_cast<const uint32_t*>(base)[i];
        default: return 0;
    }
}

inline void walkNode(const tinygltf::Model& model, int nodeIdx,
                     const glm::mat4& parentWorld,
                     ModelData& out, std::vector<int>& visited) {
    if (visited[nodeIdx] >= 0) return;
    visited[nodeIdx] = (int)out.nodes.size();

    const auto& node = model.nodes[nodeIdx];
    glm::mat4 local = nodeLocalMatrix(node);
    glm::mat4 world = parentWorld * local;

    SceneNode sn;
    sn.name = node.name; sn.localTransform = local;
    sn.worldTransform = world; sn.mesh = node.mesh; sn.children = node.children;
    out.nodes.push_back(sn);

    if (node.mesh >= 0) {
        const auto& mesh = model.meshes[node.mesh];
        for (const auto& prim : mesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) continue;
            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;
            const auto& posAcc = model.accessors[posIt->second];
            const float* posData = accessorData<float>(model, posAcc);
            if (!posData) continue;

            const float* uvData = nullptr;
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end())
                uvData = accessorData<float>(model, model.accessors[uvIt->second]);

            bool indexed = prim.indices >= 0;
            const tinygltf::Accessor* idxAcc = indexed ? &model.accessors[prim.indices] : nullptr;
            size_t vertCount = indexed ? idxAcc->count : posAcc.count;

            int matIdx = prim.material;
            glm::vec4 baseColor(1.0f);
            glm::vec4 mr(1.0f, 1.0f, 0.0f, 0.0f);
            if (matIdx >= 0 && matIdx < (int)model.materials.size()) {
                const auto& m = model.materials[matIdx];
                const auto& pbr = m.pbrMetallicRoughness;
                if (pbr.baseColorFactor.size() == 4)
                    baseColor = glm::vec4((float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1],
                                          (float)pbr.baseColorFactor[2], (float)pbr.baseColorFactor[3]);
                mr.x = (float)pbr.metallicFactor;
                mr.y = (float)pbr.roughnessFactor;
            }

            SubMesh sub;
            sub.firstVertex = (uint32_t)out.vertices.size();
            sub.material    = matIdx;

            for (size_t i = 0; i + 2 < vertCount; i += 3) {
                for (int k = 0; k < 3; ++k) {
                    uint32_t vi = indexed ? readIndex(model, *idxAcc, i + k) : (uint32_t)(i + k);
                    glm::vec3 p(posData[vi*3+0], posData[vi*3+1], posData[vi*3+2]);
                    glm::vec4 wp = world * glm::vec4(p, 1.0f);
                    ModelVertex v{};
                    v.pos = wp; v.albedo = baseColor; v.mr = mr;
                    if (uvData) v.uv = glm::vec2(uvData[vi*2+0], uvData[vi*2+1]);
                    out.vertices.push_back(v);
                }
            }
            sub.vertexCount = (uint32_t)out.vertices.size() - sub.firstVertex;
            if (sub.vertexCount > 0) out.submeshes.push_back(sub);
        }
    }

    for (int child : node.children)
        walkNode(model, child, world, out, visited);
}

} // namespace gltf_detail

inline ModelData loadGLB(const std::string& path) {
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    if (!loader.LoadBinaryFromFile(&model, &err, &warn, path))
        throw std::runtime_error("failed to load glb: " + path + (err.empty() ? "" : " — " + err));
    if (!warn.empty()) std::cerr << "glTF warn: " << warn << "\n";

    ModelData out;
    for (const auto& m : model.materials) {
        ModelMaterial mm;
        mm.name = m.name;
        mm.metallicFactor  = (float)m.pbrMetallicRoughness.metallicFactor;
        mm.roughnessFactor = (float)m.pbrMetallicRoughness.roughnessFactor;
        const auto& bc = m.pbrMetallicRoughness.baseColorFactor;
        if (bc.size() == 4)
            mm.baseColorFactor = glm::vec4((float)bc[0], (float)bc[1], (float)bc[2], (float)bc[3]);
        if (m.emissiveFactor.size() == 3)
            mm.emissiveFactor = glm::vec3((float)m.emissiveFactor[0], (float)m.emissiveFactor[1], (float)m.emissiveFactor[2]);
        auto src = [&](int t) -> int {
            if (t < 0 || t >= (int)model.textures.size()) return -1;
            return model.textures[t].source;
        };
        mm.albedoSrc   = src(m.pbrMetallicRoughness.baseColorTexture.index);
        mm.mrSrc       = src(m.pbrMetallicRoughness.metallicRoughnessTexture.index);
        mm.normalSrc   = src(m.normalTexture.index);
        mm.emissiveSrc = src(m.emissiveTexture.index);
        out.materials.push_back(mm);
    }
    if (out.materials.empty()) out.materials.push_back(ModelMaterial{});

    for (const auto& img : model.images) {
        ModelData::RawImage ri;
        ri.name = img.name; ri.width = img.width; ri.height = img.height; ri.components = img.component;
        if (!img.image.empty()) ri.pixels = img.image;
        out.images.push_back(std::move(ri));
    }

    int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIdx >= 0 && sceneIdx < (int)model.scenes.size()) {
        std::vector<int> visited(model.nodes.size(), -1);
        for (int root : model.scenes[sceneIdx].nodes)
            gltf_detail::walkNode(model, root, glm::mat4(1.0f), out, visited);
    }

    glm::vec3 mn( 1e30f), mx(-1e30f);
    for (const auto& v : out.vertices) { mn = glm::min(mn, glm::vec3(v.pos)); mx = glm::max(mx, glm::vec3(v.pos)); }
    if (out.vertices.empty()) { mn = glm::vec3(-1); mx = glm::vec3(1); }
    out.aabbMin = mn; out.aabbMax = mx; out.center = (mn + mx) * 0.5f;
    out.radius = glm::length(mx - mn) * 0.5f;

    std::cout << "loaded " << path << ": " << out.vertices.size()/3 << " tris, "
              << out.materials.size() << " materials, " << out.images.size() << " images, "
              << out.nodes.size() << " nodes\n";
    return out;
}
