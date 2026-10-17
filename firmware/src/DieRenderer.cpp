#include "DieRenderer.h"

#include <Arduino.h>
#include <M5Unified.h>

#include <algorithm>
#include <cmath>

namespace pocketdice {
namespace {

constexpr float kPi = 3.14159265359f;
constexpr float kPhi = 1.61803398875f;
constexpr std::size_t kMaxVertices = 92;
constexpr std::size_t kMaxFaces = 100;

struct Vec3 {
  float x, y, z;
};

struct Mat3 {
  float m[3][3];
};

struct Face {
  std::uint8_t count{};
  std::uint8_t vertex[5]{};
};

struct Mesh {
  Vec3 vertices[kMaxVertices]{};
  Face faces[kMaxFaces]{};
  std::uint8_t vertexCount{};
  std::uint8_t faceCount{};
  float radius{1.0f};
};

struct Projected {
  float x, y, z;
};

struct VisibleFace {
  std::uint8_t index;
  float depth;
  float brightness;
};

constexpr Vec3 kIcoVertices[] = {
    {-1, kPhi, 0}, {1, kPhi, 0}, {-1, -kPhi, 0}, {1, -kPhi, 0},
    {0, -1, kPhi}, {0, 1, kPhi}, {0, -1, -kPhi}, {0, 1, -kPhi},
    {kPhi, 0, -1}, {kPhi, 0, 1}, {-kPhi, 0, -1}, {-kPhi, 0, 1},
};

constexpr std::uint8_t kIcoFaces[][3] = {
    {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
    {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
    {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
    {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
};

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 multiply(Vec3 vector, float scalar) { return {vector.x * scalar, vector.y * scalar, vector.z * scalar}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}
float length(Vec3 vector) { return sqrtf(dot(vector, vector)); }
Vec3 normalize(Vec3 vector) {
  const float magnitude = length(vector);
  return magnitude > 0.00001f ? multiply(vector, 1.0f / magnitude) : Vec3{0, 0, 1};
}

Mat3 identity() { return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; }

Vec3 transform(const Mat3& matrix, Vec3 vector) {
  return {
      matrix.m[0][0] * vector.x + matrix.m[0][1] * vector.y + matrix.m[0][2] * vector.z,
      matrix.m[1][0] * vector.x + matrix.m[1][1] * vector.y + matrix.m[1][2] * vector.z,
      matrix.m[2][0] * vector.x + matrix.m[2][1] * vector.y + matrix.m[2][2] * vector.z,
  };
}

Mat3 matrixMultiply(const Mat3& a, const Mat3& b) {
  Mat3 result{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      for (int index = 0; index < 3; ++index) {
        result.m[row][column] += a.m[row][index] * b.m[index][column];
      }
    }
  }
  return result;
}

Mat3 axisAngle(Vec3 axis, float angle) {
  axis = normalize(axis);
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float t = 1.0f - c;
  return {{{t * axis.x * axis.x + c, t * axis.x * axis.y - s * axis.z,
            t * axis.x * axis.z + s * axis.y},
           {t * axis.x * axis.y + s * axis.z, t * axis.y * axis.y + c,
            t * axis.y * axis.z - s * axis.x},
           {t * axis.x * axis.z - s * axis.y, t * axis.y * axis.z + s * axis.x,
            t * axis.z * axis.z + c}}};
}

Mat3 rotationBetween(Vec3 from, Vec3 to) {
  from = normalize(from);
  to = normalize(to);
  const float cosine = std::max(-1.0f, std::min(1.0f, dot(from, to)));
  Vec3 axis = cross(from, to);
  if (length(axis) < 0.0001f) {
    if (cosine > 0) return identity();
    axis = cross(from, fabsf(from.x) < 0.8f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
  }
  return axisAngle(axis, acosf(cosine));
}

void addVertex(Mesh& mesh, Vec3 vertex) { mesh.vertices[mesh.vertexCount++] = vertex; }

void addFace(Mesh& mesh, std::initializer_list<std::uint8_t> indices) {
  Face& face = mesh.faces[mesh.faceCount++];
  face.count = static_cast<std::uint8_t>(indices.size());
  std::size_t index = 0;
  for (const auto vertex : indices) face.vertex[index++] = vertex;
}

void buildIcosahedron(Mesh& mesh) {
  mesh = {};
  for (const auto& vertex : kIcoVertices) addVertex(mesh, vertex);
  for (const auto& face : kIcoFaces) addFace(mesh, {face[0], face[1], face[2]});
  mesh.radius = sqrtf(1.0f + kPhi * kPhi);
}

void buildDodecahedron(Mesh& mesh) {
  mesh = {};
  for (const auto& face : kIcoFaces) {
    addVertex(mesh, normalize(add(add(kIcoVertices[face[0]], kIcoVertices[face[1]]),
                                  kIcoVertices[face[2]])));
  }
  for (std::uint8_t vertexIndex = 0; vertexIndex < 12; ++vertexIndex) {
    std::uint8_t incident[5]{};
    float angles[5]{};
    std::uint8_t count = 0;
    const Vec3 normal = normalize(kIcoVertices[vertexIndex]);
    const Vec3 reference = normalize(cross(normal, fabsf(normal.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
    const Vec3 tangent = cross(normal, reference);
    for (std::uint8_t faceIndex = 0; faceIndex < 20; ++faceIndex) {
      const auto& face = kIcoFaces[faceIndex];
      if (face[0] == vertexIndex || face[1] == vertexIndex || face[2] == vertexIndex) {
        incident[count] = faceIndex;
        angles[count] = atan2f(dot(mesh.vertices[faceIndex], tangent),
                               dot(mesh.vertices[faceIndex], reference));
        ++count;
      }
    }
    for (std::uint8_t outer = 1; outer < count; ++outer) {
      const auto face = incident[outer];
      const auto angle = angles[outer];
      std::uint8_t inner = outer;
      while (inner > 0 && angles[inner - 1] > angle) {
        incident[inner] = incident[inner - 1];
        angles[inner] = angles[inner - 1];
        --inner;
      }
      incident[inner] = face;
      angles[inner] = angle;
    }
    Face& output = mesh.faces[mesh.faceCount++];
    output.count = count;
    for (std::uint8_t index = 0; index < count; ++index) output.vertex[index] = incident[index];
  }
  mesh.radius = 1.0f;
}

void buildD100(Mesh& mesh) {
  mesh = {};
  constexpr int segments = 10;
  constexpr int rings = 9;
  addVertex(mesh, {0, 1, 0});
  for (int ring = 0; ring < rings; ++ring) {
    const float latitude = kPi * (ring + 1) / (rings + 1);
    const float ringRadius = sinf(latitude);
    const float y = cosf(latitude);
    const float phase = ring % 2 ? kPi / segments : 0;
    for (int segment = 0; segment < segments; ++segment) {
      const float longitude = phase + segment * 2 * kPi / segments;
      addVertex(mesh, {cosf(longitude) * ringRadius, y, sinf(longitude) * ringRadius});
    }
  }
  const std::uint8_t bottom = mesh.vertexCount;
  addVertex(mesh, {0, -1, 0});
  for (int segment = 0; segment < segments; ++segment) {
    addFace(mesh, {0, static_cast<std::uint8_t>(1 + segment),
                   static_cast<std::uint8_t>(1 + (segment + 1) % segments)});
  }
  for (int ring = 0; ring < rings - 1; ++ring) {
    const int current = 1 + ring * segments;
    const int next = current + segments;
    for (int segment = 0; segment < segments; ++segment) {
      addFace(mesh, {static_cast<std::uint8_t>(current + segment),
                     static_cast<std::uint8_t>(next + segment),
                     static_cast<std::uint8_t>(next + (segment + 1) % segments),
                     static_cast<std::uint8_t>(current + (segment + 1) % segments)});
    }
  }
  const int last = 1 + (rings - 1) * segments;
  for (int segment = 0; segment < segments; ++segment) {
    addFace(mesh, {bottom, static_cast<std::uint8_t>(last + (segment + 1) % segments),
                   static_cast<std::uint8_t>(last + segment)});
  }
  mesh.radius = 1.0f;
}

void buildD10(Mesh& mesh) {
  constexpr float height = 0.62f;
  Vec3 sourceVertices[10]{};
  Face sourceFaces[12]{};
  for (int index = 0; index < 5; ++index) {
    const float topAngle = index * 2 * kPi / 5;
    const float bottomAngle = kPi / 5 + index * 2 * kPi / 5;
    sourceVertices[index] = {cosf(topAngle), height, sinf(topAngle)};
    sourceVertices[5 + index] = {cosf(bottomAngle), -height, sinf(bottomAngle)};
  }
  sourceFaces[0] = {5, {0, 1, 2, 3, 4}};
  sourceFaces[1] = {5, {9, 8, 7, 6, 5}};
  std::uint8_t sourceFaceCount = 2;
  for (std::uint8_t index = 0; index < 5; ++index) {
    sourceFaces[sourceFaceCount++] = {3, {index, static_cast<std::uint8_t>(5 + index),
                                           static_cast<std::uint8_t>((index + 1) % 5)}};
    sourceFaces[sourceFaceCount++] = {3, {index,
                                           static_cast<std::uint8_t>(5 + (index + 4) % 5),
                                           static_cast<std::uint8_t>(5 + index)}};
  }

  mesh = {};
  for (std::uint8_t faceIndex = 0; faceIndex < sourceFaceCount; ++faceIndex) {
    Vec3 center{0, 0, 0};
    const Face& face = sourceFaces[faceIndex];
    for (std::uint8_t index = 0; index < face.count; ++index) {
      center = add(center, sourceVertices[face.vertex[index]]);
    }
    addVertex(mesh, normalize(center));
  }
  for (std::uint8_t vertexIndex = 0; vertexIndex < 10; ++vertexIndex) {
    std::uint8_t incident[4]{};
    float angles[4]{};
    std::uint8_t count = 0;
    const Vec3 normal = normalize(sourceVertices[vertexIndex]);
    const Vec3 reference = normalize(cross(normal, fabsf(normal.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
    const Vec3 tangent = cross(normal, reference);
    for (std::uint8_t faceIndex = 0; faceIndex < sourceFaceCount; ++faceIndex) {
      const Face& face = sourceFaces[faceIndex];
      bool included = false;
      for (std::uint8_t index = 0; index < face.count; ++index) {
        if (face.vertex[index] == vertexIndex) included = true;
      }
      if (included) {
        incident[count] = faceIndex;
        angles[count] = atan2f(dot(mesh.vertices[faceIndex], tangent),
                               dot(mesh.vertices[faceIndex], reference));
        ++count;
      }
    }
    for (std::uint8_t outer = 1; outer < count; ++outer) {
      const auto face = incident[outer];
      const auto angle = angles[outer];
      std::uint8_t inner = outer;
      while (inner > 0 && angles[inner - 1] > angle) {
        incident[inner] = incident[inner - 1];
        angles[inner] = angles[inner - 1];
        --inner;
      }
      incident[inner] = face;
      angles[inner] = angle;
    }
    Face& output = mesh.faces[mesh.faceCount++];
    output.count = count;
    for (std::uint8_t index = 0; index < count; ++index) output.vertex[index] = incident[index];
  }
  constexpr float poleScale = 1.15f;
  for (std::uint8_t index = 0; index < mesh.vertexCount; ++index) {
    mesh.vertices[index].y *= poleScale;
  }
  mesh.radius = poleScale;
}

void buildMesh(std::uint16_t sides, Mesh& mesh) {
  mesh = {};
  if (sides == 4) {
    for (const auto& vertex : {Vec3{1, 1, 1}, Vec3{-1, -1, 1}, Vec3{-1, 1, -1}, Vec3{1, -1, -1}}) addVertex(mesh, vertex);
    addFace(mesh, {0, 2, 1}); addFace(mesh, {0, 1, 3}); addFace(mesh, {0, 3, 2}); addFace(mesh, {1, 2, 3});
    mesh.radius = sqrtf(3.0f);
  } else if (sides == 6) {
    for (const auto& vertex : {Vec3{-1,-1,-1}, Vec3{1,-1,-1}, Vec3{1,1,-1}, Vec3{-1,1,-1}, Vec3{-1,-1,1}, Vec3{1,-1,1}, Vec3{1,1,1}, Vec3{-1,1,1}}) addVertex(mesh, vertex);
    addFace(mesh,{0,1,2,3}); addFace(mesh,{4,7,6,5}); addFace(mesh,{0,4,5,1});
    addFace(mesh,{1,5,6,2}); addFace(mesh,{2,6,7,3}); addFace(mesh,{4,0,3,7});
    mesh.radius = sqrtf(3.0f);
  } else if (sides == 8) {
    for (const auto& vertex : {Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,1,0},Vec3{0,-1,0},Vec3{0,0,1},Vec3{0,0,-1}}) addVertex(mesh, vertex);
    addFace(mesh,{2,0,4}); addFace(mesh,{2,4,1}); addFace(mesh,{2,1,5}); addFace(mesh,{2,5,0});
    addFace(mesh,{3,4,0}); addFace(mesh,{3,1,4}); addFace(mesh,{3,5,1}); addFace(mesh,{3,0,5});
  } else if (sides == 10) {
    buildD10(mesh);
  } else if (sides == 12) {
    buildDodecahedron(mesh);
  } else if (sides == 100) {
    buildD100(mesh);
  } else {
    buildIcosahedron(mesh);
  }
}

Vec3 faceCenter(const Mesh& mesh, const Face& face) {
  Vec3 center{0, 0, 0};
  for (std::uint8_t index = 0; index < face.count; ++index) center = add(center, mesh.vertices[face.vertex[index]]);
  return multiply(center, 1.0f / face.count);
}

Vec3 faceNormal(const Mesh& mesh, const Face& face) {
  Vec3 normal = normalize(cross(subtract(mesh.vertices[face.vertex[1]], mesh.vertices[face.vertex[0]]),
                                subtract(mesh.vertices[face.vertex[2]], mesh.vertices[face.vertex[0]])));
  return dot(normal, faceCenter(mesh, face)) < 0 ? multiply(normal, -1) : normal;
}

Mat3 orientationFor(const Mesh& mesh, std::uint16_t value, bool settled, int frame) {
  if (!settled) {
    const Mat3 y = axisAngle({0, 1, 0}, 0.44f + frame * 0.73f);
    const Mat3 tumble = axisAngle(normalize({0.74f, 0.18f, 0.61f}), -0.31f + frame * 0.49f);
    return matrixMultiply(tumble, y);
  }
  const std::uint8_t selected = static_cast<std::uint8_t>((std::max<std::uint16_t>(1, value) - 1) % mesh.faceCount);
  const bool isD10 = mesh.faceCount == 10;
  const Vec3 target = normalize(isD10 ? Vec3{-0.16f, -0.1f, 0.98f} : Vec3{-0.28f, -0.3f, 0.91f});
  const Mat3 align = rotationBetween(faceNormal(mesh, mesh.faces[selected]), target);
  const Vec3 alignedPole = isD10 ? transform(align, {0, 1, 0}) : Vec3{0, 1, 0};
  const float twist = isD10 ? atan2f(alignedPole.x, alignedPole.y)
                            : fmodf(value * 137.508f, 360.0f) * kPi / 180.0f;
  return matrixMultiply(axisAngle(isD10 ? Vec3{0, 0, 1} : target, twist), align);
}

std::uint16_t shadedPurple(float brightness) {
  const auto channel = [brightness](int value) {
    return static_cast<std::uint8_t>(std::max(0.0f, std::min(255.0f, value * brightness)));
  };
  return M5.Display.color565(channel(124), channel(92), channel(255));
}

}  // namespace

void drawDie(std::uint16_t sides, std::uint16_t value, bool settled, int frame) {
  static Mesh mesh;
  buildMesh(sides, mesh);

  const int centerX = M5.Display.width() / 2 + (settled ? 0 : ((frame % 3) - 1) * 2);
  const int centerY = M5.Display.height() / 2 - 8 + (settled ? 0 : ((frame % 2) ? -2 : 1));
  const int radius = 39;
  const Mat3 orientation = orientationFor(mesh, value, settled, frame);
  constexpr float cameraZ = 3.7f;
  const Vec3 camera{0, 0, cameraZ};
  const Vec3 light = normalize({-0.65f, -0.82f, 1.35f});
  Projected projected[kMaxVertices]{};
  Vec3 transformed[kMaxVertices]{};

  M5.Display.fillScreen(M5.Display.color565(6, 11, 25));
  M5.Display.fillEllipse(centerX + 5, centerY + radius * 3 / 4, radius * 4 / 5, 8,
                         M5.Display.color565(2, 4, 10));
  M5.Display.fillEllipse(centerX + 3, centerY + radius * 3 / 4, radius * 3 / 5, 5,
                         M5.Display.color565(4, 6, 14));

  for (std::uint8_t index = 0; index < mesh.vertexCount; ++index) {
    transformed[index] = multiply(transform(orientation, mesh.vertices[index]), 1.0f / mesh.radius);
    // The d10 is centrally symmetric. Orthographic projection preserves that
    // symmetry on the small display instead of shortening the farther pole.
    const float perspective = sides == 10 ? 1.0f : cameraZ / (cameraZ - transformed[index].z);
    projected[index] = {centerX + transformed[index].x * radius * perspective,
                        centerY + transformed[index].y * radius * perspective,
                        transformed[index].z};
  }

  VisibleFace visible[kMaxFaces]{};
  std::uint8_t visibleCount = 0;
  const std::uint8_t selected = static_cast<std::uint8_t>((std::max<std::uint16_t>(1, value) - 1) % mesh.faceCount);
  for (std::uint8_t faceIndex = 0; faceIndex < mesh.faceCount; ++faceIndex) {
    const Face& face = mesh.faces[faceIndex];
    Vec3 normal = normalize(cross(subtract(transformed[face.vertex[1]], transformed[face.vertex[0]]),
                                  subtract(transformed[face.vertex[2]], transformed[face.vertex[0]])));
    Vec3 center{0, 0, 0};
    for (std::uint8_t index = 0; index < face.count; ++index) center = add(center, transformed[face.vertex[index]]);
    center = multiply(center, 1.0f / face.count);
    if (dot(normal, center) < 0) normal = multiply(normal, -1);
    const Vec3 view = normalize(subtract(camera, center));
    if (dot(normal, view) <= 0.015f) continue;
    const float diffuse = std::max(0.0f, dot(normal, light));
    const Vec3 halfway = normalize(add(light, view));
    const float specular = powf(std::max(0.0f, dot(normal, halfway)), 20.0f);
    visible[visibleCount++] = {faceIndex, center.z,
                               0.34f + diffuse * 0.68f + specular * 0.35f +
                                   (faceIndex == selected ? 0.08f : 0.0f)};
  }
  std::sort(visible, visible + visibleCount,
            [](const VisibleFace& a, const VisibleFace& b) { return a.depth < b.depth; });

  for (std::uint8_t order = 0; order < visibleCount; ++order) {
    const VisibleFace& item = visible[order];
    const Face& face = mesh.faces[item.index];
    const auto fill = shadedPurple(item.brightness);
    for (std::uint8_t index = 1; index + 1 < face.count; ++index) {
      const Projected& a = projected[face.vertex[0]];
      const Projected& b = projected[face.vertex[index]];
      const Projected& c = projected[face.vertex[index + 1]];
      M5.Display.fillTriangle(lroundf(a.x), lroundf(a.y), lroundf(b.x), lroundf(b.y),
                              lroundf(c.x), lroundf(c.y), fill);
    }
    const auto edge = item.index == selected ? M5.Display.color565(238, 232, 255)
                                             : M5.Display.color565(154, 137, 222);
    for (std::uint8_t index = 0; index < face.count; ++index) {
      const Projected& a = projected[face.vertex[index]];
      const Projected& b = projected[face.vertex[(index + 1) % face.count]];
      M5.Display.drawLine(lroundf(a.x), lroundf(a.y), lroundf(b.x), lroundf(b.y), edge);
    }
  }

  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_BLACK);
  M5.Display.setTextSize(value >= 100 ? 2 : 3);
  M5.Display.drawNumber(value, centerX - 1, centerY);
  M5.Display.drawNumber(value, centerX + 1, centerY);
  M5.Display.drawNumber(value, centerX, centerY - 1);
  M5.Display.drawNumber(value, centerX, centerY + 1);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawNumber(value, centerX, centerY);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY);
  M5.Display.drawString(String("d") + sides, centerX, M5.Display.height() - 8);
}

}  // namespace pocketdice
