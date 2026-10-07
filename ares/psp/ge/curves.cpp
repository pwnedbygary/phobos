//Curved surfaces: BEZIER and SPLINE draw a smooth surface from a grid of control points (a "patch"), which the GE
//cuts into small flat pieces by itself and draws as triangles, lines or points. The points pull the surface toward
//them, like the handles of a drawing program's curves, and the GE works out the vertices in between; from there on
//they're drawn as PRIM's are (the transform, lighting, the pixels: draw.cpp's drawVertices()).
//
//The control points are read as PRIM reads vertices (the vertex type, its indices, the vertex or index address moving
//on past them), ucount to a row, along u, and vcount rows, along v: point (i, j) is row j's i-th. Each part of a
//vertex is worked out from the 4x4 points around it, each point's weighted:
//
//  - BEZIER (pspsdk's sceGuDrawBezier): every 4x4 points make a cubic Bezier patch, through the corner points and
//    pulled toward the others; neighbouring patches share a row or column of points, so ucount is 3N + 1 for N
//    patches along u. At t (0 to 1) across a patch, the four points along a row weigh (1 - t)³, 3t(1 - t)²,
//    3t²(1 - t) and t³. Points past the last whole patch are left out, and fewer than 4 draw nothing: pspautotests'
//    gpu/primitives/bezier, recorded on a PSP, drew a 4x5 grid's first four rows, a 4x8's first seven, a 4x2 not.
//  - SPLINE (sceGuDrawSpline): a cubic B-spline through ucount x vcount points, 4 or more each way. Every four along
//    a row make a piece of the curve and the next piece takes the next four, so the curve flows past the points,
//    smooth where pieces meet (its knots, evenly spaced). SPLINE's bits 16-17 (along u) and 18-19 (along v) say how
//    each end is cut (pspsdk's doc/commands.txt): bit 0 the first end, bit 1 the last, open (the curve starts on the
//    end point, its first knots stacked on the curve's start as a Bezier's are) or closed (the knots go on evenly
//    past the end, so the curve starts short of it, as if the points went on). gpu/primitives/spline recorded points
//    (a, a, b, b) drawing a curve from a to b with both ends open, from (5a + b) / 6 to (a + 5b) / 6 with both
//    closed, from a to (a + 3b) / 4 with only the first open and from (3a + b) / 4 to b with only the last; and
//    round 3 of tools/psp-measure drew a 4x4 spline with both ends open exactly as the same Bezier patch.
//  - PATCH_DIVISION: how many pieces each patch (BEZIER) or each piece of the curve (SPLINE) is cut into along u
//    (bits 0-7) and v (bits 8-15), evenly in t; 0 cuts as 1 does (gpu/primitives/bezier: two triangles). So vertices
//    are at t = k / divisions, neighbours sharing the vertices on their common edge. (The colors down a 4x5 Bezier
//    and a 4x5 spline, in those pictures, step where 4 divisions of each patch and of each piece of curve put them.)
//  - PATCH_PRIMITIVE (bits 0-1), as gpu/primitives/bezier and spline recorded: 0 triangles, the vertices' rows two at
//    a time as triangle strips along u, (u0, v0), (u0, v1), (u1, v0), (u1, v1)... (so with flat shading a quad's top
//    left triangle takes its top right vertex's color); 1 lines, those strips' vertices as line strips (a line down
//    each column, and up to the next column's top); 2 and 3 points, a vertex each.
//  - Colors are worked out per channel and rounded up: a channel at 223.125 is 224 (gpu/primitives/bezier's flat
//    shaded patches). Rounding up also fits round 3's smooth patches far better than to the nearest, down, or than
//    keeping fractions: 319 to 561 pixels a level apart in each channel of bezier-curved and bezier-divide-8 (where
//    the core's blending across triangles rounds a little differently from the PSP's, as round 3's color ramps
//    found), against 11,000 to 37,700 otherwise. A value within PatchSlack above a whole level counts as that level.
//  - Without texture coordinates in the vertex type, they're made up: u from 0 to 1 across the surface, v down it
//    (chosen, unmeasured). Without a normal, where one is wanted (lighting, environment mapping, the texture matrix
//    from the normal), it's made from the surface's slopes, the cross product of the slope along u and the slope
//    along v (pspautotests' gpu/exact/curves names normals from the tangents), pointing out of the front face
//    PATCH_FACING says: 0 (pspsdk's GU_CW) the side from which a strip's first triangle, (u0, v0), (u0, v1),
//    (u1, v0), runs clockwise; 1 the other (chosen: that program records only checksums of such lighting).
//  - The triangles are culled as any are (CULL_FACE_ENABLE and CULL), every other one of a strip the other way round.
//    What PATCH_CULL_ENABLE does isn't known: it's noted, not emulated.
//
//Positions are worked out in doubles, then transformed as any vertex is: that draws exactly the pixels a PSP covered
//in all of round 3's Bezier pictures and pspautotests' two, and all but 4 edge pixels of round 3's spline with both
//ends closed. gpu/exact/curves checks the arithmetic's last bits by checksums of points placed on the edges of
//sixteenths of a pixel and of depths, which the GE's own fixed-point arithmetic decides: most aren't met (part 33 of
//docs/psp-core.md), and tools/psp-measure's round 4 records the vertices themselves.
//
//Malformed surfaces are bounded: one cut into more than PatchBudget vertices (huge counts and divisions) isn't drawn
//(noted), a guard of Phobos's own, past anything the GU library allows; control points that aren't numbers make
//vertices that aren't, which the GE doesn't draw (transform.cpp) or holds to its range (draw.cpp).

