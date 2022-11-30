// C++ program to find adjoint and inverse of a matrix
#include <bits/stdc++.h>
using namespace std;
#define N 6

// Function to get cofactor of A[p][q] in temp[][]. n is
// current dimension of A[][]
void getCofactor(double A[N][N], double temp[N][N], int p, int q,
				int n)
{
	int i = 0, j = 0;

	// Looping for each element of the matrix
	for (int row = 0; row < n; row++) {
		for (int col = 0; col < n; col++) {
			// Copying into temporary matrix only those
			// element which are not in given row and
			// column
			if (row != p && col != q) {
				temp[i][j++] = A[row][col];

				// Row is filled, so increase row index and
				// reset col index
				if (j == n - 1) {
					j = 0;
					i++;
				}
			}
		}
	}
}

/* Recursive function for finding determinant of matrix.
n is current dimension of A[][]. */
double determinant(double A[N][N], double n)
{	
	//std::cout << "Solve for determinant";
	double D = 0; // Initialize result

	// Base case : if matrix contains single element
	if (n == 1)
		return A[0][0];

	double temp[N][N]; // To store cofactors

	double sign = 1; // To store sign multiplier

	// Iterate for each element of first row
	for (int f = 0; f < n; f++) {
		// Getting Cofactor of A[0][f]
		getCofactor(A, temp, 0, f, n);
		D += sign * A[0][f] * determinant(temp, n - 1);

		// terms are to be added with alternate sign
		sign = -sign;
	}

	return D;
}

// Function to get adjoint of A[N][N] in adj[N][N].
void adjoint(double A[N][N], double adj[N][N])
{
	//cout << "inverse.hpp--- solve for adjoint\n" ;
	if (N == 1) {
		adj[0][0] = 1;
		return;
	}

	// temp is used to store cofactors of A[][]
	double sign = 1, temp[N][N];

	for (int i = 0; i < N; i++) {
		for (int j = 0; j < N; j++) {
			// Get cofactor of A[i][j]
			getCofactor(A, temp, i, j, N);

			// sign of adj[j][i] positive if sum of row
			// and column indexes is even.
			sign = ((i + j) % 2 == 0) ? 1 : -1;

			// Interchanging rows and columns to get the
			// transpose of the cofactor matrix
			adj[j][i] = (sign) * (determinant(temp, N - 1));
		}
	}
}

// Function to calculate and store inverse, returns false if
// matrix is singular
bool inverse(double A[N][N], float inverse[N][N])
{
	//cout << "inverse.hpp--- solve for inverse\n" ;
	// Find determinant of A[][]
	double det = determinant(A, N);
	//cout << "Determinant (double): " << det <<"\n";

	if (det == 0) {
		cout << "Singular matrix, can't find its inverse";
		return false;
	}

	// Find adjoint
	double adj[N][N];
	//cout << "Within inv. fct, find adjoint";
	adjoint(A, adj);
	//cout << "Adjoint: \n";
	//for (int i = 0; i < N; i++) {
	//	for (int j = 0; j < N; j++){
	//	std::cout << adj[i][j] << " ";}
	//std::cout << endl;}

	// Find Inverse using formula "inverse(A) =
	// adj(A)/det(A)"
	for (int i = 0; i < N; i++)
		for (int j = 0; j < N; j++)
			inverse[i][j] = adj[i][j] / det;
	return true;
}

// Generic function to display the matrix. We use it to
// display both adjoin and inverse. adjoin is integer matrix
// and inverse is a float.
//template <class T> void display(T A[N][N])
//{
//	for (int i = 0; i < N; i++) {
//		for (int j = 0; j < N; j++)
//			cout << A[i][j] << " ";
//		cout << endl;
//	}
//}

// Driver program
//int main()
//{
    // intact rock stiffness c_i
    //double c_i[6][6] = {
    //    { 38.3, 4.80, 4.80, 0.0, 0.0, 0.0},
    //    { 4.80, 38.3, 4.80, 0.0, 0.0, 0.0},
    //    { 4.80, 4.80, 38.3, 0.0, 0.0, 0.0},
    //     { 0.0, 0.0, 0.0, 17.02, 0.0, 0.0},
    //     { 0.0, 0.0, 0.0, 0.0, 17.02, 0.0},
    //     { 0.0, 0.0, 0.0, 0.0, 0.0, 17.02}
    // };
    
	//double adj[6][6]; // To store adjoint of A[][]

	//float inv[6][6]; // To store inverse of A[][]

    //cout << "Input matrix is :\n";
    //for (int i = 0; i < N; i++) {
	//	for (int j = 0; j < N; j++)
	//		cout << c_i[i][j] << " ";
	//	cout << endl;
	//}
	//display(c_i);

    //cout << "\nThe Adjoint is :\n";
	//adjoint(c_i, adj);
	//display(adj);
    //for (int i = 0; i < N; i++) {
	//	for (int j = 0; j < N; j++)
	//		cout << adj[i][j] << " ";
	//	cout << endl;
	//}

    //cout << "\nThe Inverse is :\n";
	//if (inverse(c_i, inv))
		//display(inv)
    //    for (int i = 0; i < N; i++) {
	//	for (int j = 0; j < N; j++)
	//		cout << inv[i][j] << " ";
	//	cout << endl;
	//};

    // !!!!
    // intact rock compliance S_i
    //double S_i[6][6] = {
    //    {0.0,0.0,0.0,0.0,0.0,0.0},
    //    {0.0,0.0,0.0,0.0,0.0,0.0},
    //    {0.0,0.0,0.0,0.0,0.0,0.0},
    //    {0.0,0.0,0.0,0.0,0.0,0.0},
    //    {0.0,0.0,0.0,0.0,0.0,0.0},
    //    {0.0,0.0,0.0,0.0,0.0,0.0}
    //}; // !!!! figure out inversion!!!
    //for (int i = 0; i < N; i++) {
	//	for (int j = 0; j < N; j++)
	//		S_i[i][j]= inv[i][j];
    //};

    //cout << "\nThe intact Compliance is :\n";
    //for (int i = 0; i < N; i++) {
	//	for (int j = 0; j < N; j++)
	//		cout << S_i[i][j] << " ";
	//	cout << endl;
    //};

   //return 0;


//}

