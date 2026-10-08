#include "cart.h"
#include <math.h>
#include <stdlib.h>

Cart cart_new_va(Color color, Color sec_color, float mass_density,
                 Vector2 driver_pos, float driver_radius, Color driver_color,
                 bool standard, int num_vertices, va_list args) {
  (void)mass_density;
  Cart res = {
      .collider =
          (ColliPoly){
              .len = num_vertices,
          },
      .color = color,
      .sec_color = sec_color,
      .rot = 0.0f,
      .ang_vel = 0.0f,
      .vel = Vector2Zero(),
      .items = itemarr_new(),
      .driver_pos = driver_pos,
      .driver_radius = driver_radius,
      .driver_color = driver_color,
      .standard = standard,
      .max_vrt_dist = 0.0f,
  };

  res.collider.items = malloc(num_vertices * sizeof(Vector2));
  if (!res.collider.items)
    perror_exit("cart_new malloc");

  for (int i = 0; i < num_vertices; i++)
    res.collider.items[i] = va_arg(args, Vector2);

  float area = collipoly_area(res.collider);
  res.mass = 12.0f; // Realistic empty shopping cart mass ~12 kg
  res.base_mass = res.mass;

  res.pos = collipoly_center(res.collider, area);

  for (int i = 0; i < num_vertices; i++) {
    res.collider.items[i].x -= res.pos.x;
    res.collider.items[i].y -= res.pos.y;
    float dist = Vector2Length(res.collider.items[i]);
    if (dist > res.max_vrt_dist)
      res.max_vrt_dist = dist;
  }

  res.mom_inertia = 0.6f; // Realistic yaw moment of inertia ~0.6 kg*m^2
  res.base_mom_inertia = res.mom_inertia;

  return res;
}

Cart cart_new_old(Color color, Color sec_color, float mass_density,
                  Vector2 driver_pos, float driver_radius, Color driver_color,
                  bool standard, int num_vertices, ...) {
  va_list args;
  va_start(args, num_vertices);
  Cart res =
      cart_new_va(color, sec_color, mass_density, driver_pos, driver_radius,
                  driver_color, standard, num_vertices, args);
  va_end(args);
  return res;
}

Cart cart_new(Color color, Color sec_color, Color driver_color, Vector2 pos) {
  return cart_new_old(color, sec_color, 3,
                      Vector2Add(pos, (Vector2){0.0f, 0.3f}), .1, driver_color,
                      true, 4, Vector2Add(pos, (Vector2){-0.15f, -0.27f}),
                      Vector2Add(pos, (Vector2){-0.15f, 0.27f}),
                      Vector2Add(pos, (Vector2){0.15f, 0.27f}),
                      Vector2Add(pos, (Vector2){0.15f, -0.27f}));
}

Cart cart_new_goofy(Color color, Color sec_color, Color driver_color,
                    Vector2 pos, int num_vert, ...) {
  va_list args;
  va_start(args, num_vert);
  Cart res =
      cart_new_va(color, sec_color, 3, Vector2Add(pos, (Vector2){0.0f, 0.35f}),
                  .1, driver_color, true, num_vert, args);
  va_end(args);
  return res;
}

void cart_free(Cart *cart) {
  collipoly_free(&cart->collider);
  itemarr_free_all(&cart->items);
}

ColliPoly cart_get_world_collider(const Cart *cart) {
  ColliPoly world_collider;
  world_collider.len = cart->collider.len;
  world_collider.items =
      (Vector2 *)malloc(world_collider.len * sizeof(Vector2));
  if (!world_collider.items) {
    perror_exit("cart_get_world_collider malloc failed");
  }

  float s = sinf(cart->rot);
  float c = cosf(cart->rot);

  for (int i = 0; i < cart->collider.len; i++) {
    Vector2 local_v = cart->collider.items[i];
    float rotated_x = local_v.x * c - local_v.y * s;
    float rotated_y = local_v.x * s + local_v.y * c;
    world_collider.items[i] =
        (Vector2){cart->pos.x + rotated_x, cart->pos.y + rotated_y};
  }
  return world_collider;
}

