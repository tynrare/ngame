// agent: grok-4.6 | 2026-08-30 | MSDF instanced glyph VS | 481d49
layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec2 vertexTexCoord;
layout(location = 9) in mat4 instanceTransform;
layout(location = 13) in vec3 instanceColor;
layout(location = 14) in vec4 instanceUv;
layout(location = 15) in float instanceOutline;

uniform mat4 mvp;
uniform mat4 ng_label;

out vec2 fragTexCoord;
out vec3 v_color;
out float v_outline;

void main() {
  fragTexCoord = mix(instanceUv.xy, instanceUv.zw, vertexTexCoord);
  v_color = instanceColor;
  v_outline = instanceOutline;
  vec4 p = instanceTransform * vec4(vertexPosition, 1.0);
  gl_Position = mvp * ng_label * p;
}
// agent: grok-4.6 | 2026-08-30 | MSDF instanced glyph VS | 481d49
