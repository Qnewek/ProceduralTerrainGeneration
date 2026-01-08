#include <unordered_map>
#include <vector>
#include <HeightGenerator.h>


struct ChunkCoord {
    int x;
    int z;

    bool operator==(const ChunkCoord& other) const {
        return x == other.x && z == other.z;
    }
};
struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord& c) const {
        std::size_t seed = 0;
        std::hash<int> hasher;
        seed ^= hasher(c.x) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= hasher(c.z) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;    
};

class Chunk {
public:
    static const int SIZE = 32;       
    int MAX_HEIGHT = 256;      
    float SCALE = 1.0f;        

    ChunkCoord coord;
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    bool needsUpload = false;

    Chunk(ChunkCoord c) : coord(c) {}

    void GenerateMesh(HeightGenerator& valueGenerator) {
        vertices.clear();
        indices.clear();
        vertices.reserve((SIZE + 1) * (SIZE + 1));
        indices.reserve(SIZE * SIZE * 6);

        for (int z = 0; z <= SIZE; ++z) {
            for (int x = 0; x <= SIZE; ++x) {

                float worldX = (coord.x * SIZE + x) * SCALE;
                float worldZ = (coord.z * SIZE + z) * SCALE;

                float y = valueGenerator.GetVal(worldX, worldZ) * MAX_HEIGHT;
                float hL = valueGenerator.GetVal(worldX - 1.0f, worldZ) * MAX_HEIGHT;
                float hR = valueGenerator.GetVal(worldX + 1.0f, worldZ) * MAX_HEIGHT;
                float hD = valueGenerator.GetVal(worldX, worldZ - 1.0f) * MAX_HEIGHT;
                float hU = valueGenerator.GetVal(worldX, worldZ + 1.0f) * MAX_HEIGHT;

                float nx = hL - hR;
                float nz = hD - hU;
                float ny = 2.0f;

                float invLen = 1.0f / std::sqrt(nx * nx + ny * ny + nz * nz);
                nx *= invLen; ny *= invLen; nz *= invLen;

                float u = static_cast<float>(x) / SIZE;
                float v = static_cast<float>(z) / SIZE;

                vertices.push_back({ (float)x * SCALE, y, (float)z * SCALE, nx, ny, nz, u, v });
            }
        }

        for (int z = 0; z < SIZE; ++z) {
            for (int x = 0; x < SIZE; ++x) {
                int row1 = z * (SIZE + 1);
                int row2 = (z + 1) * (SIZE + 1);

                indices.push_back(row1 + x);
                indices.push_back(row2 + x);
                indices.push_back(row1 + x + 1);

                indices.push_back(row1 + x + 1);
                indices.push_back(row2 + x);
                indices.push_back(row2 + x + 1);
            }
        }

        needsUpload = true;
    }
};