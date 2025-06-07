#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <time.h>
#define WFC_IMPLEMENTATION
#include "wfc.h"

#define GRAVITY (Vector2){0.0f, 250.0f} // Pixels/s^2 (adjust as needed)
#define COLLISION_ITERATIONS 8          // Number of iterations for collision resolution per tick
#define RESTITUTION 0.0f                // Coefficient of restitution for collisions
#define LINEAR_DAMP 0.1f
#define ANGULAR_DAMP 0.2f
#define MAX_ANGULAR_VEL 2.0f

float fsignf(float x) {
    if (x > 0) return 1.0f;
    if (x < 0) return -1.0f;
    return 0.0f;
}

void perror_exit(const char* msg) {
    perror(msg);
    exit(1);
}

typedef struct {
    Vector2* items;
    int len;
} ColliPoly;

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

void collipoly_free(ColliPoly* poly) {
    if (poly && poly->items) {
        free(poly->items);
        poly->items = NULL;
    }
}

#define NUM_COLORS 17
Color colors[NUM_COLORS] = {
    LIGHTGRAY,
    GRAY,
    YELLOW,
    GOLD,
    ORANGE,
    PINK,
    RED,
    GREEN,
    LIME,
    SKYBLUE,
    BLUE,
    PURPLE,
    VIOLET,
    BEIGE,
    WHITE,
    MAGENTA,
    RAYWHITE 
};

Color GetRandomColor() {
    return colors[GetRandomValue(0, NUM_COLORS-1)];
}

