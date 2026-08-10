// agent: composer-2.5 | 2026-08-09 | WS atlas stamp butter FS | a8dda5
/* Butter: mix previous world atlas with freshly stamped atlas. */
in vec2 fragTexCoord;

uniform sampler2D tex_prev;
uniform sampler2D tex_curr;
uniform float ng_butter; /* 0 = keep prev, 1 = full curr */

out vec4 finalColor;

void main() {
  vec4 p = texture(tex_prev, fragTexCoord);
  vec4 c = texture(tex_curr, fragTexCoord);
  float k = clamp(ng_butter, 0.0, 1.0);
  finalColor = mix(p, c, k);
}
// agent: composer-2.5 | 2026-08-09 | WS atlas stamp butter FS | a8dda5
