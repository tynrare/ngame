// agent: composer-2.5 | 2026-07-25 | shared mesh vertex shader | 7b3e2a
// agent: composer-2.5 | 2026-08-09 | instanceTransform vertex shader | 435080
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in mat4 instanceTransform;

uniform mat4 mvp;

out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;

void main() {
  fragPosition = vec3(instanceTransform * vec4(vertexPosition, 1.0));
  fragTexCoord = vertexTexCoord;
  fragColor = vertexColor;
  fragNormal = normalize(mat3(instanceTransform) * vertexNormal);
  gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);
}
// agent: composer-2.5 | 2026-07-25 | shared mesh vertex shader | 7b3e2a
// agent: composer-2.5 | 2026-08-09 | instanceTransform vertex shader | 435080
