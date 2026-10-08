#ifndef CART_H
#define CART_H

#include "geometry.h"
#include "item.h"
#include "wall.h"
#include <raylib.h>
#include <stdarg.h>

#define COLLISION_ITERATIONS 8
#define RESTITUTION 0.20f
#define WALL_FRICTION 0.35f
#define CART_PUSH_FORCE 45.0f
#define MAX_ANGULAR_VEL 20.0f

#define ARM_WIDTH 0.08f
#define SHOUDLER_WIDTH 0.08f

typedef struct {
  ColliPoly collider;

  Vector2 pos;
  Vector2 vel;

  float rot;
  float ang_vel;

  float mass;
  float mom_inertia;

  float base_mass;
  float base_mom_inertia;

  Color color;
  Color sec_color;

  ItemArr items;

  Vector2 driver_pos;
  float driver_radius;
  Color driver_color;

  bool standard;
  float max_vrt_dist;
} Cart;

Cart cart_new_va(Color color, Color sec_color, float mass_density,
                 Vector2 driver_pos, float driver_radius, Color driver_color,
                 bool standard, int num_vertices, va_list args);
Cart cart_new_old(Color color, Color sec_color, float mass_density,
                  Vector2 driver_pos, float driver_radius, Color driver_color,
                  bool standard, int num_vertices, ...);
Cart cart_new(Color color, Color sec_color, Color driver_color, Vector2 pos);
Cart cart_new_goofy(Color color, Color sec_color, Color driver_color,
                    Vector2 pos, int num_vert, ...);
void cart_free(Cart *cart);

ColliPoly cart_get_world_collider(const Cart *cart);
void cart_recalculate_mass_inertia(Cart *cart);

void cart_apply_force(Cart *cart, Vector2 rel_pos, Vector2 force, float delta);
void cart_apply_force_rotated(Cart *cart, Vector2 rel_pos, Vector2 force,
                              float delta);
void cart_apply_impulse(Cart *cart, Vector2 rel_pos, Vector2 impulse);
void cart_apply_impulse_rotated(Cart *cart, Vector2 rel_pos, Vector2 impulse);

void cart_tick(Cart *cart, WallArr walls, int num_walls, float delta);
bool cart_collide_cart(Cart *c1, Cart *c2, Vector2 *out_bump_impulse,
                       float *out_bump_ang);
Item cart_consume_item(Cart *cart, ItemArr *items, int index);
void cart_draw(Cart *cart, int pick, Item *pick_item, Vector2 topleft,
               Vector2 botright);
void cart_draw_hands(Cart *cart, int pick, Item *pick_item);

#endif // CART_H
