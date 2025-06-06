#include <iostream>
#include <random>
#include <chrono>
#include <vector>
#include <algorithm>
#include <cmath>
#include <complex>
#include <omp.h>
#include <immintrin.h>
#include <cblas.h>

using namespace std;
using namespace std::complex_literals;

void matrixMultiply(const vector<complex<double>>& A, const vector<complex<double>>& B,
                   vector<complex<double>>& C, int n) {
    #pragma omp parallel for collapse(2)
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            complex<double> sum = 0.0;
            for (int k = 0; k < n; ++k) {
                sum += A[i * n + k] * B[k * n + j];
            }
            C[i * n + j] = sum;
        }
    }
}

void matrixMultiplyOptimized(const vector<complex<double>>& A,
                           const vector<complex<double>>& B,
                           vector<complex<double>>& C, int n) {
    constexpr int BLOCK_SIZE = 64;  
    constexpr int UNROLL = 4;       
    
    #pragma omp parallel for collapse(2) schedule(dynamic)
    for (int ii = 0; ii < n; ii += BLOCK_SIZE) {
        for (int jj = 0; jj < n; jj += BLOCK_SIZE) {
            alignas(32) complex<double> CB[BLOCK_SIZE][BLOCK_SIZE] = {{0.0}};
            
            for (int kk = 0; kk < n; kk += BLOCK_SIZE) {
                const int i_end = min(ii + BLOCK_SIZE, n);
                const int j_end = min(jj + BLOCK_SIZE, n);
                const int k_end = min(kk + BLOCK_SIZE, n);
                
                for (int k = kk; k < k_end; ++k) {
                    for (int i = ii; i < i_end; i += UNROLL) {
                        __m256d a0 = _mm256_loadu_pd(reinterpret_cast<const double*>(&A[i*n + k]));
                        __m256d a1 = _mm256_loadu_pd(reinterpret_cast<const double*>(&A[(i+1)*n + k]));
                        __m256d a2 = _mm256_loadu_pd(reinterpret_cast<const double*>(&A[(i+2)*n + k]));
                        __m256d a3 = _mm256_loadu_pd(reinterpret_cast<const double*>(&A[(i+3)*n + k]));
                        
                        for (int j = jj; j < j_end; ++j) {
                            __m256d b = _mm256_broadcast_pd(reinterpret_cast<const __m128d*>(&B[k*n + j]));
                            
                            __m256d res0 = _mm256_loadu_pd(reinterpret_cast<double*>(&CB[i-ii][j-jj]));
                            res0 = _mm256_add_pd(res0, _mm256_mul_pd(a0, b));
                            _mm256_storeu_pd(reinterpret_cast<double*>(&CB[i-ii][j-jj]), res0);
                            
                            if (i+1 < i_end) {
                                __m256d res1 = _mm256_loadu_pd(reinterpret_cast<double*>(&CB[i-ii+1][j-jj]));
                                res1 = _mm256_add_pd(res1, _mm256_mul_pd(a1, b));
                                _mm256_storeu_pd(reinterpret_cast<double*>(&CB[i-ii+1][j-jj]), res1);
                            }
                            if (i+2 < i_end) {
                                __m256d res2 = _mm256_loadu_pd(reinterpret_cast<double*>(&CB[i-ii+2][j-jj]));
                                res2 = _mm256_add_pd(res2, _mm256_mul_pd(a2, b));
                                _mm256_storeu_pd(reinterpret_cast<double*>(&CB[i-ii+2][j-jj]), res2);
                            }
                            if (i+3 < i_end) {
                                __m256d res3 = _mm256_loadu_pd(reinterpret_cast<double*>(&CB[i-ii+3][j-jj]));
                                res3 = _mm256_add_pd(res3, _mm256_mul_pd(a3, b));
                                _mm256_storeu_pd(reinterpret_cast<double*>(&CB[i-ii+3][j-jj]), res3);
                            }
                        }
                    }
                }
            }
            
            
            for (int i = ii; i < min(ii + BLOCK_SIZE, n); ++i) {
                for (int j = jj; j < min(jj + BLOCK_SIZE, n); ++j) {
                    C[i * n + j] = CB[i - ii][j - jj];
                }
            }
        }
    }
}

void generate_random_matrix(vector<complex<double>>& matrix, int n) {
    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<double> dis_real(0.0, 1.0);
    uniform_real_distribution<double> dis_imag(0.0, 1.0);

    #pragma omp parallel for
    for (int i = 0; i < n * n; ++i) {
        matrix[i] = complex<double>(dis_real(gen), dis_imag(gen));
    }
}

int main() {
    setlocale(LC_ALL, "rus");

    const int n = 4096;  
    const double complexity = 8.0 * n * n * n;  

    vector<complex<double>> A(n * n);
    vector<complex<double>> B(n * n);
    vector<complex<double>> C(n * n, 0.0);

    cout << "Создание случайных матриц " << n << "×" << n << "..." << endl;
    generate_random_matrix(A, n);
    generate_random_matrix(B, n);

    
    cout << "\n1. Стандартный метод перемножения..." << endl;
    auto start = chrono::high_resolution_clock::now();
    matrixMultiply(A, B, C, n);
    auto end = chrono::high_resolution_clock::now();
    double time_basic = chrono::duration<double>(end - start).count();
    double mflops_basic = (complexity / time_basic) * 1e-6;
    cout << "Время: " << time_basic << " с, Производительность: " 
         << mflops_basic << " MFlops" << endl;
    fill(C.begin(), C.end(), 0.0);

   
    cout << "\n2. Перемножение с использованием BLAS (zgemm)..." << endl;
    complex<double> alpha = 1.0;
    complex<double> beta = 0.0;
    start = chrono::high_resolution_clock::now();
    cblas_zgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
                n, n, n, &alpha, A.data(), n, B.data(), n, &beta, C.data(), n);
    end = chrono::high_resolution_clock::now();
    double time_blas = chrono::duration<double>(end - start).count();
    double mflops_blas = (complexity / time_blas) * 1e-6;
    cout << "Время: " << time_blas << " с, Производительность: " 
         << mflops_blas << " MFlops" << endl;
    fill(C.begin(), C.end(), 0.0);

    
    cout << "\n3. Оптимизированный метод (блочный + векторизация)..." << endl;
    start = chrono::high_resolution_clock::now();
    matrixMultiplyOptimized(A, B, C, n);
    end = chrono::high_resolution_clock::now();
    double time_opt = chrono::duration<double>(end - start).count();
    double mflops_opt = (complexity / time_opt) * 1e-6;
    cout << "Время: " << time_opt << " с, Производительность: " 
         << mflops_opt << " MFlops" << endl;

    return 0;
}