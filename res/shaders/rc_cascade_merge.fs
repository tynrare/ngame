// agent: composer-2.5 | 2026-08-09 | SS RC nearest merge | 5b4a12
in vec2 fragTexCoord;

uniform sampler2D tex_self;
uniform sampler2D tex_parent;
uniform sampler2D tex_depth;
uniform float ng_merge_weight;

out vec4 finalColor;

void main() {
  vec2 uv = fragTexCoord;
  vec3 self_r = texture(tex_self, uv).rgb;
  float d = texture(tex_depth, uv).r;
  if (d < 0.02) {
    finalColor = vec4(self_r, 1.0);
    return;
  }
  /* Nearest upsample of coarser cascade. */
  vec3 parent_r = texture(tex_parent, uv).rgb;
  finalColor = vec4(self_r + parent_r * ng_merge_weight, 1.0);
}
// agent: composer-2.5 | 2026-08-09 | SS RC nearest merge | 5b4a12