static constexpr u64 PatchBudget = 1 << 17;    //the most vertices a curved surface is cut into
static constexpr u64 PatchKept = 1 << 12;      //the most vertices patchVertices keeps room for between surfaces
static constexpr f64 PatchSlack = 1.0 / 4096;  //how far above a whole level a color may be and still count as it

//Where a surface's vertices fall along one way across it (u or v), each from four control points of its rows (or
//columns): the first of them, their weights, and the weights of their slope there (for a normal).
struct PatchStep {
  u32 first;
  f64 weight[4], slope[4];
  f64 along;  //from 0 at the surface's first vertex to 1 at its last (made-up texture coordinates)
};

//A cubic Bezier's weights at t, and their slopes.
static auto bezierStep(f64 t, PatchStep& step) -> void {
  f64 s = 1 - t;
  step.weight[0] = s * s * s, step.weight[1] = 3 * t * s * s;
  step.weight[2] = 3 * t * t * s, step.weight[3] = t * t * t;
  step.slope[0] = -3 * s * s, step.slope[1] = 3 * s * s - 6 * t * s;
  step.slope[2] = 6 * t * s - 3 * t * t, step.slope[3] = 3 * t * t;
}

//A cubic B-spline's weights at t, between knots[span] and knots[span + 1] (the four points from span - 3 on), and
//their slopes: the Cox-de Boor recursion, from step functions (degree 0) up to the cubic.
static auto splineStep(const f64* knots, u32 span, f64 t, PatchStep& step) -> void {
  f64 basis[4][4] = {};  //basis[degree][k]: point span - degree + k's weight, of that degree
  basis[0][0] = 1;
  for(u32 degree = 1; degree <= 3; degree++) {
    for(u32 k = 0; k <= degree; k++) {
      u32 point = span - degree + k;
      f64 value = 0;
      if(k > 0) {  //rising, from the same point's weight of one degree less
        f64 width = knots[point + degree] - knots[point];
        if(width > 0) value += (t - knots[point]) / width * basis[degree - 1][k - 1];
      }
      if(k < degree) {  //falling, from the next point's
        f64 width = knots[point + degree + 1] - knots[point + 1];
        if(width > 0) value += (knots[point + degree + 1] - t) / width * basis[degree - 1][k];
      }
      basis[degree][k] = value;
    }
  }
  for(u32 k = 0; k < 4; k++) {
    step.weight[k] = basis[3][k];
    //a cubic weight's slope, from the quadratic ones: 3 (N[k] / width - N[k + 1] / next width)
    u32 point = span - 3 + k;
    f64 slope = 0, width = knots[point + 3] - knots[point], next = knots[point + 4] - knots[point + 1];
    if(k > 0 && width > 0) slope += 3 * basis[2][k - 1] / width;
    if(k < 3 && next > 0) slope -= 3 * basis[2][k] / next;
    step.slope[k] = slope;
  }
}

