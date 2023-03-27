#include <stdarg.h>
#include "basic_math.c"

int LogErrorExact(float expectedOutput, float output, float *inputs, size_t nInputs)
{
	int error = !(ABS(expectedOutput - output) < 1e-8);
	if (error)
	{
		printf("Expected %lf, got %lf for inputs", expectedOutput, output);
		for (size_t i = 0; i < nInputs; i++)
		{
			printf(", %lf", inputs[i]);
		}
		printf("\n");
	}
	return error;
}

int LogErrorLeq(float expectedOutput, float output, float errorTol, float *inputs, size_t nInputs) {
	float absError = ABS(expectedOutput - output);
	int error = !(absError < errorTol);
	if (error)
	{
		printf("Expected diff of %lf and %lf to be under %lf, got %lf for inputs", output, expectedOutput, errorTol, absError);
		for (size_t i = 0; i < nInputs; i++) {
			printf(", %lf", inputs[i]);
		}
		printf("\n");
	}
	return error;
}

void RunTestFunction(int testF(void), char *fName)

{
	int nErrors = testF();
	if (nErrors == 0)
	{
		printf("%s passed\n", fName);
	} else
	{
		printf("%s failed with %d errors\n", fName, nErrors);
	}
	printf("\n============\n");

}

int TestFSqrt()
{
	float inputs[] = {1, 0.001, 4, 100};
	float expectedOutputs[] = {1, 0.0316227749, 2, 10};
	printf("Testing FSqrt\n");
	int n_errors = 0;
	for (size_t i = 0; i < 4; i++)
	{
		float res = FSqrt(inputs[i]);
		n_errors += LogErrorExact(expectedOutputs[i], res, inputs + i, 1);
	}
	return n_errors;
}

int TestFPow()
{
	printf("Testing FPow\n");
	float baseInputs[] = {1, 10, 0.1, 3};
	int expInputs[] = {0, 1, 2, 5};
	float expectedRes[16] = {
		1, 1, 1, 1,
		1, 10, 100, 100000,
		1, 0.1, 0.01, 0.00001,
		1, 3, 9, 243
	};
	int nErrors = 0;
	for (size_t i = 0; i < 4; i++)
	{
		float base = baseInputs[i];
		for (size_t j = 0; j < 4; j++)
		{
			float exp = expInputs[j];
			float inputs[2] = {base, exp};
			float expected = expectedRes[4 * i + j];
			nErrors += LogErrorExact(expected, FPow(base, exp), inputs, 2);
		}
	}
	return nErrors;
}

void TestSwap() {
	float a = 5;
	float b = 6;
	Swap(&a, &b);
	printf("a: %f, b: %f", a, b);
}

int TestGaussJordan() {
	printf("Testing GaussJordan \n");
	int nErrors = 0;
	size_t rowCases[4] = {8, 32, 128, 256};
	for (size_t testCase = 0; testCase < 4; testCase++) {
		size_t rows = rowCases[testCase];
		printf("Size %d\n", (int)rows);
		matrix_f32 id = Identityf32(rows);
		matrix_f32 a = RandomMatrixf32(rows, rows);
		matrix_f32 aInv = Copy(&a);
		GaussJordan(&aInv, NULL);
		matrix_f32 result = MatrixMicrokernelMultiply(&a, &aInv);
		float maxError = GetMaxError(&id, &result);
		float relErrorTol = 1e-6;
		float errorTol = ((float)rows) * relErrorTol;
		int doesNotPass = LogErrorLeq(0, maxError, errorTol, 0, 0);
		if (!doesNotPass) printf("Ok at %f\n", relErrorTol);
		nErrors += doesNotPass;
		if (rows < 10) {
			printf("Result: \n");
			PrintMatrix(&result);
		}
	}
	return nErrors;
}

int TestLU() {
	printf("Testing LU\n");
	int nErrors = 0;
	size_t rowCases[4] = {8, 32, 128, 256};
	for (size_t testCase = 0; testCase < 4; testCase++) {
		size_t rows = rowCases[testCase];
		printf("Size %d\n", (int)rows);
		matrix_f32 id = Identityf32(rows);
		matrix_f32 idCopy = Copy(&id);
		matrix_f32 a = RandomMatrixf32(rows, rows);
		matrix_f32 aLU = Copy(&a);
		LUFactorize(&aLU, 0);
		LUBackSub(&aLU, &id);
		// At this point our "id" matrix should be replaced with the inverse of A
		matrix_f32 result = Multiply(&a, &id);
		float relErrorTol = 1e-6;
		float errorTol = ((float)rows) * relErrorTol;
		float maxError = GetMaxError(&result, &idCopy);
		int doesNotPass = LogErrorLeq(0, maxError, errorTol, 0, 0);
		if (!doesNotPass) printf("Ok at %f\n", relErrorTol);
		nErrors += doesNotPass;
		if (rows < 10) {
			printf("Result: \n");
			PrintMatrix(&result);
		}
	}
	return nErrors;
}

void MatrixLUPopulate(matrix_f32 *lu, matrix_f32 *l, matrix_f32 *u) {
	for (size_t i = 0; i < l->rows; i++) {
		for (size_t j = 0; j < l->cols; j++) {
			if (i < j) {
				MATRIX_ITEM(u, i, j) = MATRIX_ITEM(lu, i, j);
				MATRIX_ITEM(l, i, j) = 0;
			}
			else if (i == j) {
				MATRIX_ITEM(u, i, j) = MATRIX_ITEM(lu, i, j);
				MATRIX_ITEM(l, i, j) = 1;
			}
			else {
				MATRIX_ITEM(u, i, j) = 0;
				MATRIX_ITEM(l, i, j) = MATRIX_ITEM(lu, i, j);
			}
		}
	}
}


int TestLUFactorize() {
	size_t n = 100;
	int nErrors = 0;
	matrix_f32 l = LowerRandomf32(n);
	for (size_t i = 0; i < n; i++) {
		MATRIX_ITEM(&l, i, i) = 1;  // Enforce L to be unitary for solution uniqueness
	}
	matrix_f32 u = UpperRandomf32(n);
	matrix_f32 a = Multiply(&l, &u);
	matrix_f32 aCopy = Copy(&a);
	size_t *index = malloc(n * sizeof(size_t));
	LUFactorize(&a, index);
	MatrixLUPopulate(&a, &l, &u);
	for (size_t i = 0; i < n; i++) {
		for (size_t j = 0; j < n; j++) {
			Swap(MatrixGetAddr(&aCopy, index[i], j), MatrixGetAddr(&aCopy, i, j));
		}
	}
	matrix_f32 result = Multiply(&l, &u);
	float relErrorTol = 1e-8;
	float errorTol = ((float)n) * relErrorTol;
	float maxError = GetMaxError(&aCopy, &result);
	int doesNotPass = LogErrorLeq(0, maxError, errorTol, 0, 0);
		if (!doesNotPass) printf("Ok at %f\n", relErrorTol);
		nErrors += doesNotPass;
		if (n < 10) {
			printf("Result: \n");
			MultiplyInplace(&aCopy, -1);
			matrix_f32 error = Sum(&aCopy, &result);
			PrintMatrix(&error);
		}
	return nErrors;
}


int main()
{
	RunTestFunction(TestFSqrt, "FSqrt");
	RunTestFunction(TestFPow, "FPow");
	RunTestFunction(TestGaussJordan, "GaussJordan");
	RunTestFunction(TestLUFactorize, "LU Factorize");
	// RunTestFunction(TestLU, "LUFactorize + LUBackSub");

}
