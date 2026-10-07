#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <float.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <stdarg.h>
#include <stdbool.h>

float fsignf(float x);
void perror_exit(const char *msg);

typedef struct {
  int x;
  int y;
} IntVector2;

typedef struct {
  int x;
  int y;
  int edge;
} IntEdge;

typedef struct {
  Vector2 *items;
  int len;
} ColliPoly;

typedef struct {
  bool collided;
  Vector2 normal; // Collision normal (points from B to A, to push A out of B)
  float depth;    // Penetration depth
  Vector2 mtv;    // Minimum Translation Vector (normal * depth)
} CollisionInfo;

ColliPoly collipoly_new(int num_vertices, ...);
void collipoly_free(ColliPoly *poly);
void collipoly_project(ColliPoly polygon, Vector2 axis, float *min, float *max);
bool collipoly_collide(ColliPoly a, ColliPoly b);
float collipoly_area(ColliPoly polygon);
Vector2 collipoly_center(ColliPoly polygon, float area);
float collipoly_mom_inertia(ColliPoly polygon);
bool collipoly_contains(ColliPoly polygon, Vector2 pnt);
Vector2 collipoly_random_pnt(ColliPoly polygon);

CollisionInfo get_polygon_collision_info(ColliPoly polyA, ColliPoly polyB);
Vector2 find_contact_point_on_cart(const ColliPoly *cart_world_poly,
                                   Vector2 collision_normal_from_wall_to_cart);

#endif // GEOMETRY_H
