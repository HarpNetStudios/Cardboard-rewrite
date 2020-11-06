
#include "cube.h"

static inline double det2x2(double a, double b, double c, double d) { return a*d - b*c; }
static inline double det3x3(double a1, double a2, double a3,
                            double b1, double b2, double b3,
                            double c1, double c2, double c3)
{
    return a1 * det2x2(b2, b3, c2, c3)
         - b1 * det2x2(a2, a3, c2, c3)
         + c1 * det2x2(a2, a3, b2, b3);
}

bool matrix4::invert(const matrix4 &m, double mindet)
{
    double a1 = m.a.x, a2 = m.a.y, a3 = m.a.z, a4 = m.a.w,
           b1 = m.b.x, b2 = m.b.y, b3 = m.b.z, b4 = m.b.w,
           c1 = m.c.x, c2 = m.c.y, c3 = m.c.z, c4 = m.c.w,
           d1 = m.d.x, d2 = m.d.y, d3 = m.d.z, d4 = m.d.w,
           det1 =  det3x3(b2, b3, b4, c2, c3, c4, d2, d3, d4),
           det2 = -det3x3(a2, a3, a4, c2, c3, c4, d2, d3, d4),
           det3 =  det3x3(a2, a3, a4, b2, b3, b4, d2, d3, d4),
           det4 = -det3x3(a2, a3, a4, b2, b3, b4, c2, c3, c4),
           det = a1*det1 + b1*det2 + c1*det3 + d1*det4;

    if(fabs(det) < mindet) return false;

    double invdet = 1/det;

    a.x = det1 * invdet;
    a.y = det2 * invdet;
    a.z = det3 * invdet;
    a.w = det4 * invdet;

    b.x = -det3x3(b1, b3, b4, c1, c3, c4, d1, d3, d4) * invdet;
    b.y =  det3x3(a1, a3, a4, c1, c3, c4, d1, d3, d4) * invdet;
    b.z = -det3x3(a1, a3, a4, b1, b3, b4, d1, d3, d4) * invdet;
    b.w =  det3x3(a1, a3, a4, b1, b3, b4, c1, c3, c4) * invdet;

    c.x =  det3x3(b1, b2, b4, c1, c2, c4, d1, d2, d4) * invdet;
    c.y = -det3x3(a1, a2, a4, c1, c2, c4, d1, d2, d4) * invdet;
    c.z =  det3x3(a1, a2, a4, b1, b2, b4, d1, d2, d4) * invdet;
    c.w = -det3x3(a1, a2, a4, b1, b2, b4, c1, c2, c4) * invdet;

    d.x = -det3x3(b1, b2, b3, c1, c2, c3, d1, d2, d3) * invdet;
    d.y =  det3x3(a1, a2, a3, c1, c2, c3, d1, d2, d3) * invdet;
    d.z = -det3x3(a1, a2, a3, b1, b2, b3, d1, d2, d3) * invdet;
    d.w =  det3x3(a1, a2, a3, b1, b2, b3, c1, c2, c3) * invdet;

    return true;
}

bool raysphereintersect(const vec &center, float radius, const vec &o, const vec &ray, float &dist)
{
    vec c(center);
    c.sub(o);
    float v = c.dot(ray),
          inside = radius*radius - c.squaredlen();
    if(inside<0 && v<0) return false;
    float d = inside + v*v;
    if(d<0) return false;
    dist = v - sqrt(d);
    return true;
}

bool rayboxintersect(const vec &b, const vec &s, const vec &o, const vec &ray, float &dist, int &orient)
{
    for(int d = 0; d < 3; ++d) if(ray[d])
    {
        int dc = ray[d]<0 ? 1 : 0;
        float pdist = (b[d]+s[d]*dc - o[d]) / ray[d];
        vec v(ray);
        v.mul(pdist).add(o);
        if(v[R[d]] >= b[R[d]] && v[R[d]] <= b[R[d]]+s[R[d]]
        && v[C[d]] >= b[C[d]] && v[C[d]] <= b[C[d]]+s[C[d]])
        {
            dist = pdist;
            orient = 2*d+dc;
            return true;
        }
    }
    return false;
}

