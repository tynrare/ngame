// agent: composer-2.5 | 2026-08-09 | WS seed max stamp FS | cf47f6
// agent: composer-2.5 | 2026-08-09 | WS field additive seed FS | 6ea42f
/* Scene field for WS RC: emitters/verticals stamp (first-bounce radial march). */
in vec2 fragTexCoord;

uniform sampler2D tex_prev;
uniform sampler2D tex_stamp;

out vec4 finalColor;

void main() {
  vec3 s = texture(tex_stamp, fragTexCoord).rgb;
  vec3 p = texture(tex_prev, fragTexCoord).rgb;
  /* Mild re-emit on stamped surfaces only — keeps empty floor clear for rays. */
  float lum = max(s.r, max(s.g, s.b));
  finalColor = vec4(s + p * 0.25 * step(0.04, lum), 1.0);
}
// agent: composer-2.5 | 2026-08-09 | WS seed max stamp FS | cf47f6
// agent: composer-2.5 | 2026-08-09 | WS field additive seed FS | 6ea42f
