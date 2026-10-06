//Drawing a job's rows (draw.cpp sets jobs up): each pixel's color, depth, texture coordinates and fog are worked out
//from the job exactly as drawing the whole primitive at once always worked them out (the same operations on the same
//numbers, in the same order, so each rounds the same), then the pixel is textured (texture.cpp) and goes through the
//pixel pipeline (pixel.cpp). Any rows of a job may be drawn, in any order: no pixel depends on another of the same
//primitive (but where it reads its own texture, which texture.cpp never decodes then, and draw.cpp draws in order).
//
//Each loop is written once for each frame buffer format (a template), so the pipeline's reads and writes of the
//frame buffer are the format's own, with no choosing at each pixel.

//A pixel's color: the vertex color, through the texture if there is one, plus lighting's shine when it's kept apart
//(each channel held to 255), then into the pixel pipeline.
template<u32 Format>
auto GE::shadeAs(const Look& look, bool linear, s32 x, s32 y, u32 z, u32 color, u32 specular, float u, float v,
                 u32 fog) -> void {
  if(look.textured) color = combine(look, color, sampleWith(look.texture, linear, u, v));
  if(specular) {
    color = pack(channel(color, 0) + channel(specular, 0), channel(color, 1) + channel(specular, 1),
                 channel(color, 2) + channel(specular, 2), channel(color, 3));
  }
  drawPixelAs<Format>(look.pixel, x, y, z, color, fog);
}

//A sprite's rows. In 2D a texture coordinate runs across x and the other down y, so each column's (and each row's)
//is worked out once, as far as the texels it falls between; in 3D both follow the perspective at every pixel.
template<u32 Format>
auto GE::spriteRows(const Job& job, s32 fromY, s32 toY) -> void {
  auto& s = job.sprite;
  auto& look = *job.look;
  s32 firstY = std::max(job.firstY, fromY), lastY = std::min(job.lastY, toY);
  if(s.divided) {
    for(s32 y = firstY; y <= lastY; y++) {
      for(s32 x = job.firstX; x <= job.lastX; x++) {
        s32 sampleX = x * 16 + 8, sampleY = y * 16 + 8;
        f64 alongX = f64(sampleX - s.left) / (s.right - s.left), alongY = f64(sampleY - s.top) / (s.bottom - s.top);
        f64 inverse = s.leftInverse + (s.rightInverse - s.leftInverse) * alongX;
        f64 acrossX = (s.leftAcross + (s.rightAcross - s.leftAcross) * alongX) / inverse;
        f64 downY = (s.topDown + (s.bottomDown - s.topDown) * alongY) / inverse;
        float u = s.turned ? downY : acrossX;
        float v = s.turned ? acrossX : downY;
        u32 fog = sampleX + 1 >= s.middle ? s.rightFog : s.leftFog;
        shadeAs<Format>(look, job.linear, x, y, s.z, s.color, s.specular, u, v, fog);
      }
    }
    return;
  }
  auto fogAt = [&](s32 x) { return x * 16 + 8 + 1 >= s.middle ? s.rightFog : s.leftFog; };
  if(!look.textured) {
    u32 color = s.color;
    if(s.specular) {
      color = pack(channel(color, 0) + channel(s.specular, 0), channel(color, 1) + channel(s.specular, 1),
                   channel(color, 2) + channel(s.specular, 2), channel(color, 3));
    }
    for(s32 y = firstY; y <= lastY; y++) {
      for(s32 x = job.firstX; x <= job.lastX; x++) drawPixelAs<Format>(look.pixel, x, y, s.z, color, fogAt(x));
    }
    return;
  }
  //the coordinate across x is u (v when turned): its texel(s) for each column
  auto& t = look.texture;
  TexelAxis columns[1024];
  for(s32 x = job.firstX; x <= job.lastX; x++) {
    float column = s.columnFirst + f64(x * 16 + 8 - s.columnStart) / 16 * s.columnStep;
    columns[x - job.firstX] = s.turned ? texelAxis(column, t.height, t.clampV, job.linear)
                                       : texelAxis(column, t.width, t.clampU, job.linear);
  }
  for(s32 y = firstY; y <= lastY; y++) {
    float row = s.rowFirst + f64(y * 16 + 8 - s.rowStart) / 16 * s.rowStep;
    TexelAxis down = s.turned ? texelAxis(row, t.width, t.clampU, job.linear)
                              : texelAxis(row, t.height, t.clampV, job.linear);
    for(s32 x = job.firstX; x <= job.lastX; x++) {
      const TexelAxis& across = columns[x - job.firstX];
      const TexelAxis& u = s.turned ? down : across;
      const TexelAxis& v = s.turned ? across : down;
      u32 color = combine(look, s.color, job.linear ? filtered(t, u, v) : fetch(t, u.first, v.first));
      if(s.specular) {
        color = pack(channel(color, 0) + channel(s.specular, 0), channel(color, 1) + channel(s.specular, 1),
                     channel(color, 2) + channel(s.specular, 2), channel(color, 3));
      }
      drawPixelAs<Format>(look.pixel, x, y, s.z, color, fogAt(x));
    }
  }
}

