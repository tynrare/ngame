// agent: composer-2.5 | 2026-08-09 | fullscreen textured quad VS | 8a1c02
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 fragTexCoord;

void main() {
  fragTexCoord = vertexTexCoord;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
// agent: composer-2.5 | 2026-08-09 | fullscreen textured quad VS | 8a1c02
