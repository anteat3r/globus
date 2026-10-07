#include "geometry.h"
#include <stdio.h>
#include <stdlib.h>

float fsignf(float x) {
  if (x > 0)
    return 1.0f;
  if (x < 0)
    return -1.0f;
  return 0.0f;
}

void perror_exit(const char *msg) {
  perror(msg);
  exit(1);
}

ColliPoly collipoly_new(int num_vertices, ...) {
  va_list args;
  va_start(args, num_vertices);

  ColliPoly res;
  res.len = num_vertices;
  res.items = malloc(num_vertices * sizeof(Vector2));
  if (!res.items)
    perror_exit("collipoly_new malloc");

  for (int i = 0; i < num_vertices; i++)
    res.items[i] = va_arg(args, Vector2);

  va_end(args);
  return res;
}

void collipoly_free(ColliPoly *poly) {
  if (poly && poly->items) {
    free(poly->items);
    poly->items = NULL;
  }
}

void collipoly_project(ColliPoly polygon, Vector2 axis, float *min,
                       float *max) {
  *min = Vector2DotProduct(polygon.items[0], axis);
  *max = *min;

  for (int i = 1; i < polygon.len; i++) {
    float projection = Vector2DotProduct(polygon.items[i], axis);
    if (projection < *min) {
      *min = projection;
    }
    if (projection > *max) {
      *max = projection;
    }
  }
}

bool collipoly_collide(ColliPoly a, ColliPoly b) {
  if (a.items == NULL || b.items == NULL || a.len < 3 || b.len < 3)
    return false;

  for (int i = 0; i < a.len; i++) {
    Vector2 p1 = a.items[i];
    Vector2 p2 = a.items[(i + 1) % a.len];
    Vector2 edge = Vector2Subtract(p2, p1);
    Vector2 axis = Vector2Normalize((Vector2){-edge.y, edge.x});

    float minA, maxA, minB, maxB;
    collipoly_project(a, axis, &minA, &maxA);
    collipoly_project(b, axis, &minB, &maxB);

    if (maxA < minB || maxB < minA)
      return false;
  }

  for (int i = 0; i < b.len; i++) {
    Vector2 p1 = b.items[i];
    Vector2 p2 = b.items[(i + 1) % b.len];
    Vector2 edge = Vector2Subtract(p2, p1);
    Vector2 axis = Vector2Normalize((Vector2){-edge.y, edge.x});

    float minA, maxA, minB, maxB;
    collipoly_project(a, axis, &minA, &maxA);
    collipoly_project(b, axis, &minB, &maxB);

    if (maxA < minB || maxB < minA)
      return false;
  }

  return true;
}

float collipoly_area(ColliPoly polygon) {
  float sum = 0.0f;
  for (int i = 0; i < polygon.len; i++) {
    Vector2 v = polygon.items[i];
    Vector2 w = polygon.items[(i + 1) % polygon.len];
    sum += v.x * w.y - w.x * v.y;
  }
  return sum / 2;
}

Vector2 collipoly_center(ColliPoly polygon, float area) {
  Vector2 res = {0.0f, 0.0f};
  for (int i = 0; i < polygon.len; i++) {
    Vector2 v = polygon.items[i];
    Vector2 w = polygon.items[(i + 1) % polygon.len];
    float idk = (v.x * w.y - w.x * v.y);
    res.x += (v.x + w.x) * idk;
    res.y += (v.y + w.y) * idk;
  }
  res.x /= 6 * area;
  res.y /= 6 * area;
  return res;
}

float collipoly_mom_inertia(ColliPoly polygon) {
  float sum = 0.0f;
  for (int i = 0; i < polygon.len; i++) {
    Vector2 v = polygon.items[i];
    Vector2 w = polygon.items[(i + 1) % polygon.len];
    sum += (v.y * v.y + v.y * w.y + w.y * w.y + v.x * v.x + v.x * w.x +
            w.x * w.x) *
           (v.x * w.y - w.x * v.y);
  }
  return sum / 12;
}

bool collipoly_contains(ColliPoly polygon, Vector2 pnt) {
  int i, j, c = 0;
  for (i = 0, j = polygon.len - 1; i < polygon.len; j = i++) {
    Vector2 v = polygon.items[i];
    Vector2 w = polygon.items[j];
    if (((v.y > pnt.y) != (w.y > pnt.y)) &&
        (pnt.x < (w.x - v.x) * (pnt.y - v.y) / (w.y - v.y) + v.y))
      c = !c;
  }
  return c;
}

