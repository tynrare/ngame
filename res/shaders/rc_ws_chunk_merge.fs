// agent: grok-4.6 | 2026-08-21 | chunk trilinear T-merge | db6e83
/* Child atlas W=dirs H=n^3. Parent coarser grid; 8-tap + T. */
in vec2 fragTexCoord;

uniform sampler2D tex_near;
uniform sampler2D tex_far;
uniform int ng_n_side;
uniform int ng_n_side_p;
uniform int ng_dir_count;
uniform int ng_dir_count_p;

out vec4 finalColor;

vec4 far_at(vec3 ijk, int dir_p) {
  int np = max(ng_n_side_p, 1);
  ivec3 ic = ivec3(clamp(ijk, vec3(0.0), vec3(float(np) - 1.001)));
  int h = ic.x + np * (ic.y + np * ic.z);
  return texelFetch(tex_far, ivec2(dir_p, h), 0);
}

void main() {
  int nd = max(ng_dir_count, 1);
  int n = max(ng_n_side, 1);
  int d = int(floor(gl_FragCoord.x));
  int h = int(floor(gl_FragCoord.y));
  if (d >= nd || h >= n * n * n) {
    finalColor = vec4(0.0);
    return;
  }
  vec4 near_r = texelFetch(tex_near, ivec2(d, h), 0);
  float a = clamp(near_r.a, 0.0, 1.0);
  if (a >= 0.999) {
    finalColor = near_r;
    return;
  }
  int ix = h % n;
  int iy = (h / n) % n;
  int iz = h / (n * n);
  float np = float(max(ng_n_side_p, 1));
  vec3 pu = (vec3(float(ix), float(iy), float(iz)) + 0.5) / float(n) * np - 0.5;
  vec3 i0 = floor(pu);
  vec3 f = fract(pu);
  int ndp = max(ng_dir_count_p, 1);
  int dir_p = clamp(int(floor((float(d) + 0.5) / float(nd) * float(ndp))), 0, ndp - 1);
  vec4 c000 = far_at(i0 + vec3(0.0, 0.0, 0.0), dir_p);
  vec4 c100 = far_at(i0 + vec3(1.0, 0.0, 0.0), dir_p);
  vec4 c010 = far_at(i0 + vec3(0.0, 1.0, 0.0), dir_p);
  vec4 c110 = far_at(i0 + vec3(1.0, 1.0, 0.0), dir_p);
  vec4 c001 = far_at(i0 + vec3(0.0, 0.0, 1.0), dir_p);
  vec4 c101 = far_at(i0 + vec3(1.0, 0.0, 1.0), dir_p);
  vec4 c011 = far_at(i0 + vec3(0.0, 1.0, 1.0), dir_p);
  vec4 c111 = far_at(i0 + vec3(1.0, 1.0, 1.0), dir_p);
  vec4 c00 = mix(c000, c100, f.x);
  vec4 c10 = mix(c010, c110, f.x);
  vec4 c01 = mix(c001, c101, f.x);
  vec4 c11 = mix(c011, c111, f.x);
  vec4 c0 = mix(c00, c10, f.y);
  vec4 c1 = mix(c01, c11, f.y);
  vec4 far_r = mix(c0, c1, f.z);
  float T = 1.0 - a;
  vec3 rad = near_r.rgb + T * far_r.rgb;
  float ao = a + T * clamp(far_r.a, 0.0, 1.0);
  finalColor = vec4(rad, ao);
}
// agent: grok-4.6 | 2026-08-21 | chunk trilinear T-merge | db6e83
