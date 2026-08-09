// agent: composer-2.5 | 2026-08-09 | SS RC nearest merge | 5b4a12
// agent: composer-2.5 | 2026-08-09 | miss-gated cascade merge | ee18c4
in vec2 fragTexCoord;

uniform sampler2D tex_self;
uniform sampler2D tex_parent;
uniform sampler2D tex_depth;
uniform float ng_merge_weight;

out vec4 finalColor;

void main() {
  vec2 uv = fragTexCoord;
  vec4 self_r = texture(tex_self, uv);
  float d = texture(tex_depth, uv).r;
  if (d < 0.02) {
    finalColor = self_r;
    return;
  }
  vec4 parent_r = texture(tex_parent, uv);
  float hit = clamp(self_r.a, 0.0, 1.0);
  float miss = 1.0 - hit;
  vec3 rad = self_r.rgb + miss * parent_r.rgb * ng_merge_weight;
  float a = hit + miss * parent_r.a;
  finalColor = vec4(rad, a);
}
// agent: composer-2.5 | 2026-08-09 | SS RC nearest merge | 5b4a12
// agent: composer-2.5 | 2026-08-09 | miss-gated cascade merge | ee18c4
