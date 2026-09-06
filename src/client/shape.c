// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 92faaf
/* Flow ID: shape-submit (canonical owner).
 * Scope index: frame-draw owns lifetimes; font-msdf owns glyph layout.
 * 1) renderer/font layout → instance values → mesh or XY plane with tint plus
 * UV. 2) renderer → compatible batch/run → material bound once, retained buffer
 * upload. 3) backend → instanced triangles → scoped pass output; caller owns
 * blend/depth policy. Invariants: transparent callers preserve adjacent runs;
 * never register transient GI geometry.
 */
#include "shape.h"
#include <raymath.h>
#include <rlgl.h>
#include <stddef.h>
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#elif defined(_WIN32)
#include "glad.h"
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#endif
static Mesh plane;
static unsigned int buffer;
static int capacity;
static NgShapeStats stats;
static bool capture;
/** @param reset bool clear counters. @return NgShapeStats reusable buffer and
 * submission diagnostics. */
NgShapeStats ng_shape_stats(bool reset) {
  NgShapeStats out = stats;
  out.capacity = capacity;
  if (reset) {
    stats = (NgShapeStats){0};
    capture = true;
  }
  return out;
}
/** @param m Matrix pose. @param tint const unsigned char* RGB or NULL. @return
 * NgShapeInstance packed attributes. */
NgShapeInstance ng_shape_instance(Matrix m, const unsigned char *tint) {
  NgShapeInstance out = {.tint = {1, 1, 1}, .uv = {0, 0, 1, 1}};
  memcpy(out.transform, MatrixToFloatV(m).v, sizeof(out.transform));
  if (tint)
    for (int i = 0; i < 3; i++)
      out.tint[i] = tint[i] / 255.0f;
  return out;
}
/** @return Mesh cached XY unit plane. */
Mesh ng_shape_plane(void) {
  if (plane.vaoId)
    return plane;
  plane.vertexCount = 4;
  plane.triangleCount = 2;
  const float vertices[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  const float uv[] = {0, 0, 1, 0, 1, 1, 0, 1};
  const unsigned short indices[] = {0, 1, 2, 0, 2, 3};
  plane.vertices = MemAlloc(sizeof(vertices));
  plane.texcoords = MemAlloc(sizeof(uv));
  plane.indices = MemAlloc(sizeof(indices));
  memcpy(plane.vertices, vertices, sizeof(vertices));
  memcpy(plane.texcoords, uv, sizeof(uv));
  memcpy(plane.indices, indices, sizeof(indices));
  UploadMesh(&plane, false);
  return plane;
}
/** @param mesh Mesh geometry. @param material Material bindings. @param
 * instances const NgShapeInstance* data. @param count int length. @param mvp
 * Matrix camera. @return void. */
void ng_shape_draw(Mesh mesh, Material material,
                   const NgShapeInstance *instances, int count, Matrix mvp) {
  if (!count || !mesh.vaoId || !material.shader.id)
    return;
  stats.draws++;
  stats.instances += (unsigned)count;
  if (capture) {
    stats.digest = (stats.digest ^ mesh.vaoId) * 1099511628211ull;
    stats.digest = (stats.digest ^ material.shader.id) * 1099511628211ull;
    stats.digest =
        (stats.digest ^
         (material.maps ? material.maps[MATERIAL_MAP_ALBEDO].texture.id : 0)) *
        1099511628211ull;
  }
  const unsigned char *bytes = (const unsigned char *)instances;
  if (capture)
    for (size_t i = 0; i < (size_t)count * sizeof(*instances); i++)
      stats.digest = (stats.digest ^ bytes[i]) * 1099511628211ull;
  rlDrawRenderBatchActive();
  rlEnableShader(material.shader.id);
  SetShaderValueMatrix(material.shader,
                       material.shader.locs[SHADER_LOC_MATRIX_MVP], mvp);
  rlActiveTextureSlot(0);
  rlEnableTexture(material.maps ? material.maps[MATERIAL_MAP_ALBEDO].texture.id
                                : rlGetTextureIdDefault());
  rlEnableVertexArray(mesh.vaoId);
  if (!buffer)
    glGenBuffers(1, &buffer);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  if (count > capacity) {
    capacity = capacity ? capacity : 64;
    while (capacity < count)
      capacity *= 2;
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)capacity * sizeof(*instances),
                 NULL, GL_DYNAMIC_DRAW);
  }
  glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)count * sizeof(*instances),
                  instances);
  const char *names[] = {"instanceTransform", "instanceColor", "instanceUv",
                         "instanceOutline"};
  const int widths[] = {4, 3, 4, 1};
  const size_t offsets[] = {0, offsetof(NgShapeInstance, tint),
                            offsetof(NgShapeInstance, uv),
                            offsetof(NgShapeInstance, outline)};
  int locations[7], n = 0;
  for (int a = 0; a < 4; a++) {
    int loc = GetShaderLocationAttrib(material.shader, names[a]);
    if (loc < 0)
      continue;
    for (int c = 0; c < (a == 0 ? 4 : 1); c++) {
      glEnableVertexAttribArray(loc + c);
      glVertexAttribPointer(loc + c, widths[a], GL_FLOAT, GL_FALSE,
                            sizeof(*instances),
                            (void *)(offsets[a] + c * 4 * sizeof(float)));
      glVertexAttribDivisor(loc + c, 1);
      locations[n++] = loc + c;
    }
  }
  if (mesh.indices)
    rlDrawVertexArrayElementsInstanced(0, mesh.triangleCount * 3, 0, count);
  else
    rlDrawVertexArrayInstanced(0, mesh.vertexCount, count);
  for (int i = 0; i < n; i++) {
    glDisableVertexAttribArray(locations[i]);
    glVertexAttribDivisor(locations[i], 0);
  }
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  rlDisableVertexArray();
  rlDisableTexture();
  rlDisableShader();
}
/** @return void; scene cleanup. */
void ng_shape_shutdown(void) {
  if (buffer)
    glDeleteBuffers(1, &buffer);
  if (plane.vaoId)
    UnloadMesh(plane);
  buffer = 0;
  capacity = 0;
  plane = (Mesh){0};
  stats = (NgShapeStats){0};
  capture = false;
}
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | 92faaf