void cart_recalculate_mass_inertia(Cart *cart) {
  float total_mass = cart->base_mass;
  float total_inertia = cart->base_mom_inertia;

  for (int i = 0; i < cart->items.len; i++) {
    float item_m =
        cart->items.items[i].mass > 0.001f ? cart->items.items[i].mass : 0.2f;
    total_mass += item_m;
    float dist_sqr = Vector2LengthSqr(cart->items.items[i].pos);
    total_inertia += item_m * (0.02f + dist_sqr);
  }

  cart->mass = total_mass;
  cart->mom_inertia = total_inertia;
}

void cart_apply_force(Cart *cart, Vector2 rel_pos, Vector2 force, float delta) {
  if (cart->mass <= EPSILON || delta <= 0.0f)
    return;
  Vector2 dv = Vector2Scale(force, (1.0f / cart->mass) * delta);
  cart->vel = Vector2Add(cart->vel, dv);

  if (cart->mom_inertia <= EPSILON)
    return;
  float torque = (rel_pos.x * force.y) - (rel_pos.y * force.x);
  cart->ang_vel += (torque / cart->mom_inertia) * delta;
}

void cart_apply_force_rotated(Cart *cart, Vector2 rel_pos, Vector2 force,
                              float delta) {
  cart_apply_force(cart, Vector2Rotate(rel_pos, cart->rot),
                   Vector2Rotate(force, cart->rot), delta);
}

void cart_apply_impulse(Cart *cart, Vector2 rel_pos, Vector2 impulse) {
  if (cart->mass <= EPSILON)
    return;
  Vector2 dv = Vector2Scale(impulse, 1.0f / cart->mass);
  cart->vel = Vector2Add(cart->vel, dv);

  if (cart->mom_inertia <= EPSILON)
    return;
  float angular_impulse = (rel_pos.x * impulse.y) - (rel_pos.y * impulse.x);
  cart->ang_vel += angular_impulse / cart->mom_inertia;
}

void cart_apply_impulse_rotated(Cart *cart, Vector2 rel_pos, Vector2 impulse) {
  cart_apply_impulse(cart, Vector2Rotate(rel_pos, cart->rot),
                     Vector2Rotate(impulse, cart->rot));
}