Vector2 collipoly_random_pnt(ColliPoly polygon) {
  Vector2 center = collipoly_center(polygon, collipoly_area(polygon));

  float max_dist = 0.0f;
  for (int i = 0; i < polygon.len; i++) {
    Vector2 pnt = polygon.items[i];
    float dist = Vector2Distance(center, pnt);
    if (dist > max_dist)
      max_dist = dist;
  }

  Vector2 res = {0.0f, 0.0f};
  while (true) {
    res.x = (float)rand() / (float)(RAND_MAX / max_dist / 2) - max_dist;
    res.y = (float)rand() / (float)(RAND_MAX / max_dist / 2) - max_dist;
    if (collipoly_contains(polygon, res))
      break;
  }
  return res;
}

CollisionInfo get_polygon_collision_info(ColliPoly polyA, ColliPoly polyB) {
  CollisionInfo info = {
      .collided = false, .depth = FLT_MAX, .normal = {0, 0}, .mtv = {0, 0}};

  if (polyA.items == NULL || polyB.items == NULL || polyA.len < 3 ||
      polyB.len < 3) {
    return info;
  }

  // Check axes of Polygon A
  for (int i = 0; i < polyA.len; i++) {
    Vector2 p1 = polyA.items[i];
    Vector2 p2 = polyA.items[(i + 1) % polyA.len];
    Vector2 edge = Vector2Subtract(p2, p1);
    Vector2 axis = Vector2Normalize((Vector2){-edge.y, edge.x});

    float minA, maxA, minB, maxB;
    collipoly_project(polyA, axis, &minA, &maxA);
    collipoly_project(polyB, axis, &minB, &maxB);

    if (maxA < minB || maxB < minA) {
      info.collided = false;
      return info;
    }

    float overlap = fminf(maxA, maxB) - fmaxf(minA, minB);
    if (overlap < info.depth) {
      info.depth = overlap;
      info.normal = axis;
    }
  }

  // Check axes of Polygon B
  for (int i = 0; i < polyB.len; i++) {
    Vector2 p1 = polyB.items[i];
    Vector2 p2 = polyB.items[(i + 1) % polyB.len];
    Vector2 edge = Vector2Subtract(p2, p1);
    Vector2 axis = Vector2Normalize((Vector2){-edge.y, edge.x});

    float minA, maxA, minB, maxB;
    collipoly_project(polyA, axis, &minA, &maxA);
    collipoly_project(polyB, axis, &minB, &maxB);

    if (maxA < minB || maxB < minA) {
      info.collided = false;
      return info;
    }

    float overlap = fminf(maxA, maxB) - fmaxf(minA, minB);
    if (overlap < info.depth) {
      info.depth = overlap;
      info.normal = axis;
    }
  }

  info.collided = true;

  // Ensure normal points from polyB to polyA
  Vector2 centerA = collipoly_center(polyA, collipoly_area(polyA));
  Vector2 centerB = collipoly_center(polyB, collipoly_area(polyB));
  Vector2 dir_B_to_A = Vector2Subtract(centerA, centerB);

  if (Vector2DotProduct(dir_B_to_A, info.normal) < 0.0f) {
    info.normal = Vector2Negate(info.normal);
  }

  info.mtv = Vector2Scale(info.normal, info.depth);
  return info;
}

Vector2 find_contact_point_on_cart(const ColliPoly *cart_world_poly,
                                   Vector2 collision_normal_from_wall_to_cart) {
  if (cart_world_poly->len == 0)
    return (Vector2){0, 0};

  Vector2 contact_point = cart_world_poly->items[0];
  float max_proj =
      Vector2DotProduct(contact_point, collision_normal_from_wall_to_cart);

  for (int i = 1; i < cart_world_poly->len; i++) {
    float proj = Vector2DotProduct(cart_world_poly->items[i],
                                   collision_normal_from_wall_to_cart);
    if (proj > max_proj) {
      max_proj = proj;
      contact_point = cart_world_poly->items[i];
    }
  }
  return contact_point;
}