bool linecylinderintersect(const vec &from, const vec &to, const vec &start, const vec &end, float radius, float &dist)
{
    vec d(end), m(from), n(to);
    d.sub(start);
    m.sub(start);
    n.sub(from);
    float md = m.dot(d),
          nd = n.dot(d),
          dd = d.squaredlen();
    if(md < 0 && md + nd < 0) return false;
    if(md > dd && md + nd > dd) return false;
    float nn = n.squaredlen(),
          mn = m.dot(n),
          a = dd*nn - nd*nd,
          k = m.squaredlen() - radius*radius,
          c = dd*k - md*md;
    if(fabs(a) < 0.005f)
    {
        if(c > 0) return false;
        if(md < 0) dist = -mn / nn;
        else if(md > dd) dist = (nd - mn) / nn;
        else dist = 0;
        return true;
    }
    else if(c > 0)
    {
        float b = dd*mn - nd*md,
              discrim = b*b - a*c;
        if(discrim < 0) return false;
        dist = (-b - sqrtf(discrim)) / a;
    }
    else dist = 0;
    float offset = md + dist*nd;
    if(offset < 0)
    {
        if(nd <= 0) return false;
        dist = -md / nd;
        if(k + dist*(2*mn + dist*nn) > 0) return false;
    }
    else if(offset > dd)
    {
        if(nd >= 0) return false;
        dist = (dd - md) / nd;
        if(k + dd - 2*md + dist*(2*(mn-nd) + dist*nn) > 0) return false;
    }
    return dist >= 0 && dist <= 1;
}