void cart_tick(Cart *cart, WallArr walls, int num_walls, float delta) {
  if (delta <= 0.0f)
    return;

  // --- 1. Wheel Dynamics & Ground Friction ---
  Vector2 u_fwd = (Vector2){sinf(cart->rot), -cosf(cart->rot)};
  Vector2 u_right = (Vector2){cosf(cart->rot), sinf(cart->rot)};

  float v_fwd = Vector2DotProduct(cart->vel, u_fwd);

  float g = 9.81f;
  float f_roll_mag = 0.050f * cart->mass * g;
  float f_roll = 0.0f;
  if (fabsf(v_fwd) > 0.02f) {
    float sign = (v_fwd > 0.0f) ? 1.0f : -1.0f;
    float blend = fminf(fabsf(v_fwd) / 0.15f, 1.0f);
    f_roll = -sign * f_roll_mag * blend - 0.20f * v_fwd * fabsf(v_fwd);
  } else {
    cart->vel = Vector2Subtract(cart->vel, Vector2Scale(u_fwd, v_fwd));
    v_fwd = 0.0f;
  }

  Vector2 r_f = Vector2Rotate((Vector2){0.0f, -0.22f}, cart->rot);
  Vector2 r_r = Vector2Rotate((Vector2){0.0f, +0.22f}, cart->rot);

  Vector2 v_f = (Vector2){cart->vel.x - cart->ang_vel * r_f.y,
                          cart->vel.y + cart->ang_vel * r_f.x};
  Vector2 v_r = (Vector2){cart->vel.x - cart->ang_vel * r_r.y,
                          cart->vel.y + cart->ang_vel * r_r.x};

  float v_lat_f = Vector2DotProduct(v_f, u_right);
  float v_lat_r = Vector2DotProduct(v_r, u_right);

  float grip_rear_coeff = 28.0f * cart->mass;
  float grip_front_coeff = 9.0f * cart->mass;

  float normal_load = 0.5f * cart->mass * g;
  float max_friction_rear = 0.85f * normal_load;
  float max_friction_front = 0.40f * normal_load;

  float f_lat_r = -grip_rear_coeff * v_lat_r;
  if (fabsf(f_lat_r) > max_friction_rear) {
    f_lat_r = (f_lat_r > 0 ? 1.0f : -1.0f) * max_friction_rear;
  }

  float f_lat_f = -grip_front_coeff * v_lat_f;
  if (fabsf(f_lat_f) > max_friction_front) {
    f_lat_f = (f_lat_f > 0 ? 1.0f : -1.0f) * max_friction_front;
  }

  Vector2 F_lat_r_vec = Vector2Scale(u_right, f_lat_r);
  Vector2 F_lat_f_vec = Vector2Scale(u_right, f_lat_f);

  float torque_wheels = (r_r.x * F_lat_r_vec.y - r_r.y * F_lat_r_vec.x) +
                        (r_f.x * F_lat_f_vec.y - r_f.y * F_lat_f_vec.x);

  float torque_damping =
      -2.2f * cart->ang_vel * (cart->mom_inertia / cart->base_mom_inertia);

  Vector2 f_total = Vector2Add(Vector2Scale(u_fwd, f_roll),
                               Vector2Add(F_lat_r_vec, F_lat_f_vec));

  cart->vel =
      Vector2Add(cart->vel, Vector2Scale(f_total, (1.0f / cart->mass) * delta));
  cart->ang_vel +=
      ((torque_wheels + torque_damping) / cart->mom_inertia) * delta;

  if (fabsf(cart->ang_vel) > MAX_ANGULAR_VEL) {
    cart->ang_vel = (cart->ang_vel > 0 ? 1.0f : -1.0f) * MAX_ANGULAR_VEL;
  }

  if (fabsf(cart->ang_vel) < 0.005f && Vector2Length(cart->vel) < 0.015f) {
    cart->ang_vel = 0.0f;
    cart->vel = Vector2Zero();
  }

  cart->pos = Vector2Add(cart->pos, Vector2Scale(cart->vel, delta));
  cart->rot += cart->ang_vel * delta;

  // --- 2. Collision Detection and Rigid Body Response ---
  float step = Vector2Length(cart->vel) * delta;
  for (int iter = 0; iter < COLLISION_ITERATIONS; iter++) {
    bool collision_occurred = false;
    for (int i = 0; i < num_walls; i++) {
      ColliPoly wall_poly = walls.items[i].collider;

      float dist_cap = walls.items[i].max_dist + cart->max_vrt_dist + step * 2;
      if (Vector2DistanceSqr(walls.items[i].collider.items[0], cart->pos) >
          dist_cap * dist_cap)
        continue;

      ColliPoly cart_world_poly = cart_get_world_collider(cart);
      CollisionInfo collision_info =
          get_polygon_collision_info(cart_world_poly, wall_poly);

      if (collision_info.collided) {
        collision_occurred = true;

        cart->pos = Vector2Add(cart->pos, collision_info.mtv);

        Vector2 contact_point_world = find_contact_point_on_cart(
            &cart_world_poly, Vector2Negate(collision_info.normal));
        Vector2 r_cart = Vector2Subtract(contact_point_world, cart->pos);

        Vector2 r_cart_perp_vel =
            (Vector2){-cart->ang_vel * r_cart.y, cart->ang_vel * r_cart.x};
        Vector2 contact_velocity_cart = Vector2Add(cart->vel, r_cart_perp_vel);

        Vector2 n = collision_info.normal;
        float vn = Vector2DotProduct(contact_velocity_cart, n);

        if (vn < 0.0f) {
          float r_cross_n = (r_cart.x * n.y) - (r_cart.y * n.x);
          float inv_mass = (cart->mass > EPSILON) ? (1.0f / cart->mass) : 0.0f;
          float inv_inertia_n =
              (cart->mom_inertia > EPSILON)
                  ? ((r_cross_n * r_cross_n) / cart->mom_inertia)
                  : 0.0f;
          float Kn = inv_mass + inv_inertia_n;

          if (Kn > EPSILON) {
            float restitution = RESTITUTION;
            float jn = -(1.0f + restitution) * vn / Kn;
            if (jn < 0.0f)
              jn = 0.0f;

            Vector2 t = (Vector2){-n.y, n.x};
            float vt = Vector2DotProduct(contact_velocity_cart, t);
            float r_cross_t = (r_cart.x * t.y) - (r_cart.y * t.x);
            float inv_inertia_t =
                (cart->mom_inertia > EPSILON)
                    ? ((r_cross_t * r_cross_t) / cart->mom_inertia)
                    : 0.0f;
            float Kt = inv_mass + inv_inertia_t;

            float jt = 0.0f;
            if (Kt > EPSILON) {
              jt = -vt / Kt;
              float max_jt = WALL_FRICTION * jn;
              if (jt > max_jt)
                jt = max_jt;
              if (jt < -max_jt)
                jt = -max_jt;
            }

            Vector2 impulse =
                Vector2Add(Vector2Scale(n, jn), Vector2Scale(t, jt));
            cart_apply_impulse(cart, r_cart, impulse);
          }
        }
      }
      collipoly_free(&cart_world_poly);
    }
    if (!collision_occurred)
      break;
  }
}