//The steps along one way across a surface of count control points: BEZIER's patches, or SPLINE's pieces with these
//ends (bit 0 the first open, bit 1 the last), each cut into divisions. None if there are too few points for one.
//How many vertices one way across a surface is cut into (as patchSteps makes them): none for fewer than four
//control points, else divisions per piece and one more for the far edge.
static auto patchColumns(bool spline, u32 count, u32 divisions) -> u64 {
  if(count < 4) return 0;
  u64 pieces = spline ? count - 3 : (count - 1) / 3;
  return pieces * divisions + 1;
}

static auto patchSteps(bool spline, u32 count, u32 ends, u32 divisions, std::vector<PatchStep>& steps) -> void {
  steps.clear();
  if(count < 4) return;
  u32 pieces = spline ? count - 3 : (count - 1) / 3;
  u32 total = pieces * divisions;
  f64 knots[255 + 4];
  if(spline) {
    for(u32 n = 0; n < count + 4; n++) knots[n] = f64(n) - 3;  //evenly spaced: -3, -2, -1, 0, 1, ..., count
    if(ends & 1) knots[0] = knots[1] = knots[2] = 0;           //open: stacked on the curve's start (0)
    if(ends & 2) knots[count + 1] = knots[count + 2] = knots[count + 3] = count - 3;  //and on its end
  }
  steps.resize(total + 1);
  for(u32 n = 0; n <= total; n++) {
    auto& step = steps[n];
    u32 piece = std::min(n / divisions, pieces - 1);
    f64 t = f64(n - piece * divisions) / divisions;
    if(spline) step.first = piece, splineStep(knots, piece + 3, piece + t, step);
    else step.first = piece * 3, bezierStep(t, step);
    step.along = f64(n) / total;
  }
}

