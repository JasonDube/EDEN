// Load the prefab through LIME'S OWN EditableMesh, not EDEN's loader.
// This is the reader that rejected the first attempt; it is the only one whose
// verdict counts for a file meant to be opened in the modeller.
#include "EditableMesh.hpp"
#include <cstdio>
using namespace eden;
int main(int argc, char** argv) {
    EditableMesh mesh;
    if (!mesh.loadLime(argv[1])) { std::printf("LIME REFUSED IT\n"); return 1; }
    std::vector<ModelVertex> v; std::vector<uint32_t> i;
    mesh.triangulate(v, i);                     // the exact call that logged the errors
    std::printf("LIME loaded it: %zu verts, %zu faces, %zu triangles\n",
                mesh.getVertexCount(), mesh.getFaceCount(), i.size() / 3);
    return 0;
}