bool cart_collide_cart(Cart *c1, Cart *c2, Vector2 *out_bump_impulse,
                       float *out_bump_ang) {
  if (out_bump_impulse)
    *out_bump_impulse = Vector2Zero();
  if (out_bump_ang)
    *out_bump_ang = 0.0f;

  Vector2 delta = Vector2Subtract(c1->pos, c2->pos);
  float dist = Vector2Length(delta);
  float min_dist = c1->max_vrt_dist + c2->max_vrt_dist;
  if (dist >= min_dist || dist < 0.001f)
    return false;

  Vector2 n = Vector2Scale(delta, 1.0f / dist);
  float penetration = min_dist - dist;

  float m1 = c1->mass > 0.1f ? c1->mass : 12.0f;
  float m2 = c2->mass > 0.1f ? c2->mass : 12.0f;
  float inv_m1 = 1.0f / m1;
  float inv_m2 = 1.0f / m2;
  float total_inv_m = inv_m1 + inv_m2;

  c1->pos = Vector2Add(c1->pos,
                       Vector2Scale(n, penetration * (inv_m1 / total_inv_m)));
  c2->pos = Vector2Subtract(
      c2->pos, Vector2Scale(n, penetration * (inv_m2 / total_inv_m)));

  Vector2 v_rel = Vector2Subtract(c1->vel, c2->vel);
  float vn = Vector2DotProduct(v_rel, n);
  if (vn >= 0.0f)
    return false;

  // Punchy arcade bumper response: energetic restitution and bonus charging
  // kick
  float restitution = 1.15f;
  float impact_speed = -vn;
  float boost = (impact_speed > 0.35f) ? 1.5f : 1.0f;
  float jn = -(1.0f + restitution) * vn / total_inv_m * boost;

  Vector2 impulse = Vector2Scale(n, jn);

  c1->vel = Vector2Add(c1->vel, Vector2Scale(impulse, inv_m1));
  c2->vel = Vector2Subtract(c2->vel, Vector2Scale(impulse, inv_m2));

  float i1 = c1->mom_inertia > 0.1f ? c1->mom_inertia : 0.6f;
  float i2 = c2->mom_inertia > 0.1f ? c2->mom_inertia : 0.6f;
  float torque = ((n.x * v_rel.y - n.y * v_rel.x) * 1.5f) + (n.y * 1.0f);
  c1->ang_vel += torque / i1;
  c2->ang_vel -= torque / i2;

  if (out_bump_impulse && out_bump_ang) {
    *out_bump_impulse = Vector2Scale(impulse, -inv_m2);
    *out_bump_ang = -torque / i2;
  }

  return (impact_speed > 0.35f);
}

