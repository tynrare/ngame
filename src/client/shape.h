// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | fea510
#ifndef NG_SHAPE_H
#define NG_SHAPE_H
#include <raylib.h>
#include <stdbool.h>
typedef struct NgShapeInstance {
  float transform[16], tint[3], uv[4], outline;
} NgShapeInstance;
typedef struct NgShapeStats {
  unsigned draws, instances, capacity;
  unsigned long long digest;
} NgShapeStats;
/** @param reset bool clear counters after read. @return NgShapeStats submission
 * diagnostics. */
NgShapeStats ng_shape_stats(bool reset);
/** @param m Matrix pose. @param tint const unsigned char* RGB or NULL white.
 * @return NgShapeInstance full UV instance. */
NgShapeInstance ng_shape_instance(Matrix m, const unsigned char *tint);
/** @return Mesh shared XY plane, lazily uploaded. */
Mesh ng_shape_plane(void);
/** @param mesh Mesh geometry. @param material Material bindings. @param
 * instances const NgShapeInstance* data. @param count int length. @param mvp
 * Matrix camera. @return void; retained GPU buffer grows only at capacity
 * boundaries. */
void ng_shape_draw(Mesh mesh, Material material,
                   const NgShapeInstance *instances, int count, Matrix mvp);
/** @return void; releases shared geometry and instance buffer. */
void ng_shape_shutdown(void);
#endif
// agent: gpt-6 | 2026-09-06 | share shape rendering and material recipes | fea510
