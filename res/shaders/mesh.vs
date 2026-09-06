// agent: composer-2.5 | 2026-07-25 | shared mesh vertex shader | 7b3e2a
// agent: composer-2.5 | 2026-08-09 | instanceTransform vertex shader | 435080
// agent: composer-2.5 | 2026-08-12 | gbuf VS pass inst id only | 938e73
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in mat4 instanceTransform;
in vec3 instanceColor;
in vec4 instanceUv;

uniform mat4 mvp;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main() {
  /* m15 may pack graph inst_i — rebuild col3 so W stays 1. */
  mat4 M = mat4(instanceTransform[0], instanceTransform[1], instanceTransform[2],
                vec4(instanceTransform[3].xyz, 1.0));
  fragPosition = vec3(M * vec4(vertexPosition, 1.0));
  fragTexCoord = mix(instanceUv.xy,instanceUv.zw,vertexTexCoord);
  fragColor = vec4(instanceColor,1.0);
  fragNormal = normalize(mat3(M) * vertexNormal);
  gl_Position = mvp * M * vec4(vertexPosition, 1.0);
}
// agent: composer-2.5 | 2026-07-25 | shared mesh vertex shader | 7b3e2a
// agent: composer-2.5 | 2026-08-09 | instanceTransform vertex shader | 435080
// agent: composer-2.5 | 2026-08-12 | gbuf VS pass inst id only | 938e73