extern const vec2 sincos360[721] =
{
    vec2(1.00000000, 0.00000000), vec2(0.99984770, 0.01745241), vec2(0.99939083, 0.03489950), vec2(0.99862953, 0.05233596), vec2(0.99756405, 0.06975647), vec2(0.99619470, 0.08715574), // 0
    vec2(0.99452190, 0.10452846), vec2(0.99254615, 0.12186934), vec2(0.99026807, 0.13917310), vec2(0.98768834, 0.15643447), vec2(0.98480775, 0.17364818), vec2(0.98162718, 0.19080900), // 6
    vec2(0.97814760, 0.20791169), vec2(0.97437006, 0.22495105), vec2(0.97029573, 0.24192190), vec2(0.96592583, 0.25881905), vec2(0.96126170, 0.27563736), vec2(0.95630476, 0.29237170), // 12
    vec2(0.95105652, 0.30901699), vec2(0.94551858, 0.32556815), vec2(0.93969262, 0.34202014), vec2(0.93358043, 0.35836795), vec2(0.92718385, 0.37460659), vec2(0.92050485, 0.39073113), // 18
    vec2(0.91354546, 0.40673664), vec2(0.90630779, 0.42261826), vec2(0.89879405, 0.43837115), vec2(0.89100652, 0.45399050), vec2(0.88294759, 0.46947156), vec2(0.87461971, 0.48480962), // 24
    vec2(0.86602540, 0.50000000), vec2(0.85716730, 0.51503807), vec2(0.84804810, 0.52991926), vec2(0.83867057, 0.54463904), vec2(0.82903757, 0.55919290), vec2(0.81915204, 0.57357644), // 30
    vec2(0.80901699, 0.58778525), vec2(0.79863551, 0.60181502), vec2(0.78801075, 0.61566148), vec2(0.77714596, 0.62932039), vec2(0.76604444, 0.64278761), vec2(0.75470958, 0.65605903), // 36
    vec2(0.74314483, 0.66913061), vec2(0.73135370, 0.68199836), vec2(0.71933980, 0.69465837), vec2(0.70710678, 0.70710678), vec2(0.69465837, 0.71933980), vec2(0.68199836, 0.73135370), // 42
    vec2(0.66913061, 0.74314483), vec2(0.65605903, 0.75470958), vec2(0.64278761, 0.76604444), vec2(0.62932039, 0.77714596), vec2(0.61566148, 0.78801075), vec2(0.60181502, 0.79863551), // 48
    vec2(0.58778525, 0.80901699), vec2(0.57357644, 0.81915204), vec2(0.55919290, 0.82903757), vec2(0.54463904, 0.83867057), vec2(0.52991926, 0.84804810), vec2(0.51503807, 0.85716730), // 54
    vec2(0.50000000, 0.86602540), vec2(0.48480962, 0.87461971), vec2(0.46947156, 0.88294759), vec2(0.45399050, 0.89100652), vec2(0.43837115, 0.89879405), vec2(0.42261826, 0.90630779), // 60
    vec2(0.40673664, 0.91354546), vec2(0.39073113, 0.92050485), vec2(0.37460659, 0.92718385), vec2(0.35836795, 0.93358043), vec2(0.34202014, 0.93969262), vec2(0.32556815, 0.94551858), // 66
    vec2(0.30901699, 0.95105652), vec2(0.29237170, 0.95630476), vec2(0.27563736, 0.96126170), vec2(0.25881905, 0.96592583), vec2(0.24192190, 0.97029573), vec2(0.22495105, 0.97437006), // 72
    vec2(0.20791169, 0.97814760), vec2(0.19080900, 0.98162718), vec2(0.17364818, 0.98480775), vec2(0.15643447, 0.98768834), vec2(0.13917310, 0.99026807), vec2(0.12186934, 0.99254615), // 78
    vec2(0.10452846, 0.99452190), vec2(0.08715574, 0.99619470), vec2(0.06975647, 0.99756405), vec2(0.05233596, 0.99862953), vec2(0.03489950, 0.99939083), vec2(0.01745241, 0.99984770), // 84
    vec2(0.00000000, 1.00000000), vec2(-0.01745241, 0.99984770), vec2(-0.03489950, 0.99939083), vec2(-0.05233596, 0.99862953), vec2(-0.06975647, 0.99756405), vec2(-0.08715574, 0.99619470), // 90
    vec2(-0.10452846, 0.99452190), vec2(-0.12186934, 0.99254615), vec2(-0.13917310, 0.99026807), vec2(-0.15643447, 0.98768834), vec2(-0.17364818, 0.98480775), vec2(-0.19080900, 0.98162718), // 96
    vec2(-0.20791169, 0.97814760), vec2(-0.22495105, 0.97437006), vec2(-0.24192190, 0.97029573), vec2(-0.25881905, 0.96592583), vec2(-0.27563736, 0.96126170), vec2(-0.29237170, 0.95630476), // 102
    vec2(-0.30901699, 0.95105652), vec2(-0.32556815, 0.94551858), vec2(-0.34202014, 0.93969262), vec2(-0.35836795, 0.93358043), vec2(-0.37460659, 0.92718385), vec2(-0.39073113, 0.92050485), // 108
    vec2(-0.40673664, 0.91354546), vec2(-0.42261826, 0.90630779), vec2(-0.43837115, 0.89879405), vec2(-0.45399050, 0.89100652), vec2(-0.46947156, 0.88294759), vec2(-0.48480962, 0.87461971), // 114
    vec2(-0.50000000, 0.86602540), vec2(-0.51503807, 0.85716730), vec2(-0.52991926, 0.84804810), vec2(-0.54463904, 0.83867057), vec2(-0.55919290, 0.82903757), vec2(-0.57357644, 0.81915204), // 120
    vec2(-0.58778525, 0.80901699), vec2(-0.60181502, 0.79863551), vec2(-0.61566148, 0.78801075), vec2(-0.62932039, 0.77714596), vec2(-0.64278761, 0.76604444), vec2(-0.65605903, 0.75470958), // 126
    vec2(-0.66913061, 0.74314483), vec2(-0.68199836, 0.73135370), vec2(-0.69465837, 0.71933980), vec2(-0.70710678, 0.70710678), vec2(-0.71933980, 0.69465837), vec2(-0.73135370, 0.68199836), // 132
    vec2(-0.74314483, 0.66913061), vec2(-0.75470958, 0.65605903), vec2(-0.76604444, 0.64278761), vec2(-0.77714596, 0.62932039), vec2(-0.78801075, 0.61566148), vec2(-0.79863551, 0.60181502), // 138
    vec2(-0.80901699, 0.58778525), vec2(-0.81915204, 0.57357644), vec2(-0.82903757, 0.55919290), vec2(-0.83867057, 0.54463904), vec2(-0.84804810, 0.52991926), vec2(-0.85716730, 0.51503807), // 144
    vec2(-0.86602540, 0.50000000), vec2(-0.87461971, 0.48480962), vec2(-0.88294759, 0.46947156), vec2(-0.89100652, 0.45399050), vec2(-0.89879405, 0.43837115), vec2(-0.90630779, 0.42261826), // 150
    vec2(-0.91354546, 0.40673664), vec2(-0.92050485, 0.39073113), vec2(-0.92718385, 0.37460659), vec2(-0.93358043, 0.35836795), vec2(-0.93969262, 0.34202014), vec2(-0.94551858, 0.32556815), // 156
    vec2(-0.95105652, 0.30901699), vec2(-0.95630476, 0.29237170), vec2(-0.96126170, 0.27563736), vec2(-0.96592583, 0.25881905), vec2(-0.97029573, 0.24192190), vec2(-0.97437006, 0.22495105), // 162
    vec2(-0.97814760, 0.20791169), vec2(-0.98162718, 0.19080900), vec2(-0.98480775, 0.17364818), vec2(-0.98768834, 0.15643447), vec2(-0.99026807, 0.13917310), vec2(-0.99254615, 0.12186934), // 168
    vec2(-0.99452190, 0.10452846), vec2(-0.99619470, 0.08715574), vec2(-0.99756405, 0.06975647), vec2(-0.99862953, 0.05233596), vec2(-0.99939083, 0.03489950), vec2(-0.99984770, 0.01745241), // 174
    vec2(-1.00000000, 0.00000000), vec2(-0.99984770, -0.01745241), vec2(-0.99939083, -0.03489950), vec2(-0.99862953, -0.05233596), vec2(-0.99756405, -0.06975647), vec2(-0.99619470, -0.08715574), // 180
    vec2(-0.99452190, -0.10452846), vec2(-0.99254615, -0.12186934), vec2(-0.99026807, -0.13917310), vec2(-0.98768834, -0.15643447), vec2(-0.98480775, -0.17364818), vec2(-0.98162718, -0.19080900), // 186
    vec2(-0.97814760, -0.20791169), vec2(-0.97437006, -0.22495105), vec2(-0.97029573, -0.24192190), vec2(-0.96592583, -0.25881905), vec2(-0.96126170, -0.27563736), vec2(-0.95630476, -0.29237170), // 192
    vec2(-0.95105652, -0.30901699), vec2(-0.94551858, -0.32556815), vec2(-0.93969262, -0.34202014), vec2(-0.93358043, -0.35836795), vec2(-0.92718385, -0.37460659), vec2(-0.92050485, -0.39073113), // 198
    vec2(-0.91354546, -0.40673664), vec2(-0.90630779, -0.42261826), vec2(-0.89879405, -0.43837115), vec2(-0.89100652, -0.45399050), vec2(-0.88294759, -0.46947156), vec2(-0.87461971, -0.48480962), // 204
    vec2(-0.86602540, -0.50000000), vec2(-0.85716730, -0.51503807), vec2(-0.84804810, -0.52991926), vec2(-0.83867057, -0.54463904), vec2(-0.82903757, -0.55919290), vec2(-0.81915204, -0.57357644), // 210
    vec2(-0.80901699, -0.58778525), vec2(-0.79863551, -0.60181502), vec2(-0.78801075, -0.61566148), vec2(-0.77714596, -0.62932039), vec2(-0.76604444, -0.64278761), vec2(-0.75470958, -0.65605903), // 216
    vec2(-0.74314483, -0.66913061), vec2(-0.73135370, -0.68199836), vec2(-0.71933980, -0.69465837), vec2(-0.70710678, -0.70710678), vec2(-0.69465837, -0.71933980), vec2(-0.68199836, -0.73135370), // 222
    vec2(-0.66913061, -0.74314483), vec2(-0.65605903, -0.75470958), vec2(-0.64278761, -0.76604444), vec2(-0.62932039, -0.77714596), vec2(-0.61566148, -0.78801075), vec2(-0.60181502, -0.79863551), // 228
    vec2(-0.58778525, -0.80901699), vec2(-0.57357644, -0.81915204), vec2(-0.55919290, -0.82903757), vec2(-0.54463904, -0.83867057), vec2(-0.52991926, -0.84804810), vec2(-0.51503807, -0.85716730), // 234
    vec2(-0.50000000, -0.86602540), vec2(-0.48480962, -0.87461971), vec2(-0.46947156, -0.88294759), vec2(-0.45399050, -0.89100652), vec2(-0.43837115, -0.89879405), vec2(-0.42261826, -0.90630779), // 240
    vec2(-0.40673664, -0.91354546), vec2(-0.39073113, -0.92050485), vec2(-0.37460659, -0.92718385), vec2(-0.35836795, -0.93358043), vec2(-0.34202014, -0.93969262), vec2(-0.32556815, -0.94551858), // 246
    vec2(-0.30901699, -0.95105652), vec2(-0.29237170, -0.95630476), vec2(-0.27563736, -0.96126170), vec2(-0.25881905, -0.96592583), vec2(-0.24192190, -0.97029573), vec2(-0.22495105, -0.97437006), // 252
    vec2(-0.20791169, -0.97814760), vec2(-0.19080900, -0.98162718), vec2(-0.17364818, -0.98480775), vec2(-0.15643447, -0.98768834), vec2(-0.13917310, -0.99026807), vec2(-0.12186934, -0.99254615), // 258
    vec2(-0.10452846, -0.99452190), vec2(-0.08715574, -0.99619470), vec2(-0.06975647, -0.99756405), vec2(-0.05233596, -0.99862953), vec2(-0.03489950, -0.99939083), vec2(-0.01745241, -0.99984770), // 264
    vec2(-0.00000000, -1.00000000), vec2(0.01745241, -0.99984770), vec2(0.03489950, -0.99939083), vec2(0.05233596, -0.99862953), vec2(0.06975647, -0.99756405), vec2(0.08715574, -0.99619470), // 270
    vec2(0.10452846, -0.99452190), vec2(0.12186934, -0.99254615), vec2(0.13917310, -0.99026807), vec2(0.15643447, -0.98768834), vec2(0.17364818, -0.98480775), vec2(0.19080900, -0.98162718), // 276
    vec2(0.20791169, -0.97814760), vec2(0.22495105, -0.97437006), vec2(0.24192190, -0.97029573), vec2(0.25881905, -0.96592583), vec2(0.27563736, -0.96126170), vec2(0.29237170, -0.95630476), // 282
    vec2(0.30901699, -0.95105652), vec2(0.32556815, -0.94551858), vec2(0.34202014, -0.93969262), vec2(0.35836795, -0.93358043), vec2(0.37460659, -0.92718385), vec2(0.39073113, -0.92050485), // 288
    vec2(0.40673664, -0.91354546), vec2(0.42261826, -0.90630779), vec2(0.43837115, -0.89879405), vec2(0.45399050, -0.89100652), vec2(0.46947156, -0.88294759), vec2(0.48480962, -0.87461971), // 294
    vec2(0.50000000, -0.86602540), vec2(0.51503807, -0.85716730), vec2(0.52991926, -0.84804810), vec2(0.54463904, -0.83867057), vec2(0.55919290, -0.82903757), vec2(0.57357644, -0.81915204), // 300
    vec2(0.58778525, -0.80901699), vec2(0.60181502, -0.79863551), vec2(0.61566148, -0.78801075), vec2(0.62932039, -0.77714596), vec2(0.64278761, -0.76604444), vec2(0.65605903, -0.75470958), // 306
    vec2(0.66913061, -0.74314483), vec2(0.68199836, -0.73135370), vec2(0.69465837, -0.71933980), vec2(0.70710678, -0.70710678), vec2(0.71933980, -0.69465837), vec2(0.73135370, -0.68199836), // 312
    vec2(0.74314483, -0.66913061), vec2(0.75470958, -0.65605903), vec2(0.76604444, -0.64278761), vec2(0.77714596, -0.62932039), vec2(0.78801075, -0.61566148), vec2(0.79863551, -0.60181502), // 318
    vec2(0.80901699, -0.58778525), vec2(0.81915204, -0.57357644), vec2(0.82903757, -0.55919290), vec2(0.83867057, -0.54463904), vec2(0.84804810, -0.52991926), vec2(0.85716730, -0.51503807), // 324
    vec2(0.86602540, -0.50000000), vec2(0.87461971, -0.48480962), vec2(0.88294759, -0.46947156), vec2(0.89100652, -0.45399050), vec2(0.89879405, -0.43837115), vec2(0.90630779, -0.42261826), // 330
    vec2(0.91354546, -0.40673664), vec2(0.92050485, -0.39073113), vec2(0.92718385, -0.37460659), vec2(0.93358043, -0.35836795), vec2(0.93969262, -0.34202014), vec2(0.94551858, -0.32556815), // 336
    vec2(0.95105652, -0.30901699), vec2(0.95630476, -0.29237170), vec2(0.96126170, -0.27563736), vec2(0.96592583, -0.25881905), vec2(0.97029573, -0.24192190), vec2(0.97437006, -0.22495105), // 342
    vec2(0.97814760, -0.20791169), vec2(0.98162718, -0.19080900), vec2(0.98480775, -0.17364818), vec2(0.98768834, -0.15643447), vec2(0.99026807, -0.13917310), vec2(0.99254615, -0.12186934), // 348
    vec2(0.99452190, -0.10452846), vec2(0.99619470, -0.08715574), vec2(0.99756405, -0.06975647), vec2(0.99862953, -0.05233596), vec2(0.99939083, -0.03489950), vec2(0.99984770, -0.01745241), // 354
    vec2(1.00000000, 0.00000000), vec2(0.99984770, 0.01745241), vec2(0.99939083, 0.03489950), vec2(0.99862953, 0.05233596), vec2(0.99756405, 0.06975647), vec2(0.99619470, 0.08715574), // 360
    vec2(0.99452190, 0.10452846), vec2(0.99254615, 0.12186934), vec2(0.99026807, 0.13917310), vec2(0.98768834, 0.15643447), vec2(0.98480775, 0.17364818), vec2(0.98162718, 0.19080900), // 366
    vec2(0.97814760, 0.20791169), vec2(0.97437006, 0.22495105), vec2(0.97029573, 0.24192190), vec2(0.96592583, 0.25881905), vec2(0.96126170, 0.27563736), vec2(0.95630476, 0.29237170), // 372
    vec2(0.95105652, 0.30901699), vec2(0.94551858, 0.32556815), vec2(0.93969262, 0.34202014), vec2(0.93358043, 0.35836795), vec2(0.92718385, 0.37460659), vec2(0.92050485, 0.39073113), // 378
    vec2(0.91354546, 0.40673664), vec2(0.90630779, 0.42261826), vec2(0.89879405, 0.43837115), vec2(0.89100652, 0.45399050), vec2(0.88294759, 0.46947156), vec2(0.87461971, 0.48480962), // 384
    vec2(0.86602540, 0.50000000), vec2(0.85716730, 0.51503807), vec2(0.84804810, 0.52991926), vec2(0.83867057, 0.54463904), vec2(0.82903757, 0.55919290), vec2(0.81915204, 0.57357644), // 390
    vec2(0.80901699, 0.58778525), vec2(0.79863551, 0.60181502), vec2(0.78801075, 0.61566148), vec2(0.77714596, 0.62932039), vec2(0.76604444, 0.64278761), vec2(0.75470958, 0.65605903), // 396
    vec2(0.74314483, 0.66913061), vec2(0.73135370, 0.68199836), vec2(0.71933980, 0.69465837), vec2(0.70710678, 0.70710678), vec2(0.69465837, 0.71933980), vec2(0.68199836, 0.73135370), // 402
    vec2(0.66913061, 0.74314483), vec2(0.65605903, 0.75470958), vec2(0.64278761, 0.76604444), vec2(0.62932039, 0.77714596), vec2(0.61566148, 0.78801075), vec2(0.60181502, 0.79863551), // 408
    vec2(0.58778525, 0.80901699), vec2(0.57357644, 0.81915204), vec2(0.55919290, 0.82903757), vec2(0.54463904, 0.83867057), vec2(0.52991926, 0.84804810), vec2(0.51503807, 0.85716730), // 414
    vec2(0.50000000, 0.86602540), vec2(0.48480962, 0.87461971), vec2(0.46947156, 0.88294759), vec2(0.45399050, 0.89100652), vec2(0.43837115, 0.89879405), vec2(0.42261826, 0.90630779), // 420
    vec2(0.40673664, 0.91354546), vec2(0.39073113, 0.92050485), vec2(0.37460659, 0.92718385), vec2(0.35836795, 0.93358043), vec2(0.34202014, 0.93969262), vec2(0.32556815, 0.94551858), // 426
    vec2(0.30901699, 0.95105652), vec2(0.29237170, 0.95630476), vec2(0.27563736, 0.96126170), vec2(0.25881905, 0.96592583), vec2(0.24192190, 0.97029573), vec2(0.22495105, 0.97437006), // 432
    vec2(0.20791169, 0.97814760), vec2(0.19080900, 0.98162718), vec2(0.17364818, 0.98480775), vec2(0.15643447, 0.98768834), vec2(0.13917310, 0.99026807), vec2(0.12186934, 0.99254615), // 438
    vec2(0.10452846, 0.99452190), vec2(0.08715574, 0.99619470), vec2(0.06975647, 0.99756405), vec2(0.05233596, 0.99862953), vec2(0.03489950, 0.99939083), vec2(0.01745241, 0.99984770), // 444
    vec2(0.00000000, 1.00000000), vec2(-0.01745241, 0.99984770), vec2(-0.03489950, 0.99939083), vec2(-0.05233596, 0.99862953), vec2(-0.06975647, 0.99756405), vec2(-0.08715574, 0.99619470), // 450
    vec2(-0.10452846, 0.99452190), vec2(-0.12186934, 0.99254615), vec2(-0.13917310, 0.99026807), vec2(-0.15643447, 0.98768834), vec2(-0.17364818, 0.98480775), vec2(-0.19080900, 0.98162718), // 456
    vec2(-0.20791169, 0.97814760), vec2(-0.22495105, 0.97437006), vec2(-0.24192190, 0.97029573), vec2(-0.25881905, 0.96592583), vec2(-0.27563736, 0.96126170), vec2(-0.29237170, 0.95630476), // 462
    vec2(-0.30901699, 0.95105652), vec2(-0.32556815, 0.94551858), vec2(-0.34202014, 0.93969262), vec2(-0.35836795, 0.93358043), vec2(-0.37460659, 0.92718385), vec2(-0.39073113, 0.92050485), // 468
    vec2(-0.40673664, 0.91354546), vec2(-0.42261826, 0.90630779), vec2(-0.43837115, 0.89879405), vec2(-0.45399050, 0.89100652), vec2(-0.46947156, 0.88294759), vec2(-0.48480962, 0.87461971), // 474
    vec2(-0.50000000, 0.86602540), vec2(-0.51503807, 0.85716730), vec2(-0.52991926, 0.84804810), vec2(-0.54463904, 0.83867057), vec2(-0.55919290, 0.82903757), vec2(-0.57357644, 0.81915204), // 480
    vec2(-0.58778525, 0.80901699), vec2(-0.60181502, 0.79863551), vec2(-0.61566148, 0.78801075), vec2(-0.62932039, 0.77714596), vec2(-0.64278761, 0.76604444), vec2(-0.65605903, 0.75470958), // 486
    vec2(-0.66913061, 0.74314483), vec2(-0.68199836, 0.73135370), vec2(-0.69465837, 0.71933980), vec2(-0.70710678, 0.70710678), vec2(-0.71933980, 0.69465837), vec2(-0.73135370, 0.68199836), // 492
    vec2(-0.74314483, 0.66913061), vec2(-0.75470958, 0.65605903), vec2(-0.76604444, 0.64278761), vec2(-0.77714596, 0.62932039), vec2(-0.78801075, 0.61566148), vec2(-0.79863551, 0.60181502), // 498
    vec2(-0.80901699, 0.58778525), vec2(-0.81915204, 0.57357644), vec2(-0.82903757, 0.55919290), vec2(-0.83867057, 0.54463904), vec2(-0.84804810, 0.52991926), vec2(-0.85716730, 0.51503807), // 504
    vec2(-0.86602540, 0.50000000), vec2(-0.87461971, 0.48480962), vec2(-0.88294759, 0.46947156), vec2(-0.89100652, 0.45399050), vec2(-0.89879405, 0.43837115), vec2(-0.90630779, 0.42261826), // 510
    vec2(-0.91354546, 0.40673664), vec2(-0.92050485, 0.39073113), vec2(-0.92718385, 0.37460659), vec2(-0.93358043, 0.35836795), vec2(-0.93969262, 0.34202014), vec2(-0.94551858, 0.32556815), // 516
    vec2(-0.95105652, 0.30901699), vec2(-0.95630476, 0.29237170), vec2(-0.96126170, 0.27563736), vec2(-0.96592583, 0.25881905), vec2(-0.97029573, 0.24192190), vec2(-0.97437006, 0.22495105), // 522
    vec2(-0.97814760, 0.20791169), vec2(-0.98162718, 0.19080900), vec2(-0.98480775, 0.17364818), vec2(-0.98768834, 0.15643447), vec2(-0.99026807, 0.13917310), vec2(-0.99254615, 0.12186934), // 528
    vec2(-0.99452190, 0.10452846), vec2(-0.99619470, 0.08715574), vec2(-0.99756405, 0.06975647), vec2(-0.99862953, 0.05233596), vec2(-0.99939083, 0.03489950), vec2(-0.99984770, 0.01745241), // 534
    vec2(-1.00000000, 0.00000000), vec2(-0.99984770, -0.01745241), vec2(-0.99939083, -0.03489950), vec2(-0.99862953, -0.05233596), vec2(-0.99756405, -0.06975647), vec2(-0.99619470, -0.08715574), // 540
    vec2(-0.99452190, -0.10452846), vec2(-0.99254615, -0.12186934), vec2(-0.99026807, -0.13917310), vec2(-0.98768834, -0.15643447), vec2(-0.98480775, -0.17364818), vec2(-0.98162718, -0.19080900), // 546
    vec2(-0.97814760, -0.20791169), vec2(-0.97437006, -0.22495105), vec2(-0.97029573, -0.24192190), vec2(-0.96592583, -0.25881905), vec2(-0.96126170, -0.27563736), vec2(-0.95630476, -0.29237170), // 552
    vec2(-0.95105652, -0.30901699), vec2(-0.94551858, -0.32556815), vec2(-0.93969262, -0.34202014), vec2(-0.93358043, -0.35836795), vec2(-0.92718385, -0.37460659), vec2(-0.92050485, -0.39073113), // 558
    vec2(-0.91354546, -0.40673664), vec2(-0.90630779, -0.42261826), vec2(-0.89879405, -0.43837115), vec2(-0.89100652, -0.45399050), vec2(-0.88294759, -0.46947156), vec2(-0.87461971, -0.48480962), // 564
    vec2(-0.86602540, -0.50000000), vec2(-0.85716730, -0.51503807), vec2(-0.84804810, -0.52991926), vec2(-0.83867057, -0.54463904), vec2(-0.82903757, -0.55919290), vec2(-0.81915204, -0.57357644), // 570
    vec2(-0.80901699, -0.58778525), vec2(-0.79863551, -0.60181502), vec2(-0.78801075, -0.61566148), vec2(-0.77714596, -0.62932039), vec2(-0.76604444, -0.64278761), vec2(-0.75470958, -0.65605903), // 576
    vec2(-0.74314483, -0.66913061), vec2(-0.73135370, -0.68199836), vec2(-0.71933980, -0.69465837), vec2(-0.70710678, -0.70710678), vec2(-0.69465837, -0.71933980), vec2(-0.68199836, -0.73135370), // 582
    vec2(-0.66913061, -0.74314483), vec2(-0.65605903, -0.75470958), vec2(-0.64278761, -0.76604444), vec2(-0.62932039, -0.77714596), vec2(-0.61566148, -0.78801075), vec2(-0.60181502, -0.79863551), // 588
    vec2(-0.58778525, -0.80901699), vec2(-0.57357644, -0.81915204), vec2(-0.55919290, -0.82903757), vec2(-0.54463904, -0.83867057), vec2(-0.52991926, -0.84804810), vec2(-0.51503807, -0.85716730), // 594
    vec2(-0.50000000, -0.86602540), vec2(-0.48480962, -0.87461971), vec2(-0.46947156, -0.88294759), vec2(-0.45399050, -0.89100652), vec2(-0.43837115, -0.89879405), vec2(-0.42261826, -0.90630779), // 600
    vec2(-0.40673664, -0.91354546), vec2(-0.39073113, -0.92050485), vec2(-0.37460659, -0.92718385), vec2(-0.35836795, -0.93358043), vec2(-0.34202014, -0.93969262), vec2(-0.32556815, -0.94551858), // 606
    vec2(-0.30901699, -0.95105652), vec2(-0.29237170, -0.95630476), vec2(-0.27563736, -0.96126170), vec2(-0.25881905, -0.96592583), vec2(-0.24192190, -0.97029573), vec2(-0.22495105, -0.97437006), // 612
    vec2(-0.20791169, -0.97814760), vec2(-0.19080900, -0.98162718), vec2(-0.17364818, -0.98480775), vec2(-0.15643447, -0.98768834), vec2(-0.13917310, -0.99026807), vec2(-0.12186934, -0.99254615), // 618
    vec2(-0.10452846, -0.99452190), vec2(-0.08715574, -0.99619470), vec2(-0.06975647, -0.99756405), vec2(-0.05233596, -0.99862953), vec2(-0.03489950, -0.99939083), vec2(-0.01745241, -0.99984770), // 624
    vec2(-0.00000000, -1.00000000), vec2(0.01745241, -0.99984770), vec2(0.03489950, -0.99939083), vec2(0.05233596, -0.99862953), vec2(0.06975647, -0.99756405), vec2(0.08715574, -0.99619470), // 630
    vec2(0.10452846, -0.99452190), vec2(0.12186934, -0.99254615), vec2(0.13917310, -0.99026807), vec2(0.15643447, -0.98768834), vec2(0.17364818, -0.98480775), vec2(0.19080900, -0.98162718), // 636
    vec2(0.20791169, -0.97814760), vec2(0.22495105, -0.97437006), vec2(0.24192190, -0.97029573), vec2(0.25881905, -0.96592583), vec2(0.27563736, -0.96126170), vec2(0.29237170, -0.95630476), // 642
    vec2(0.30901699, -0.95105652), vec2(0.32556815, -0.94551858), vec2(0.34202014, -0.93969262), vec2(0.35836795, -0.93358043), vec2(0.37460659, -0.92718385), vec2(0.39073113, -0.92050485), // 648
    vec2(0.40673664, -0.91354546), vec2(0.42261826, -0.90630779), vec2(0.43837115, -0.89879405), vec2(0.45399050, -0.89100652), vec2(0.46947156, -0.88294759), vec2(0.48480962, -0.87461971), // 654
    vec2(0.50000000, -0.86602540), vec2(0.51503807, -0.85716730), vec2(0.52991926, -0.84804810), vec2(0.54463904, -0.83867057), vec2(0.55919290, -0.82903757), vec2(0.57357644, -0.81915204), // 660
    vec2(0.58778525, -0.80901699), vec2(0.60181502, -0.79863551), vec2(0.61566148, -0.78801075), vec2(0.62932039, -0.77714596), vec2(0.64278761, -0.76604444), vec2(0.65605903, -0.75470958), // 666
    vec2(0.66913061, -0.74314483), vec2(0.68199836, -0.73135370), vec2(0.69465837, -0.71933980), vec2(0.70710678, -0.70710678), vec2(0.71933980, -0.69465837), vec2(0.73135370, -0.68199836), // 672
    vec2(0.74314483, -0.66913061), vec2(0.75470958, -0.65605903), vec2(0.76604444, -0.64278761), vec2(0.77714596, -0.62932039), vec2(0.78801075, -0.61566148), vec2(0.79863551, -0.60181502), // 678
    vec2(0.80901699, -0.58778525), vec2(0.81915204, -0.57357644), vec2(0.82903757, -0.55919290), vec2(0.83867057, -0.54463904), vec2(0.84804810, -0.52991926), vec2(0.85716730, -0.51503807), // 684
    vec2(0.86602540, -0.50000000), vec2(0.87461971, -0.48480962), vec2(0.88294759, -0.46947156), vec2(0.89100652, -0.45399050), vec2(0.89879405, -0.43837115), vec2(0.90630779, -0.42261826), // 690
    vec2(0.91354546, -0.40673664), vec2(0.92050485, -0.39073113), vec2(0.92718385, -0.37460659), vec2(0.93358043, -0.35836795), vec2(0.93969262, -0.34202014), vec2(0.94551858, -0.32556815), // 696
    vec2(0.95105652, -0.30901699), vec2(0.95630476, -0.29237170), vec2(0.96126170, -0.27563736), vec2(0.96592583, -0.25881905), vec2(0.97029573, -0.24192190), vec2(0.97437006, -0.22495105), // 702
    vec2(0.97814760, -0.20791169), vec2(0.98162718, -0.19080900), vec2(0.98480775, -0.17364818), vec2(0.98768834, -0.15643447), vec2(0.99026807, -0.13917310), vec2(0.99254615, -0.12186934), // 708
    vec2(0.99452190, -0.10452846), vec2(0.99619470, -0.08715574), vec2(0.99756405, -0.06975647), vec2(0.99862953, -0.05233596), vec2(0.99939083, -0.03489950), vec2(0.99984770, -0.01745241), // 714
    vec2(1.00000000, 0.00000000) // 720        
};