auto GE::patch(bool spline, u32 argument) -> void {
  if(!drawing.deferring) settle();  //(as primitive())
  auto format = vertexFormat();
  u32 ucount = argument & 0xff, vcount = argument >> 8 & 0xff;
  readVertices(ucount * vcount, format);
  if(!format.positionFormat) return;
  u32 divisionsU = std::max(commands[PatchDivision] & 0xff, 1u);
  u32 divisionsV = std::max(commands[PatchDivision] >> 8 & 0xff, 1u);
  //The budget is checked before anything is worked out, so a surface past it allocates nothing.
  u64 columns = patchColumns(spline, ucount, divisionsU), rows = patchColumns(spline, vcount, divisionsV);
  if(!columns || !rows) return;
  if(columns * rows > PatchBudget) return note("a curved surface cut into more vertices than the core draws");
  std::vector<PatchStep> across, down;
  patchSteps(spline, ucount, spline ? argument >> 16 & 3 : 0, divisionsU, across);
  patchSteps(spline, vcount, spline ? argument >> 18 & 3 : 0, divisionsV, down);
  if(commands[PatchCullEnable] & 1) note("PATCH_CULL_ENABLE isn't emulated");

  //What the vertices need: the parts the vertex type has, and a normal made from the slopes where one is wanted
  //but the vertex type has none.
  u32 mapMode = commands[TextureMapMode] & 3, mapSource = commands[TextureMapMode] >> 8 & 3;
  bool wantsNormal = !format.through &&
                     ((commands[LightingEnable] & 1) || mapMode == 2 || (mapMode == 1 && mapSource >= 2));
  bool normals = format.normalFormat != 0, slopes = !normals && wantsNormal;
  bool textured = format.textureFormat != 0, weighted = format.weightFormat != 0;
  bool clockwise = !(commands[PatchFacing] & 1);
  auto& points = primitiveVertices;
  std::vector<Vertex> grid(columns * rows);
  for(u32 r = 0; r < rows; r++) {
    auto& v = down[r];
    for(u32 c = 0; c < columns; c++) {
      auto& u = across[c];
      f64 position[3] = {}, color[4] = {}, texture[2] = {}, normal[3] = {}, weights[8] = {};
      f64 slopeU[3] = {}, slopeV[3] = {};
      for(u32 j = 0; j < 4; j++) {
        for(u32 i = 0; i < 4; i++) {
          auto& point = points[(v.first + j) * ucount + u.first + i];
          f64 w = u.weight[i] * v.weight[j];
          f64 xyz[3] = {point.x, point.y, point.z};
          for(u32 k = 0; k < 3; k++) position[k] += w * xyz[k];
          for(u32 k = 0; k < 4; k++) color[k] += w * f64(point.color >> k * 8 & 0xff);
          if(textured) texture[0] += w * point.u, texture[1] += w * point.v;
          if(normals) for(u32 k = 0; k < 3; k++) normal[k] += w * point.normal[k];
          if(weighted) for(u32 k = 0; k < format.weights; k++) weights[k] += w * point.weights[k];
          if(slopes) {
            for(u32 k = 0; k < 3; k++) {
              slopeU[k] += u.slope[i] * v.weight[j] * xyz[k];
              slopeV[k] += u.weight[i] * v.slope[j] * xyz[k];
            }
          }
        }
      }
      Vertex& vertex = grid[r * columns + c];
      vertex.x = position[0], vertex.y = position[1], vertex.z = position[2];
      for(u32 k = 0; k < 4; k++) {
        vertex.color |= u32(std::clamp(std::ceil(color[k] - PatchSlack), 0.0, 255.0)) << k * 8;
      }
      if(textured) vertex.u = texture[0], vertex.v = texture[1];
      else vertex.u = u.along, vertex.v = v.along;
      if(normals) for(u32 k = 0; k < 3; k++) vertex.normal[k] = normal[k];
      if(slopes) {
        f64 cross[3] = {slopeU[1] * slopeV[2] - slopeU[2] * slopeV[1], slopeU[2] * slopeV[0] - slopeU[0] * slopeV[2],
                        slopeU[0] * slopeV[1] - slopeU[1] * slopeV[0]};
        for(u32 k = 0; k < 3; k++) vertex.normal[k] = clockwise ? cross[k] : -cross[k];
      }
      for(u32 k = 0; k < 8; k++) vertex.weights[k] = weights[k];
    }
  }

  //Drawn as PATCH_PRIMITIVE says: the rows two at a time as strips (triangles or lines), or each vertex a point.
  u32 kind = commands[PatchPrimitive] & 3;
  auto& vertices = patchVertices;
  if(kind >= 2) {
    vertices = std::move(grid);
    drawVertices(Points, format, vertices, vertices.size());
  } else {
    vertices.clear();
    for(u32 r = 0; r + 1 < rows; r++) {
      for(u32 c = 0; c < columns; c++) {
        vertices.push_back(grid[r * columns + c]);
        vertices.push_back(grid[(r + 1) * columns + c]);
      }
    }
    drawVertices(kind == 0 ? TriangleStrip : LineStrip, format, vertices, columns * 2);
  }
  //A huge surface's vertices (up to tens of MB at the budget) aren't kept once drawn: drawVertices() is done with
  //them (what it defers, it copies), and only room for an ordinary surface's stays for the next.
  if(vertices.capacity() > PatchKept) std::vector<Vertex>().swap(vertices);
}
