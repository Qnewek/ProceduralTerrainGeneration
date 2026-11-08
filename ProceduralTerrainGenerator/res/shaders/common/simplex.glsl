// 2D and 4D simplex noise, ported from src/vendor/Simplex/SimplexNoise.cpp (of which the app now uses only makePermutation).
// The file including this one provides: int permAt(int table, int i), returning entry i & 255 of a permutation table.

float grad2(int hash, float x, float y)
{
	int h = hash & 63;
	float u = h < 4 ? x : y;
	float v = h < 4 ? y : x;
	return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -2.0 * v : 2.0 * v);
}

float grad4(int hash, float x, float y, float z, float t)
{
	int h = hash & 31;
	float u = h < 24 ? x : y;
	float v = h < 16 ? y : z;
	float w = h < 8 ? z : t;
	return ((h & 1) != 0 ? -u : u) + ((h & 2) != 0 ? -v : v) + ((h & 4) != 0 ? -w : w);
}

float simplex2(int table, float x, float y)
{
	const float F2 = 0.366025403;
	const float G2 = 0.211324865;

	float s = (x + y) * F2;
	int i = int(floor(x + s));
	int j = int(floor(y + s));

	float t = float(i + j) * G2;
	float x0 = x - (float(i) - t);
	float y0 = y - (float(j) - t);

	int i1 = x0 > y0 ? 1 : 0;
	int j1 = x0 > y0 ? 0 : 1;

	float x1 = x0 - float(i1) + G2;
	float y1 = y0 - float(j1) + G2;
	float x2 = x0 - 1.0 + 2.0 * G2;
	float y2 = y0 - 1.0 + 2.0 * G2;

	float sum = 0.0;
	float t0 = 0.5 - x0 * x0 - y0 * y0;
	if (t0 >= 0.0) {
		t0 *= t0;
		sum += t0 * t0 * grad2(permAt(table, i + permAt(table, j)), x0, y0);
	}
	float t1 = 0.5 - x1 * x1 - y1 * y1;
	if (t1 >= 0.0) {
		t1 *= t1;
		sum += t1 * t1 * grad2(permAt(table, i + i1 + permAt(table, j + j1)), x1, y1);
	}
	float t2 = 0.5 - x2 * x2 - y2 * y2;
	if (t2 >= 0.0) {
		t2 *= t2;
		sum += t2 * t2 * grad2(permAt(table, i + 1 + permAt(table, j + 1)), x2, y2);
	}
	return 45.23065 * sum;
}

// Gradient vector g of grad2: grad2(hash, x, y) = dot(g, vec2(x, y))
vec2 grad2Vector(int hash)
{
	int h = hash & 63;
	float su = (h & 1) != 0 ? -1.0 : 1.0;
	float sv = (h & 2) != 0 ? -2.0 : 2.0;
	return h < 4 ? vec2(su, sv) : vec2(sv, su);
}

// Contribution of one corner and its derivatives: t^4 * dot(g, d)
vec3 simplex2Corner(int hash, float x, float y)
{
	float t = 0.5 - x * x - y * y;
	if (t < 0.0) {
		return vec3(0.0);
	}
	vec2 g = grad2Vector(hash);
	float dotGD = dot(g, vec2(x, y));
	float t2 = t * t;
	float t4 = t2 * t2;
	// d/dx t^4 = 4 t^3 * (-2x)
	return vec3(t4 * dotGD, -8.0 * t2 * t * dotGD * vec2(x, y) + t4 * g);
}

// simplex2 with its derivatives: (value, d/dx, d/dy). The value can differ from simplex2 in the last bits (other order of operations)
vec3 simplex2Grad(int table, float x, float y)
{
	const float F2 = 0.366025403;
	const float G2 = 0.211324865;

	float s = (x + y) * F2;
	int i = int(floor(x + s));
	int j = int(floor(y + s));

	float t = float(i + j) * G2;
	float x0 = x - (float(i) - t);
	float y0 = y - (float(j) - t);

	int i1 = x0 > y0 ? 1 : 0;
	int j1 = x0 > y0 ? 0 : 1;

	vec3 sum = simplex2Corner(permAt(table, i + permAt(table, j)), x0, y0);
	sum += simplex2Corner(permAt(table, i + i1 + permAt(table, j + j1)), x0 - float(i1) + G2, y0 - float(j1) + G2);
	sum += simplex2Corner(permAt(table, i + 1 + permAt(table, j + 1)), x0 - 1.0 + 2.0 * G2, y0 - 1.0 + 2.0 * G2);
	return 45.23065 * sum;
}

