/*
 * trans.c - Matrix transpose B = A^T
 *
 * Each transpose function must have a prototype of the form:
 * void trans(int M, int N, int A[N][M], int B[M][N]);
 *
 * A transpose function is evaluated by counting the number of misses
 * on a 1KB direct mapped cache with a block size of 32 bytes.
 */
#include "cachelab.h"
#include <stdio.h>

int is_transpose(int M, int N, int A[N][M], int B[M][N]);

/*
 * transpose_submit - This is the solution transpose function that you
 *     will be graded on for Part B of the assignment. Do not change
 *     the description string "Transpose submission", as the driver
 *     searches for that string to identify the transpose function to
 *     be graded.
 */
char transpose_submit_desc[] = "Transpose submission";
void transpose_submit(int M, int N, int A[N][M], int B[M][N]) {
  int bsize = 8, bj, bi, i, j;
  // int tmp;

  for (bj = 0; bj < M; bj += bsize) {
    for (bi = 0; bi < N; bi += bsize) {
      for (i = bi; i < bi + bsize && i < N; i++) {
        for (j = bj; j < bj + bsize && j < M; j++) {
          // printf("swapping %d %d", i, j);
          B[j][i] = A[i][j];
          // else {
          //   tmp = A[i][j];
          //   B[i][j] = tmp;
          // }
        }
      }
    }
  }
}
char transpose_submit_4_desc[] = "Transpose submission 4 bsize";
void transpose_submit_4(int M, int N, int A[N][M], int B[M][N]) {
  int bsize = 4, bj, bi, i, j;
  // int tmp;

  for (bj = 0; bj < M; bj += bsize) {
    for (bi = 0; bi < N; bi += bsize) {
      for (i = bi; i < bi + bsize && i < N; i++) {
        for (j = bj; j < bj + bsize && j < M; j++) {
          // printf("swapping %d %d", i, j);
          B[j][i] = A[i][j];
          // else {
          //   tmp = A[i][j];
          //   B[i][j] = tmp;
          // }
        }
      }
    }
  }
}

char transpose_submit_16_desc[] = "Transpose submission 16 bsize";
void transpose_submit_16(int M, int N, int A[N][M], int B[M][N]) {
  int bsize = 16, bj, bi, i, j;
  // int tmp;

  for (bj = 0; bj < M; bj += bsize) {
    for (bi = 0; bi < N; bi += bsize) {
      for (i = bi; i < bi + bsize && i < N; i++) {
        for (j = bj; j < bj + bsize && j < M; j++) {
          // printf("swapping %d %d", i, j);
          B[j][i] = A[i][j];
          // else {
          //   tmp = A[i][j];
          //   B[i][j] = tmp;
          // }
        }
      }
    }
  }
}
/*
 * You can define additional transpose functions below. We've defined
 * a simple one below to help you get started.
 */

/*
 * trans - A simple baseline transpose function, not optimized for the cache.
 */
char trans_desc[] = "Simple row-wise scan transpose";
void trans(int M, int N, int A[N][M], int B[M][N]) {
  int i, j;

  for (i = 0; i < N; i++) {
    for (j = 0; j < M; j++) {
      // tmp = A[i][j];
      // B[j][i] = tmp;
      B[j][i] = A[i][j];
    }
  }
}

/*
 * registerFunctions - This function registers your transpose
 *     functions with the driver.  At runtime, the driver will
 *     evaluate each of the registered functions and summarize their
 *     performance. This is a handy way to experiment with different
 *     transpose strategies.
 */
void registerFunctions() {

  /* Register any additional transpose functions */
  registerTransFunction(trans, trans_desc);

  /* Register your solution function */
  registerTransFunction(transpose_submit, transpose_submit_desc);
  registerTransFunction(transpose_submit_4, transpose_submit_4_desc);
  registerTransFunction(transpose_submit_16, transpose_submit_16_desc);
}

/*
 * is_transpose - This helper function checks if B is the transpose of
 *     A. You can check the correctness of your transpose by calling
 *     it before returning from the transpose function.
 */
int is_transpose(int M, int N, int A[N][M], int B[M][N]) {
  int i, j;

  for (i = 0; i < N; i++) {
    for (j = 0; j < M; ++j) {
      if (A[i][j] != B[j][i]) {
        return 0;
      }
    }
  }
  return 1;
}