// Projects polygon onto axis
static void collipoly_project(ColliPoly polygon, Vector2 axis, float* min, float* max) {
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

// Function to check if two convex polygons intersect using the Separating Axis Theorem (SAT)
// Assumes polygons are convex.
bool collipoly_collide(ColliPoly a, ColliPoly b) {
    if (a.items == NULL || b.items == NULL || a.len < 3 || b.len < 3)
        return false;

    for (int i = 0; i < a.len; i++) {
        Vector2 p1 = a.items[i];
        Vector2 p2 = a.items[(i + 1) % a.len];
        Vector2 edge = Vector2Subtract(p2, p1);

        // Calculate the perpendicular vector (normal) to the edge
        // This normal is a potential separating axis
        Vector2 axis = (Vector2){ -edge.y, edge.x };
        axis = Vector2Normalize(axis); // Normalize the axis for consistent projection

        // Project both polygons onto this axis
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

        Vector2 axis = (Vector2){ -edge.y, edge.x };
        axis = Vector2Normalize(axis);

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
    Vector2 res = { 0.0f, 0.0f };
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
        sum += (v.y * v.y + v.y * w.y + w.y * w.y + v.x * v.x + v.x * w.x + w.x * w.x) * (v.x * w.y - w.x * v.y);
    }
    return sum / 12;
}

bool collipoly_contains(ColliPoly polygon, Vector2 pnt) {
    int i, j, c = 0;
    for (i = 0, j = polygon.len-1; i < polygon.len; j = i++) {
        Vector2 v = polygon.items[i];
        Vector2 w = polygon.items[j];
        if ( ((v.y > pnt.y) != (w.y > pnt.y)) &&
             (pnt.x < (w.x - v.x) * (pnt.y-v.y) / (w.y - v.y) + v.y) )
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

    int cnt = 0;
    Vector2 res = { 0.0f, 0.0f };
    while (true) {
        res.x = (float)rand()/(float)(RAND_MAX/max_dist/2) - max_dist;
        res.y = (float)rand()/(float)(RAND_MAX/max_dist/2) - max_dist;
        if (collipoly_contains(polygon, res)) break;
        cnt++;
        if (cnt > 10) exit(0);
    };
    return res;
}

typedef struct {
    ColliPoly collider;
    Color color;
    float max_dist;
} Wall;

void wall_calculate_max_dist(Wall* wall) {
    if (wall->collider.len < 1) {
        wall->max_dist = 0.0f;
        return;
    };
    wall->max_dist = 0.0f;
    Vector2 first_vrt = wall->collider.items[0];
    for (int i = 0; i < wall->collider.len; i++) {
        float dist = Vector2Distance(first_vrt, wall->collider.items[i]);
        if (dist > wall->max_dist)
            wall->max_dist = dist;
    }
}

Wall wall_new_va(Color color, int num_vertices, va_list args) {

    Wall res;
    res.color = color;
    res.collider.len = num_vertices;
    res.collider.items = malloc(num_vertices * sizeof(Vector2));
    if (!res.collider.items)
        perror_exit("wall_new malloc");

    for (int i = 0; i < num_vertices; i++)
        res.collider.items[i] = va_arg(args, Vector2);

    wall_calculate_max_dist(&res);
    return res;
}

Wall wall_new_ptr(Color color, int num_vertices, Vector2* items) {
    Wall res;
    res.color = color;
    res.collider.len = num_vertices;
    res.collider.items = malloc(num_vertices * sizeof(Vector2));
    if (!res.collider.items)
        perror_exit("wall_new malloc");

    for (int i = 0; i < num_vertices; i++)
        res.collider.items[i] = items[i];

    wall_calculate_max_dist(&res);
    return res;
}

Wall wall_new_rect(Color color, Vector2 corner_a, Vector2 corner_b) {
    Wall res;
    res.color = color;
    res.collider.len = 4;
    res.collider.items = malloc(4 * sizeof(Vector2));
    if (!res.collider.items)
        perror_exit("wall_new malloc");

    Vector2 topleft = { 
        fminf(corner_a.x, corner_b.x),
        fminf(corner_a.y, corner_b.y),
    };
    Vector2 botright = { 
        fmaxf(corner_a.x, corner_b.x),
        fmaxf(corner_a.y, corner_b.y),
    };

    res.collider.items[0] = topleft;
    res.collider.items[1] = (Vector2){topleft.x, botright.y};
    res.collider.items[2] = botright;
    res.collider.items[3] = (Vector2){botright.x, topleft.y};

    wall_calculate_max_dist(&res);
    return res;
}

Wall wall_new(Color color, int num_vertices, ...) {
    va_list args;
    va_start(args, num_vertices);
    Wall res = wall_new_va(color, num_vertices, args);
    va_end(args);
    return res;
}

void wall_free(Wall wall) {
    free(wall.collider.items);
}

void wall_draw(Wall wall) {
    DrawTriangleFan(wall.collider.items, wall.collider.len, wall.color);
}

typedef struct {
    Wall* items;
    int len;
    int cap;
} WallArr;

WallArr wallarr_new() {
    WallArr res = { NULL, 0, 0 };
    return res;
}

void wallarr_free_all(WallArr* crl) {
    for (int i = 0; i < crl->len; i++) {
        wall_free(crl->items[i]);
    }
    free(crl->items);
    crl->items = NULL;
}

void wallarr_draw(WallArr crl) {
    for (int i = 0; i < crl.len; i++) {
        wall_draw(crl.items[i]);
    }
}

void wallarr_push(WallArr* crl, Wall cr) {
    if (!crl->items) {
        crl->len = 0;
        crl->cap = 10;
        crl->items = malloc(crl->cap * sizeof(Wall));
    } else if (crl->len >= crl->cap) {
        crl->cap *= 2;
        crl->items = realloc(crl->items, crl->cap * sizeof(Wall));
    }
    crl->items[crl->len] = cr;
    crl->len++;
}

void wallarr_push_malloc(WallArr* crl, Color color, int num, ...) {
    va_list args;
    va_start(args, num);
    wallarr_push(crl, wall_new_va(color, num, args));
    va_end(args);
}

void wallarr_push_malloc_rect(WallArr* crl, Color color, Vector2 topleft, Vector2 botright) {
    wallarr_push(crl, wall_new_rect(color, topleft, botright));
}

void wallarr_remove(WallArr* crl, int index) {
    if (index >= crl->len) return;
    crl->len--;
    for (int i = 0; i < crl->len; i++)
        crl->items[i] = crl->items[i+1];
}

void wallarr_remove_free(WallArr* crl, int index) {
    if (index >= crl->len) return;
    wall_free(crl->items[index]);
    crl->len--;
    for (int i = 0; i < crl->len; i++)
        crl->items[i] = crl->items[i+1];
}

typedef struct {
    Texture2D image;
    Vector2 pos;
    float rot;
    float scale;
    float collision_radius;
} Item;

Item item_new(Texture2D image, Vector2 pos, float rot, float scale, float col_rad) {
    return (Item){ image, pos, rot, scale, col_rad };
}

void item_set_scale_from_width(Item* item, float n_width) {
    item->scale = n_width / item->image.width;
}

Item item_new_width(Texture2D image, Vector2 pos, float rot, float new_width, float col_rad) {
    Item res = item_new(image, pos, rot, 0, col_rad);
    item_set_scale_from_width(&res, new_width);
    return res;
}

void item_free(Item item) {
    UnloadTexture(item.image);
}

void item_draw(Item item, Vector2 offset, float rot) {
    Vector2 half_size = {
        (float)item.image.width / 2 * item.scale,
        (float)item.image.height / 2 * item.scale,
    };
    float total_rot = item.rot + rot;
    DrawTextureEx(
        item.image, 
        Vector2Subtract(
            Vector2Add(offset, Vector2Rotate(item.pos, rot)),
            Vector2Rotate(half_size, total_rot)
        ),
        total_rot * RAD2DEG, item.scale, WHITE
    );
}

ColliPoly item_get_world_collider(Item item) {
    Vector2 half_size = {
        (float)item.image.width / 2 * item.scale,
        (float)item.image.height / 2 * item.scale,
    };
    return collipoly_new( 4,
        Vector2Rotate((Vector2){
            item.pos.x - half_size.x,
            item.pos.y - half_size.y
        }, item.rot),
        Vector2Rotate((Vector2){
            item.pos.x - half_size.x,
            item.pos.y + half_size.y
        }, item.rot),
        Vector2Rotate((Vector2){
            item.pos.x + half_size.x,
            item.pos.y + half_size.y
        }, item.rot),
        Vector2Rotate((Vector2){
            item.pos.x + half_size.x,
            item.pos.y - half_size.y
        }, item.rot)
    );
}

typedef struct {
    Item* items;
    int len;
    int cap;
} ItemArr;

ItemArr itemarr_new() {
    ItemArr res = { NULL, 0, 0 };
    return res;
}

void itemarr_free_all(ItemArr* crl) {
    for (int i = 0; i < crl->len; i++) {
        item_free(crl->items[i]);
    }
    free(crl->items);
    crl->items = NULL;
}

void itemarr_draw(ItemArr crl, Vector2 offset, float rot) {
    // printf("%d\n", crl.len);
    for (int i = 0; i < crl.len; i++) {
        // printf("%d\n", i);
        item_draw(crl.items[i], offset, rot);
    }
}

void itemarr_push(ItemArr* crl, Item cr) {
    if (!crl->items) {
        crl->len = 0;
        crl->cap = 10;
        crl->items = malloc(crl->cap * sizeof(Item));
    } else if (crl->len >= crl->cap) {
        crl->cap *= 2;
        crl->items = realloc(crl->items, crl->cap * sizeof(Item));
    }
    crl->items[crl->len] = cr;
    crl->len++;
}

void itemarr_remove(ItemArr* crl, int index) {
    if (index >= crl->len) return;
    crl->len--;
    for (int i = 0; i < crl->len; i++)
        crl->items[i] = crl->items[i+1];
}

void itemarr_remove_free(ItemArr* crl, int index) {
    if (index >= crl->len) return;
    item_free(crl->items[index]);
    crl->len--;
    for (int i = 0; i < crl->len; i++)
        crl->items[i] = crl->items[i+1];
}


typedef struct {
    ColliPoly collider;

    Vector2 pos;
    Vector2 vel;

    float rot;
    float ang_vel;

    float mass;
    float mom_inertia;

    Color color;

    ItemArr items;

    Vector2 driver_pos;
    float driver_radius;
    Color driver_color;

    bool standard;
    float max_vrt_dist;
} Cart;

Cart cart_new_va(Color color, float mass_density, Vector2 driver_pos, float driver_radius, Color driver_color, bool standard, int num_vertices, va_list args) {
    Cart res = {
        .collider = (ColliPoly){
            .len = num_vertices,
        },
        .color = color,
        .rot = 0.0f,
        .ang_vel = 0.0f,
        .vel = Vector2Zero(),
        .items = itemarr_new(),
        .driver_pos = driver_pos,
        .driver_radius = driver_radius,
        .driver_color = driver_color,
        .standard = standard,
    };

    res.collider.items = malloc(num_vertices * sizeof(Vector2));
    if (!res.collider.items)
        perror_exit("cart_new malloc");

    for (int i = 0; i < num_vertices; i++) 
        res.collider.items[i] = va_arg(args, Vector2);

    float area = collipoly_area(res.collider);
    res.mass = fabsf(area) * mass_density;

    res.pos = collipoly_center(res.collider, area);

    for (int i = 0; i < num_vertices; i++) {
        res.collider.items[i].x -= res.pos.x;
        res.collider.items[i].y -= res.pos.y;
        float dist = Vector2Length(res.collider.items[i]);
        if (dist > res.max_vrt_dist)
            res.max_vrt_dist = dist;
    }

    res.mom_inertia = fabsf(collipoly_mom_inertia(res.collider)) * mass_density * 3;

    return res;
}

Cart cart_new_old(Color color, float mass_density, Vector2 driver_pos, float driver_radius, Color driver_color, bool standard, int num_vertices, ...) {
    va_list args;
    va_start(args, num_vertices);
    Cart res = cart_new_va(color, mass_density, driver_pos, driver_radius, driver_color, standard, num_vertices, args);
    va_end(args);
    return res;
}

Cart cart_new(Color color, Color driver_color, Vector2 pos) {
    return cart_new_old(
        color, 3,
        Vector2Add(pos, (Vector2){ 0.0f, 0.3f }),
        .1, driver_color, true, 4,
        Vector2Add(pos, (Vector2){ -0.15f, -0.27f }),
        Vector2Add(pos, (Vector2){ -0.15f, 0.27f }),
        Vector2Add(pos, (Vector2){ 0.15f, 0.27f }),
        Vector2Add(pos, (Vector2){ 0.15f, -0.27f })
    );
}

Cart cart_new_goofy(Color color, Color driver_color, Vector2 pos, int num_vert, ...) {
    va_list args;
    va_start(args, num_vert);
    Cart res = cart_new_va(
        color, 3,
        Vector2Add(pos, (Vector2){ 0.0f, 0.3f }),
        .1, driver_color, true, num_vert, args
    );
    va_end(args);
    return res;
}

void cart_free(Cart* cart) {
    collipoly_free(&cart->collider);
    itemarr_free_all(&cart->items);
}

typedef struct {
    bool collided;
    Vector2 normal; // Collision normal (points from B to A, to push A out of B)
    float depth;    // Penetration depth
    Vector2 mtv;    // Minimum Translation Vector (normal * depth)
} CollisionInfo;


// --- Helper functions for collision detection and cart manipulation ---

// Projects polygon onto axis (this is the function signature from user's code, assuming implementation exists)
// static void collipoly_project(CollisionPolygon polygon, Vector2 axis, float* min, float* max) {
//     if (polygon.len == 0) {
//         *min = 0; *max = 0;
//         return;
//     }
//     *min = Vector2DotProduct(polygon.items[0], axis);
//     *max = *min;

//     for (int i = 1; i < polygon.len; i++) {
//         float projection = Vector2DotProduct(polygon.items[i], axis);
//         if (projection < *min) {
//             *min = projection;
//         }
//         if (projection > *max) {
//             *max = projection;
//         }
//     }
// }


/**
 * @brief Gets detailed collision information between two convex polygons using SAT.
 * Assumes polyA and polyB vertices are in world space.
 * The MTV will be calculated to push polyA out of polyB.
 * The normal will point from polyB towards polyA.
 * @param polyA First polygon (e.g., the cart).
 * @param polyB Second polygon (e.g., a wall).
 * @return CollisionInfo structure with collision details.
 */
CollisionInfo get_polygon_collision_info(ColliPoly polyA, ColliPoly polyB) {
    CollisionInfo info = { .collided = false, .depth = FLT_MAX, .normal = {0,0}, .mtv = {0,0} };

    if (polyA.items == NULL || polyB.items == NULL || polyA.len < 3 || polyB.len < 3) {
        return info; // Not enough vertices
    }

    // Check axes of Polygon A
    for (int i = 0; i < polyA.len; i++) {
        Vector2 p1 = polyA.items[i];
        Vector2 p2 = polyA.items[(i + 1) % polyA.len];
        Vector2 edge = Vector2Subtract(p2, p1);
        Vector2 axis = Vector2Normalize((Vector2){-edge.y, edge.x}); // Perpendicular axis

        float minA, maxA, minB, maxB;
        collipoly_project(polyA, axis, &minA, &maxA);
        collipoly_project(polyB, axis, &minB, &maxB);

        if (maxA < minB || maxB < minA) { // Separating axis found
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
        Vector2 axis = Vector2Normalize((Vector2){-edge.y, edge.x}); // Perpendicular axis

        float minA, maxA, minB, maxB;
        collipoly_project(polyA, axis, &minA, &maxA);
        collipoly_project(polyB, axis, &minB, &maxB);

        if (maxA < minB || maxB < minA) { // Separating axis found
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

    // Ensure the normal points from polyB towards polyA (to push polyA out of polyB)
    // This uses the centroids of the polygons in world space.
    float areaA = collipoly_area(polyA);
    Vector2 centerA = collipoly_center(polyA, areaA);
    float areaB = collipoly_area(polyB);
    Vector2 centerB = collipoly_center(polyB, areaB);

    Vector2 direction_centerB_to_centerA = Vector2Subtract(centerA, centerB);
    if (Vector2DotProduct(info.normal, direction_centerB_to_centerA) < 0) {
        info.normal = Vector2Negate(info.normal); // Flip normal if it's not pointing from B to A
    }

    info.mtv = Vector2Scale(info.normal, info.depth);
    return info;
}

/**
 * @brief Creates a world-space representation of the cart's collider.
 * IMPORTANT: The 'items' in the returned CollisionPolygon are dynamically allocated
 * and must be freed using free_collision_polygon_items() after use.
 * @param cart Pointer to the cart.
 * @return CollisionPolygon with vertices in world space.
 */
ColliPoly cart_get_world_collider(const Cart* cart) {
    ColliPoly world_collider;
    world_collider.len = cart->collider.len;
    world_collider.items = (Vector2*)malloc(world_collider.len * sizeof(Vector2));
    if (!world_collider.items) {
        perror_exit("cart_get_world_collider malloc failed");
    }

    float s = sinf(cart->rot);
    float c = cosf(cart->rot);

    for (int i = 0; i < cart->collider.len; i++) {
        Vector2 local_v = cart->collider.items[i]; // Vertices are relative to cart's CoM (pos)

        // Rotate vertex
        float rotated_x = local_v.x * c - local_v.y * s;
        float rotated_y = local_v.x * s + local_v.y * c;

        // Translate vertex to world space
        world_collider.items[i] = (Vector2){
            cart->pos.x + rotated_x,
            cart->pos.y + rotated_y
        };
    }
    return world_collider;
}

/**
 * @brief Finds a contact point on the cart's world polygon.
 * This is a simplified heuristic: returns the vertex of cart_world_poly
 * that is furthest in the direction of the collision_normal.
 * @param cart_world_poly The cart's collider in world space.
 * @param collision_normal_from_wall_to_cart The collision normal, pointing from wall towards cart.
 * @return The contact point in world coordinates.
 */
Vector2 find_contact_point_on_cart(const ColliPoly* cart_world_poly, Vector2 collision_normal_from_wall_to_cart) {
    if (cart_world_poly->len == 0) return (Vector2){0,0}; // Should not happen for valid carts

    Vector2 contact_point = cart_world_poly->items[0];
    float max_proj = Vector2DotProduct(contact_point, collision_normal_from_wall_to_cart);

    for (int i = 1; i < cart_world_poly->len; i++) {
        float proj = Vector2DotProduct(cart_world_poly->items[i], collision_normal_from_wall_to_cart);
        if (proj > max_proj) {
            max_proj = proj;
            contact_point = cart_world_poly->items[i];
        }
    }
    return contact_point;
}


// --- Missing functions to be implemented ---

/**
 * @brief Applies an impulse to the cart.
 * Used for collision response. Modifies last_pos and last_rot to effect change in velocity.
 * @param cart Pointer to the cart.
 * @param rel_pos Position of impulse application relative to cart's center of mass (world space offset).
 * @param impulse The impulse vector (world space).
 * @param delta The time step of the simulation (used for Verlet adjustment).
 */
void cart_apply_impulse(Cart* cart, Vector2 rel_pos, Vector2 impulse, float delta) {
    if (cart->mass <= EPSILON) return; // Effectively infinite mass or invalid cart
    //
    // Linear impulse effect (changes linear velocity)
    // delta_v = impulse / mass
    // For Verlet: last_pos = current_pos - (current_velocity + delta_v) * dt
    // last_pos_new = last_pos_old - (impulse / mass) * dt
    Vector2 linear_velocity_change_scaled = Vector2Scale(impulse, (1.0f / cart->mass) * delta);
    // printf("%f %f\n", linear_velocity_change_scaled.x, linear_velocity_change_scaled.y);
    cart->vel = Vector2Add(cart->vel, linear_velocity_change_scaled);

    // Angular impulse effect (changes angular velocity)
    if (cart->mom_inertia <= EPSILON) return; // Effectively infinite moment of inertia or invalid

    // Angular impulse = r x J (cross product: rel_pos.x * impulse.y - rel_pos.y * impulse.x)
    float angular_impulse_magnitude = (rel_pos.x * impulse.y) - (rel_pos.y * impulse.x);

    // delta_angular_velocity = angular_impulse_magnitude / moment_of_inertia
    // For Verlet: last_rot = current_rot - (current_angular_velocity + delta_angular_velocity) * dt
    // last_rot_new = last_rot_old - (angular_impulse_magnitude / mom_inertia) * dt
    float angular_velocity_change_scaled = (angular_impulse_magnitude / cart->mom_inertia) * delta;
    cart->ang_vel += angular_velocity_change_scaled;
    // printf("%f %f, %f %f\n", rel_pos.x, rel_pos.y, impulse.x, impulse.y);

    // DrawLineEx(Vector2Add(cart->pos, rel_pos), Vector2Add(Vector2Add(cart->pos, rel_pos), Vector2Scale(impulse, 1)), 0.05f, RED);
}

void cart_apply_impulse_rotated(Cart* cart, Vector2 rel_pos, Vector2 impulse, float delta) {
    cart_apply_impulse(cart, Vector2Rotate(rel_pos, cart->rot), Vector2Rotate(impulse, cart->rot), delta);
}

/**
 * @brief Updates the cart's state for one time step (delta).
 * Includes Verlet integration, gravity, and collision detection/response with static walls.
 * @param cart Pointer to the cart to update.
 * @param walls Array of static CollisionPolygons representing walls.
 * @param num_walls Number of walls in the array.
 * @param delta Time step.
 */
void cart_tick(Cart* cart, WallArr walls, int num_walls, float delta) {
    if (delta <= 0.0f) return;

    // --- 1. Verlet Integration (Position and Rotation) ---
    Vector2 last_pos = cart->pos;
    float last_rot = cart->rot;

    // Apply gravity (as an acceleration)
    // Vector2 acceleration = GRAVITY;
    //
    // Update position: pos_new = pos_curr + (pos_curr - pos_last) + acc * dt^2
    cart->pos = Vector2Add(cart->pos, Vector2Scale(cart->vel, delta));
    cart->vel = Vector2Scale(cart->vel, powf(1 - LINEAR_DAMP, delta));

    // Update rotation: rot_new = rot_curr + (rot_curr - rot_last) + ang_acc * dt^2
    // (Assuming no explicit angular acceleration other than from impulses for now)
    cart->rot += cart->ang_vel * delta;
    cart->ang_vel *= powf(1 - ANGULAR_DAMP, delta);
    if (fabsf(cart->ang_vel) > MAX_ANGULAR_VEL) {
        cart->ang_vel -= fsignf(cart->ang_vel) * (fabsf(cart->ang_vel) - MAX_ANGULAR_VEL) * powf(0.5f, delta);
    }


    float step = Vector2Length(cart->vel) * delta;
    // --- 2. Collision Detection and Response ---
    // Multiple iterations can help stabilize complex collisions or stacking
    for (int iter = 0; iter < COLLISION_ITERATIONS; iter++) {
        bool collision_occured_this_iteration = false;
        for (int i = 0; i < num_walls; i++) {

            ColliPoly wall_poly = walls.items[i].collider; // Assuming walls are static and their vertices are world-space
            
            float dist_cap = walls.items[i].max_dist + cart->max_vrt_dist + step * 2;
            if (Vector2DistanceSqr(walls.items[i].collider.items[0], cart->pos) > dist_cap * dist_cap) continue;

            // Get the cart's current collider in world space
            ColliPoly cart_world_poly = cart_get_world_collider(cart);

            CollisionInfo collision_info = get_polygon_collision_info(cart_world_poly, wall_poly); // polyA = cart, polyB = wall

            if (collision_info.collided) {
                collision_occured_this_iteration = true;

                // --- 2a. Positional Correction (Resolve Penetration) ---
                // Move cart by MTV to resolve penetration. MTV points from wall to cart.
                cart->pos = Vector2Add(cart->pos, collision_info.mtv);

                // Vector2 cur_pos = cart->pos;
                // float cur_rot = cart->rot;
                //
                // for (float t = 0.0f; t < 1.0f; t += 0.001f) {
                //     cart->rot = last_rot + (cur_rot - last_rot) * t;
                //     cart->pos = Vector2Add(last_pos, Vector2Scale(Vector2Subtract(cur_pos, last_pos), t));
                //     CollisionPolygon c_w_poly = cart_get_world_collider(cart);
                //     bool collided = get_polygon_collision_info(c_w_poly, wall_poly).collided;
                //     free_collision_polygon_items(&c_w_poly);
                //     if (!collided) {
                //         break;
                //     }
                // }


                // Note: After positional correction, cart_world_poly is outdated.
                // For accuracy in the same iteration, it should be recomputed if other collisions are checked.
                // Or, accumulate corrections and apply once. For simplicity, apply immediately.
                // Re-generate cart_world_poly for impulse calculation if needed (or use the one before correction for contact point finding).
                // For finding contact point, it's often better to use the state *before* positional correction.
                // Let's use the cart_world_poly *before* this specific MTV correction for finding contact point.

                // --- 2b. Impulse-based Collision Response ---
                // Find a contact point on the cart (in world space)
                // free_collision_polygon_items(&cart_world_poly);
                // cart_world_poly = cart_get_world_collider(cart);

                Vector2 contact_point_world = find_contact_point_on_cart(&cart_world_poly, Vector2Negate(collision_info.normal));
                // DrawCircleV(contact_point_world, 0.1, RED);
                // printf("%f %f\n", contact_point_world.x, contact_point_world.y);
                Vector2 r_cart = Vector2Subtract(contact_point_world, cart->pos); // Vector from cart CoM to contact point AFTER mtv correction
                // Or, relative to cart->pos BEFORE mtv correction: Vector2Subtract(contact_point_world, Vector2Subtract(cart->pos, collision_info.mtv))
                // Let's use r_cart relative to the CoM at the moment of impulse.
                r_cart = Vector2Subtract(contact_point_world, cart->pos);



                // Velocity of the contact point on the cart: V_contact = V_linear + omega x r_cart
                // omega x r_cart = (-omega * r_cart.y, omega * r_cart.x)
                Vector2 r_cart_perp_vel = (Vector2){-cart->ang_vel * r_cart.y, cart->ang_vel * r_cart.x};
                Vector2 contact_velocity_cart = Vector2Add(cart->vel, r_cart_perp_vel);

                // Relative velocity along the collision normal
                float relative_velocity_normal = Vector2DotProduct(contact_velocity_cart, collision_info.normal);

                // If objects are already separating, no impulse needed
                if (relative_velocity_normal > 0.0f) {
                    collipoly_free(&cart_world_poly);
                    continue; 
                }

                // Calculate impulse magnitude (j)
                // j = -(1 + e) * V_rel_normal / (1/m1 + 1/m2 + (r1_cross_n)^2/I1 + (r2_cross_n)^2/I2)
                // For cart vs static wall (m2=inf, I2=inf):
                // j = -(1 + e) * V_rel_normal / (1/m_cart + (r_cart_cross_n)^2/I_cart)

                float r_cart_cross_normal = (r_cart.x * collision_info.normal.y) - (r_cart.y * collision_info.normal.x);
                float term_inv_mass = (cart->mass > EPSILON) ? (1.0f / cart->mass) : 0.0f;
                float term_inv_inertia = (cart->mom_inertia > EPSILON) ? ((r_cart_cross_normal * r_cart_cross_normal) / cart->mom_inertia) : 0.0f;

                float denominator = term_inv_mass + term_inv_inertia;

                if (denominator < EPSILON) { // Avoid division by zero (e.g. if cart has infinite mass/inertia)
                    collipoly_free(&cart_world_poly);
                    continue;
                }

                float j_magnitude = -(1.0f + RESTITUTION) * relative_velocity_normal / denominator;

                // Impulse vector (applied to cart)
                Vector2 impulse_vector = Vector2Scale(collision_info.normal, j_magnitude);
                // Apply impulse to the cart
                // The rel_pos for cart_apply_impulse is r_cart
                cart_apply_impulse(cart, r_cart, impulse_vector, delta);
                // cart->last_rot = cart->rot;
                // cart->rot *= 0.5;
            }
            // Free the dynamically allocated world collider for the cart
            collipoly_free(&cart_world_poly);
        }
        if (!collision_occured_this_iteration) break; // No collisions in this iteration, further iterations won't change anything
    }
}

void cart_consume_item(Cart* cart, ItemArr* items, int index) {
    Item item = items->items[index];

    ColliPoly padd_collider = {
        .len = cart->collider.len,
        .items = malloc(cart->collider.len * sizeof(Vector2)),
    };
    if (!padd_collider.items) perror_exit("cart_consume_item malloc");

    for (int i = 0; i < cart->collider.len; i++) {
        Vector2 vrt = cart->collider.items[i];
        padd_collider.items[i] = Vector2Subtract(
            vrt,
            Vector2Scale(Vector2Normalize(vrt), item.collision_radius)
        );
    }

    Vector2 item_pos = collipoly_random_pnt(padd_collider);
    collipoly_free(&padd_collider);

    item.pos = item_pos;
    item.rot = (float)rand()/(float)(RAND_MAX/PI/2);
    itemarr_push(&cart->items, item);
    itemarr_remove(items, index);
}

// -1 not pick, 0 left pick, 1 right pick
void cart_draw(Cart* cart, int pick, Item* pick_item) {
    ColliPoly real_poly = cart_get_world_collider(cart);

    for (int i = 0; i < real_poly.len; i++) {
        Vector2 p1 = real_poly.items[i];
        Vector2 p2 = real_poly.items[(i + 1) % real_poly.len];
        DrawLineEx(p1, p2, 0.05, cart->color);
        DrawCircleV(p1, 0.025, cart->color);
    }
    collipoly_free(&real_poly);
    itemarr_draw(cart->items, cart->pos, cart->rot);

}

void cart_draw_hands(Cart* cart, int pick, Item* pick_item) {
    Vector2 driver = Vector2Add(cart->pos, Vector2Rotate(cart->driver_pos, cart->rot));
    Vector2 lhaldle = Vector2Add(cart->pos, Vector2Rotate(cart->collider.items[1], cart->rot));
    Vector2 rhaldle = Vector2Add(cart->pos, Vector2Rotate(cart->collider.items[2], cart->rot));

    if (cart->standard) {
        DrawCircleV(lhaldle, 0.07, cart->color);
        DrawCircleV(rhaldle, 0.07, cart->color);
        if (pick != 0) {
            DrawLineEx(driver, lhaldle, 0.04, cart->driver_color);
            DrawCircleV(lhaldle, 0.04, cart->driver_color);
        }
        if (pick != 1) {
            DrawLineEx(driver, rhaldle, 0.04, cart->driver_color);
            DrawCircleV(rhaldle, 0.04, cart->driver_color);
        }
        if (pick != -1) {
            DrawLineEx(driver, pick_item->pos, 0.04, cart->driver_color);
            DrawCircleV(pick_item->pos, 0.04, cart->driver_color);
        }
    }
    DrawCircleV(driver, cart->driver_radius, cart->driver_color);
}

void gen_plan(char* filename, WallArr* walls) {
    Image wfc_image = LoadImage(filename);

    Color* wfc_image_colors = malloc(wfc_image.width * wfc_image.height * 4);
    if (!wfc_image_colors) perror_exit("wfc_image_colors malloc");

    for (int i = 0; i < wfc_image.height; i++) {
        for (int j = 0; j < wfc_image.width; j++) {
            int idx = i * wfc_image.width + j;
            wfc_image_colors[idx] = GetImageColor(wfc_image, i, j);
        }
    }

    struct wfc_image image = {
        .data = (unsigned char*)wfc_image_colors,
        .width = wfc_image.width,
        .height = wfc_image.height,
        .component_cnt = 4,
    };
    struct wfc* wfc = wfc_overlapping(64, 64, &image, 3, 3, 1, 1, 1, 1);
    if (!wfc) perror_exit("wfc_overlapping");

    if (!wfc_run(wfc, -1)) perror_exit("wfc_run");

    struct wfc_image* res_plan = wfc_output_image(wfc);

    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            int idx = i * 64 + j;
            if (res_plan->data[idx * 4] != 0) {
                wallarr_push_malloc_rect(
                    walls, GRAY, (Vector2){i, j}, (Vector2){i + 1, j + 1}
                );
            }
        }
    }

    free(wfc_image_colors);
    UnloadImage(wfc_image);
    wfc_destroy(wfc);
    wfc_img_destroy(res_plan);
}

int main() {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "Polygon Collision Test");
    // SetTargetFPS(60);
    srand(time(NULL));

    Cart main_cart = cart_new_goofy(
        DARKBLUE, BLACK, Vector2Zero(), 4,
        (Vector2){ -0.15f, -0.27f },
        (Vector2){ -0.25f, 0.27f },
        (Vector2){ 0.25f, 0.27f },
        (Vector2){ 0.15f, -0.27f }
    );

    WallArr walls = wallarr_new();
    gen_plan("res/wfc.png", &walls);

    ItemArr items = itemarr_new();
    itemarr_push(&items, item_new_width(
        LoadTexture("res/watermelon.png"),
        (Vector2){3, -3},
        0, .3, .3
    ));
    // printf("%d\n", items.items[0].image.id);

    Camera2D cam = {
        (Vector2){(float)GetScreenWidth()/2, (float)GetScreenHeight()/2},
        (Vector2){0.0f, 0.0f},
        0, 100,
    };

    bool rot_follow = false;

    int sel_wall = -1;
    int sel_vrt = -1;

    bool dragging = false;
    int pick_item_idx = -1;
    int pick_side = -1;

    while (!WindowShouldClose()) {
        float delta = GetFrameTime();

        if (IsKeyDown(KEY_K)) {
            cart_apply_impulse_rotated(&main_cart, (Vector2){0.3f, 0.3f}, (Vector2){0.0f, 1.0f}, delta);
        }

        if (IsKeyDown(KEY_J)) {
            cart_apply_impulse_rotated(&main_cart, (Vector2){-0.3f, 0.3f}, (Vector2){0.0f, 1.0f}, delta);
        }

        if (IsKeyDown(KEY_H)) {
            cart_apply_impulse_rotated(&main_cart, (Vector2){-0.3f, 0.3f}, (Vector2){0.0f, -1.0f}, delta);
        }

        if (IsKeyDown(KEY_L)) {
            cart_apply_impulse_rotated(&main_cart, (Vector2){0.3f, 0.3f}, (Vector2){0.0f, -1.0f}, delta);
        }

        if (IsKeyDown(KEY_O))
            cam.zoom /= 1.001;

        if (IsKeyDown(KEY_P))
            cam.zoom *= 1.001;

        if (IsKeyPressed(KEY_I))
            rot_follow = !rot_follow;

        Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), cam);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            bool set_idx = false;
            for (int i = 0; i < walls.len; i++) {
                Wall wall = walls.items[i];
                for (int j = 0; j < wall.collider.len; j++) {
                    Vector2 vrt = wall.collider.items[j];
                    if (Vector2Distance(vrt, mouse) < 0.3) {
                        if (sel_vrt == j && sel_wall == i) {
                            dragging = true;
                            break;
                        }
                        sel_vrt = j;
                        sel_wall = i;
                        set_idx = true;
                        break;
                    }
                }
                if (set_idx)
                    break;
            }
        }


        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && dragging) {
            dragging = false;
        }

        cart_tick(&main_cart, walls, walls.len, delta);

        for (int i = 0; i < items.len; i++) {
            Item item = items.items[i];
            float min_dist = FLT_MAX;

            pick_item_idx = -1;
            pick_side = -1;

            Vector2 driver = Vector2Add(main_cart.pos, Vector2Rotate(main_cart.driver_pos, main_cart.rot));
            Vector2 lhaldle = Vector2Add(main_cart.pos, Vector2Rotate(main_cart.collider.items[1], main_cart.rot));
            Vector2 rhaldle = Vector2Add(main_cart.pos, Vector2Rotate(main_cart.collider.items[2], main_cart.rot));

            float ddist = Vector2Distance(item.pos, driver);

            if (ddist < item.collision_radius + main_cart.driver_radius && ddist < min_dist) {
                min_dist = ddist;
                pick_item_idx = i;
                if (Vector2Distance(item.pos, lhaldle) < Vector2Distance(item.pos, rhaldle))
                    pick_side = 0;
                else
                    pick_side = 1;
            }
        }

        if (IsKeyPressed(KEY_ENTER) && pick_side != -1) {
            cart_consume_item(&main_cart, &items, pick_item_idx);
            pick_item_idx = -1;
            pick_side = -1;
        }

        cam.target = main_cart.pos;
        if (rot_follow)
            cam.rotation = -main_cart.rot * RAD2DEG;
        else
            cam.rotation = 0;
        cam.offset = (Vector2){(float)GetScreenWidth()/2, (float)GetScreenHeight()/2};

        Vector2 topleft = GetScreenToWorld2D((Vector2){0, 0}, cam);
        Vector2 botright = GetScreenToWorld2D((Vector2){GetScreenWidth(), GetScreenHeight()}, cam);
        int diam = Vector2Length(Vector2Subtract(topleft, botright)) / 2;

        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode2D(cam);


        for (int i = -1; i < diam * 2 + 1; i++) {
            for (int j = -1; j < diam * 2 + 1; j++) {
                int posx = (int)cam.target.x - diam + i;
                int posy = (int)cam.target.y - diam + j;
                DrawRectangleV(
                    (Vector2){posx, posy},
                    (Vector2){1, 1}, 
                    (posx + posy) % 2 
                        ? RAYWHITE 
                        : LIGHTGRAY
                );
            }
        }

        wallarr_draw(walls);

        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
            walls.items[sel_wall].collider.items[sel_vrt] = mouse;
            DrawCircleV(mouse, 0.1, ORANGE);
        }

        cart_draw(&main_cart, pick_side, items.items + pick_item_idx);
        itemarr_draw(items, Vector2Zero(), 0);
        cart_draw_hands(&main_cart, pick_side, items.items + pick_item_idx);

        EndMode2D();
        DrawFPS(10, 10);
        EndDrawing();
    }

    itemarr_free_all(&items);
    wallarr_free_all(&walls);
    cart_free(&main_cart);

    CloseWindow();
    return 0;
}