int simplex4Hash(int table, int i, int j, int k, int l)
{
	return permAt(table, i + permAt(table, j + permAt(table, k + permAt(table, l))));
}

float simplex4(int table, float x, float y, float z, float w)
{
	const float F4 = 0.309016994;	// (sqrt(5) - 1) / 4
	const float G4 = 0.138196601;	// (5 - sqrt(5)) / 20

	float s = (x + y + z + w) * F4;
	int i = int(floor(x + s));
	int j = int(floor(y + s));
	int k = int(floor(z + s));
	int l = int(floor(w + s));
	float t = float(i + j + k + l) * G4;
	float x0 = x - (float(i) - t);
	float y0 = y - (float(j) - t);
	float z0 = z - (float(k) - t);
	float w0 = w - (float(l) - t);

	int rankx = 0, ranky = 0, rankz = 0, rankw = 0;
	if (x0 > y0) rankx++; else ranky++;
	if (x0 > z0) rankx++; else rankz++;
	if (x0 > w0) rankx++; else rankw++;
	if (y0 > z0) ranky++; else rankz++;
	if (y0 > w0) ranky++; else rankw++;
	if (z0 > w0) rankz++; else rankw++;

	int i1 = rankx >= 3 ? 1 : 0, j1 = ranky >= 3 ? 1 : 0, k1 = rankz >= 3 ? 1 : 0, l1 = rankw >= 3 ? 1 : 0;
	int i2 = rankx >= 2 ? 1 : 0, j2 = ranky >= 2 ? 1 : 0, k2 = rankz >= 2 ? 1 : 0, l2 = rankw >= 2 ? 1 : 0;
	int i3 = rankx >= 1 ? 1 : 0, j3 = ranky >= 1 ? 1 : 0, k3 = rankz >= 1 ? 1 : 0, l3 = rankw >= 1 ? 1 : 0;

	float x1 = x0 - float(i1) + G4, y1 = y0 - float(j1) + G4, z1 = z0 - float(k1) + G4, w1 = w0 - float(l1) + G4;
	float x2 = x0 - float(i2) + 2.0 * G4, y2 = y0 - float(j2) + 2.0 * G4, z2 = z0 - float(k2) + 2.0 * G4, w2 = w0 - float(l2) + 2.0 * G4;
	float x3 = x0 - float(i3) + 3.0 * G4, y3 = y0 - float(j3) + 3.0 * G4, z3 = z0 - float(k3) + 3.0 * G4, w3 = w0 - float(l3) + 3.0 * G4;
	float x4 = x0 - 1.0 + 4.0 * G4, y4 = y0 - 1.0 + 4.0 * G4, z4 = z0 - 1.0 + 4.0 * G4, w4 = w0 - 1.0 + 4.0 * G4;

	float sum = 0.0;
	float t0 = 0.6 - x0 * x0 - y0 * y0 - z0 * z0 - w0 * w0;
	if (t0 >= 0.0) {
		t0 *= t0;
		sum += t0 * t0 * grad4(simplex4Hash(table, i, j, k, l), x0, y0, z0, w0);
	}
	float t1 = 0.6 - x1 * x1 - y1 * y1 - z1 * z1 - w1 * w1;
	if (t1 >= 0.0) {
		t1 *= t1;
		sum += t1 * t1 * grad4(simplex4Hash(table, i + i1, j + j1, k + k1, l + l1), x1, y1, z1, w1);
	}
	float t2 = 0.6 - x2 * x2 - y2 * y2 - z2 * z2 - w2 * w2;
	if (t2 >= 0.0) {
		t2 *= t2;
		sum += t2 * t2 * grad4(simplex4Hash(table, i + i2, j + j2, k + k2, l + l2), x2, y2, z2, w2);
	}
	float t3 = 0.6 - x3 * x3 - y3 * y3 - z3 * z3 - w3 * w3;
	if (t3 >= 0.0) {
		t3 *= t3;
		sum += t3 * t3 * grad4(simplex4Hash(table, i + i3, j + j3, k + k3, l + l3), x3, y3, z3, w3);
	}
	float t4 = 0.6 - x4 * x4 - y4 * y4 - z4 * z4 - w4 * w4;
	if (t4 >= 0.0) {
		t4 *= t4;
		sum += t4 * t4 * grad4(simplex4Hash(table, i + 1, j + 1, k + 1, l + 1), x4, y4, z4, w4);
	}
	return 27.0 * sum;
}
