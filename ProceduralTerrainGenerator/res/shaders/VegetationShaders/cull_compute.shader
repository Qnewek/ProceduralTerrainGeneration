#version 450 core

//Culling and sorting of the placed vegetation for indirect drawing (VegetationLayer::Draw), in three passes:
//  pass 0 (a workgroup per range): every instance of the ranges in the frustum and in range gets a group (model, variant, level
//         of detail; level 3 = impostor) and its index in the group (atomic count)
//  pass 1 (one workgroup): start of every group in the output, then instanceCount and baseInstance of every command
//  pass 2 (a workgroup per range): every instance kept is copied to the start of its group plus its index
layout(local_size_x = 64) in;

//Instance: (x, height, z, world units per metre), (yaw, variant, model, layer) = VegetationModels::GpuInstance
struct Instance {
    vec4 positionScale;
    vec4 data;
};
layout(std430, binding = 0) readonly buffer Placed { Instance placed[]; };
//Ranges of placed instances to look at; outBase = sum of the counts of the ranges before (index into classified)
struct Range {
    uint first;
    uint count;
    uint outBase;
    uint pad;
};
layout(std430, binding = 1) readonly buffer Ranges { Range ranges[]; };
//Per instance of the ranges: group << 20 | index in the group, CULLED if not drawn
layout(std430, binding = 2) buffer Classified { uint classified[]; };
layout(std430, binding = 3) buffer Counts { uint counts[]; };
//Per variant of all models: x = largest horizontal extent around the base, y, z = lowest and highest point, in metres;
//w = its last level of detail
layout(std430, binding = 4) readonly buffer Variants { vec4 variants[]; };
//Per model: first variant in Variants, first group, 1 if it has impostors
struct ModelInfo {
    uint variantBase;
    uint groupBase;
    uint hasImpostors;
    uint pad;
};
layout(std430, binding = 5) readonly buffer Models { ModelInfo models[]; };
layout(std430, binding = 6) coherent buffer Offsets { uint offsets[]; };
//DrawElementsIndirectCommand of every part of every model, and the group it draws
struct MeshCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint baseInstance;
};
layout(std430, binding = 7) buffer MeshCommands { MeshCommand meshCommands[]; };
layout(std430, binding = 8) readonly buffer MeshGroups { uint meshGroups[]; };
//DrawArraysIndirectCommand of the impostors of every variant with impostors, and the group it draws
struct ImpostorCommand {
    uint count;
    uint instanceCount;
    uint first;
    uint baseInstance;
};
layout(std430, binding = 9) buffer ImpostorCommands { ImpostorCommand impostorCommands[]; };
layout(std430, binding = 10) readonly buffer ImpostorGroups { uint impostorGroups[]; };
layout(std430, binding = 11) writeonly buffer Drawn { Instance drawn[]; };

const uint CULLED = 0xFFFFFFFFu;
const int IMPOSTOR_LOD = 3;

uniform int pass;
uniform vec3 eye;               //camera before modelScale
uniform vec4 planes[6];         //frustum in world units before modelScale (Frustum)
uniform float heightScale;
//Per layer: squared distances in world units from which LOD 1, LOD 2 and impostors are used, and beyond which nothing is drawn
uniform vec4 rangeSquared[3];
uniform bool impostorsOn;
uniform int groupCount;
uniform int meshCommandCount;
uniform int impostorCommandCount;

bool InFrustum(vec3 boxMin, vec3 boxMax)
{
    for (int i = 0; i < 6; i++) {
        vec4 p = planes[i];
        vec3 corner = vec3(p.x >= 0.0 ? boxMax.x : boxMin.x, p.y >= 0.0 ? boxMax.y : boxMin.y, p.z >= 0.0 ? boxMax.z : boxMin.z);
        if (dot(p.xyz, corner) + p.w < 0.0) {
            return false;
        }
    }
    return true;
}

uint Classify(Instance instance)
{
    vec3 base = instance.positionScale.xyz * vec3(1.0, heightScale, 1.0);
    vec3 offset = base - eye;
    float distanceSquared = dot(offset, offset);
    int layer = int(instance.data.w + 0.5);
    vec4 r = rangeSquared[layer];
    if (distanceSquared > r.w) {
        return CULLED;
    }
    uint model = uint(instance.data.z + 0.5);
    uint variant = uint(instance.data.y + 0.5);
    ModelInfo info = models[model];
    //The box of the variant turned any way: its largest horizontal extent around the base
    vec4 bounds = variants[info.variantBase + variant];
    float scale = instance.positionScale.w;
    float reach = bounds.x * scale;
    if (!InFrustum(base + vec3(-reach, bounds.y * scale, -reach), base + vec3(reach, bounds.z * scale, reach))) {
        return CULLED;
    }
    int lod = distanceSquared < r.x ? 0 : distanceSquared < r.y ? 1 : distanceSquared < r.z ? 2 : IMPOSTOR_LOD;
    if (lod == IMPOSTOR_LOD && (!impostorsOn || info.hasImpostors == 0u)) {
        lod = 2;
    }
    if (lod != IMPOSTOR_LOD) {
        lod = min(lod, int(bounds.w + 0.5));
    }
    return info.groupBase + variant * 4u + uint(lod);
}

void main()
{
    if (pass == 1) {
        //Few groups: one thread sums them, then all fill the commands
        if (gl_LocalInvocationIndex == 0u) {
            uint sum = 0u;
            for (int g = 0; g < groupCount; g++) {
                offsets[g] = sum;
                sum += counts[g];
            }
            memoryBarrierBuffer();
        }
        barrier();
        for (int c = int(gl_LocalInvocationIndex); c < meshCommandCount; c += 64) {
            uint g = meshGroups[c];
            meshCommands[c].instanceCount = counts[g];
            meshCommands[c].baseInstance = offsets[g];
        }
        for (int c = int(gl_LocalInvocationIndex); c < impostorCommandCount; c += 64) {
            uint g = impostorGroups[c];
            impostorCommands[c].instanceCount = counts[g];
            impostorCommands[c].baseInstance = offsets[g];
        }
        return;
    }

    Range range = ranges[gl_WorkGroupID.x];
    for (uint i = gl_LocalInvocationIndex; i < range.count; i += 64u) {
        Instance instance = placed[range.first + i];
        if (pass == 0) {
            uint group = Classify(instance);
            classified[range.outBase + i] = group == CULLED ? CULLED : (group << 20) | atomicAdd(counts[group], 1u);
        }
        else {
            uint c = classified[range.outBase + i];
            if (c != CULLED) {
                drawn[offsets[c >> 20] + (c & 0xFFFFFu)] = instance;
            }
        }
    }
}
