// agent: composer-2.5 | 2026-08-12 | gbuf VS pass inst id only | 938e73
// agent: composer-2.5 | 2026-08-12 | gbuf VS rebuild instance W | fe1ddf
// agent: composer-2.5 | 2026-08-12 | gbuf VS texelFetch cull | 69c0a2
/* Gbuf mesh VS: pack graph inst_i in instanceTransform[3][3]; cull via tex_inst_vis. */
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in mat4 instanceTransform;

uniform mat4 mvp;
uniform sampler2D tex_inst_vis;
uniform int ng_inst_cull; /* 1 = sample tex_inst_vis */

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
flat out float vInstId;

void main() {
  /* Rebuild col3 — assigning M[3][3] on an attribute copy can leave W=0. */
  float inst_i = instanceTransform[3].w;
  mat4 M = mat4(instanceTransform[0], instanceTransform[1], instanceTransform[2],
                vec4(instanceTransform[3].xyz, 1.0));
  vInstId = inst_i;
  fragPosition = vec3(M * vec4(vertexPosition, 1.0));
  fragTexCoord = vertexTexCoord;
  fragColor = vertexColor;
  fragNormal = normalize(mat3(M) * vertexNormal);
  if (ng_inst_cull != 0) {
    int ii = int(floor(inst_i + 0.5));
    if (ii >= 0) {
      float a = texelFetch(tex_inst_vis, ivec2(ii, 0), 0).r;
      float b = texelFetch(tex_inst_vis, ivec2(ii, 1), 0).r;
      if (max(a, b) < 0.5) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); /* off-screen */
        return;
      }
    }
  }
  gl_Position = mvp * M * vec4(vertexPosition, 1.0);
}
// agent: composer-2.5 | 2026-08-12 | gbuf VS pass inst id only | 938e73
// agent: composer-2.5 | 2026-08-12 | gbuf VS rebuild instance W | fe1ddf
// agent: composer-2.5 | 2026-08-12 | gbuf VS texelFetch cull | 69c0a2
