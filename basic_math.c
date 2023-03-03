#include <stdio.h>

#define ABS(x) ((x) > 0 ? (x) : (-(x)))
#define GET_BIT(x, i) (((x) >> (i)) & 1)
#define PRINT_RES(func, ...) ((printf("Input : %f, Output: %f\n", (__VA_ARGS__), (func)(__VA_ARGS__))))

float f_sqrt(float x) {
	float errorTol = 1e-8;
	int maxIter = 1000;
	int nIter = 0;
	int running = 1;
	float result = x + 1;
	float nextResult;
	while (running) {
		nextResult = (result + x / result) / 2;
		if (ABS(nextResult - result) < errorTol) {
			running = 0;
		}
		if (nIter++ > maxIter) {
			running = 0;
		}
		result = nextResult;
	}
	return result;
}

float f_pow(float base, int exponent) {
	float result = 1;
	float current_square_value = base;
	for (size_t i = 0; i < sizeof(int); i++) {
		if (GET_BIT(exponent, i)) {
			result *= current_square_value;
		}
		current_square_value *= current_square_value;
	}
	return result;
}