Item cart_consume_item(Cart *cart, ItemArr *items, int index) {
  Item item = items->items[index];

  ColliPoly padd_collider = {
      .len = cart->collider.len,
      .items = malloc(cart->collider.len * sizeof(Vector2)),
  };
  if (!padd_collider.items)
    perror_exit("cart_consume_item malloc");

  for (int i = 0; i < cart->collider.len; i++) {
    Vector2 vrt = cart->collider.items[i];
    padd_collider.items[i] =
        Vector2Subtract(vrt, Vector2Scale(Vector2Normalize(vrt), item.radius));
  }

  Vector2 item_pos = collipoly_random_pnt(padd_collider);
  collipoly_free(&padd_collider);

  item.pos = item_pos;
  item.rot = mall_rand_float() * 2.0f * PI;
  itemarr_push(&cart->items, item);
  itemarr_remove(items, index);

  cart_recalculate_mass_inertia(cart);
  return item;
}

void cart_draw(Cart *cart, int pick, Item *pick_item, Vector2 topleft,
               Vector2 botright) {
  (void)pick;
  (void)pick_item;
  ColliPoly real_poly = cart_get_world_collider(cart);

  DrawTriangleFan(real_poly.items, real_poly.len, cart->sec_color);
  for (int i = 0; i < real_poly.len; i++) {
    Vector2 p1 = real_poly.items[i];
    Vector2 p2 = real_poly.items[(i + 1) % real_poly.len];
    DrawLineEx(p1, p2, 0.05, cart->color);
    DrawCircleV(p1, 0.025, cart->color);
  }
  collipoly_free(&real_poly);
  itemarr_draw(cart->items, cart->pos, cart->rot, topleft, botright);
}

void cart_draw_hands(Cart *cart, int pick, Item *pick_item) {
  Vector2 driver =
      Vector2Add(cart->pos, Vector2Rotate(cart->driver_pos, cart->rot));
  Vector2 lhaldle =
      Vector2Add(cart->pos, Vector2Rotate(cart->collider.items[1], cart->rot));
  Vector2 rhaldle =
      Vector2Add(cart->pos, Vector2Rotate(cart->collider.items[2], cart->rot));
  Vector2 lshoulder = Vector2Add(
      cart->pos,
      Vector2Rotate(Vector2Add(cart->driver_pos,
                               (Vector2){-cart->driver_radius * 1.5, 0.00}),
                    cart->rot));
  Vector2 rshoulder = Vector2Add(
      cart->pos,
      Vector2Rotate(Vector2Add(cart->driver_pos,
                               (Vector2){cart->driver_radius * 1.5, 0.00}),
                    cart->rot));

  if (cart->standard) {
    DrawCircleV(lhaldle, 0.07, cart->color);
    DrawCircleV(rhaldle, 0.07, cart->color);
    bool drew_pick_arm = false;
    if (pick != -1 && pick_item != NULL) {
      Vector2 pick_shoulder = (pick == 1) ? rshoulder : lshoulder;
      if (Vector2Distance(pick_shoulder, pick_item->pos) <= 6.5f) {
        DrawLineEx(pick_shoulder, pick_item->pos, ARM_WIDTH,
                   cart->driver_color);
        DrawCircleV(pick_item->pos, 0.04, cart->driver_color);
        drew_pick_arm = true;
      }
    }

    if (!drew_pick_arm || pick != 0) {
      DrawLineEx(lshoulder, lhaldle, ARM_WIDTH, cart->driver_color);
      DrawCircleV(lhaldle, 0.04, cart->driver_color);
    }
    if (!drew_pick_arm || pick != 1) {
      DrawLineEx(rshoulder, rhaldle, ARM_WIDTH, cart->driver_color);
      DrawCircleV(rhaldle, 0.04, cart->driver_color);
    }
  }
  DrawCircleV(driver, cart->driver_radius, cart->driver_color);
  DrawLineEx(lshoulder, rshoulder, SHOUDLER_WIDTH, cart->driver_color);
  DrawCircleV(lshoulder, SHOUDLER_WIDTH / 2, cart->driver_color);
  DrawCircleV(rshoulder, SHOUDLER_WIDTH / 2, cart->driver_color);
}