//A triangle's rows. Its edges' functions (positive on the triangle's side, zero on the edge) are whole numbers, so
//each row's pixels inside it are found by dividing, not by trying every pixel of the box around it, and the
//functions step along the row by adding: the same numbers as working each out afresh. What a pixel's color, depth,
//texture coordinates and fog are blended from is worked out only where the pipeline uses it.
template<u32 Format>
auto GE::triangleRows(const Job& job, s32 fromY, s32 toY) -> void {
  auto& r = job.triangle;
  auto& look = *job.look;
  auto& p = look.pixel;
  //edge k at (x, y), in sixteenths: a[k] * x + b[k] * y + c[k]; edge 0 runs from corner 1 to 2, 1 from 2 to 0, 2 from
  //0 to 1, and a pixel's blending weights are the three at its middle
  s64 a[3], b[3], c[3];
  for(u32 k = 0; k < 3; k++) {
    u32 from = (k + 1) % 3, to = (k + 2) % 3;
    a[k] = r.y[from] - r.y[to], b[k] = r.x[to] - r.x[from], c[k] = r.y[to] * r.x[from] - r.x[to] * r.y[from];
  }
  bool needsZ = p.depthRange || (p.clear ? p.clearDepth : p.depthTest);
  bool blended = !r.flat, shining = !r.flat && r.shines;
  float colors[3][4], shines[3][4];  //each corner's channels, as the blending takes them
  for(u32 k = 0; k < 3; k++) {
    for(u32 n = 0; n < 4; n++) colors[k][n] = channel(r.color[k], n), shines[k][n] = channel(r.specular[k], n);
  }
  //a weighted sum of the corners' values, as the blending has always taken it
  auto blendColor = [&](const float (&value)[3][4], float w0, float w1, float w2) {
    float mixed[4];
    for(u32 n = 0; n < 4; n++) mixed[n] = (value[0][n] * w0 + value[1][n] * w1 + value[2][n] * w2) / r.total;
    u32 color = 0;
    for(u32 n = 0; n < 4; n++) color |= u32(std::clamp(s32(mixed[n]), 0, 255)) << n * 8;
    return color;
  };
  for(s32 y = std::max(job.firstY, fromY); y <= std::min(job.lastY, toY); y++) {
    s64 sampleY = s64(y) * 16 + 8;
    s64 sampleX = s64(job.firstX) * 16 + 8;
    //the row's pixels inside: where every edge's function plus its bias is at least zero
    s64 start = job.firstX, stop = job.lastX;
    s64 w[3];
    for(u32 k = 0; k < 3; k++) {
      w[k] = a[k] * sampleX + b[k] * sampleY + c[k];
      s64 have = w[k] + r.bias[k], step = a[k] * 16;  //at the first column, and from one column to the next
      if(step > 0 && have < 0) start = std::max(start, job.firstX + (-have + step - 1) / step);
      if(step < 0) stop = have < 0 ? -1 : std::min(stop, job.firstX + have / -step);
      if(step == 0 && have < 0) stop = -1;
    }
    if(start > stop) continue;
    for(u32 k = 0; k < 3; k++) w[k] += a[k] * 16 * (start - job.firstX);
    for(s32 x = start; x <= stop; x++, w[0] += a[0] * 16, w[1] += a[1] * 16, w[2] += a[2] * 16) {
      float w0 = float(w[0]), w1 = float(w[1]), w2 = float(w[2]);
      auto blend = [&](float first, float second, float third) {
        return (first * w0 + second * w1 + third * w2) / r.total;
      };
      u32 color = blended ? blendColor(colors, w0, w1, w2) : r.flatColor;
      u32 specular = shining ? blendColor(shines, w0, w1, w2) : r.flatSpecular;
      u32 z = needsZ ? u32(std::clamp(blend(r.z[0], r.z[1], r.z[2]), 0.0f, 65535.0f)) : 0;
      float u = 0, v = 0;
      if(look.textured && r.perspective) {
        float ka = w0 / r.w[0], kb = w1 / r.w[1], kc = w2 / r.w[2];
        float divisor = ka * r.q[0] + kb * r.q[1] + kc * r.q[2];
        u = (ka * r.u[0] + kb * r.u[1] + kc * r.u[2]) / divisor;
        v = (ka * r.v[0] + kb * r.v[1] + kc * r.v[2]) / divisor;
      } else if(look.textured) {
        s64 middleX = s64(x) * 16 + 8;
        u = r.uStart + f64(middleX - r.startX) / 16 * r.uAcross + f64(sampleY - r.startY) / 16 * r.uDown;
        v = r.vStart + f64(middleX - r.startX) / 16 * r.vAcross + f64(sampleY - r.startY) / 16 * r.vDown;
      }
      u32 fog = p.fog ? fogAmount(blend(r.fog[0], r.fog[1], r.fog[2])) : 255;
      shadeAs<Format>(look, job.linear, x, y, z, color, specular, u, v, fog);
    }
  }
}

template<u32 Format>
auto GE::rasterizeAs(const Job& job, s32 fromY, s32 toY) -> void {
  switch(job.kind) {
  case Job::Kind::Sprite: return spriteRows<Format>(job, fromY, toY);
  case Job::Kind::Triangle: return triangleRows<Format>(job, fromY, toY);
  case Job::Kind::Point: {
    auto& p = job.point;
    if(p.y < fromY || p.y > toY) return;
    return shadeAs<Format>(*job.look, job.linear, p.x, p.y, p.z, p.color, p.specular, p.u, p.v, p.fog);
  }
  }
}

//The job's pixels in rows fromY to toY (inclusive).
auto GE::rasterize(const Job& job, s32 fromY, s32 toY) -> void {
  switch(job.look->pixel.format) {
  case 0: return rasterizeAs<0>(job, fromY, toY);
  case 1: return rasterizeAs<1>(job, fromY, toY);
  case 2: return rasterizeAs<2>(job, fromY, toY);
  case 3: return rasterizeAs<3>(job, fromY, toY);
  }
}
