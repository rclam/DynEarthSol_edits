#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>

#include "3x3-C/dsyevh3.h"

#include "constants.hpp"
#include "parameters.hpp"
#include "matprops.hpp"
#include "rheology.hpp"
#include "utils.hpp"
#include <Eigen/Dense>
using Eigen::MatrixXd;
using Eigen::VectorXd;


#include "ic.hpp"
//To calculate elapsed time and write it down to an external file.
#include <iomanip>      // std::setw
#include <chrono>
#include <fstream>
using namespace std::chrono;
//
#include "fields.hpp"



static void principal_stresses3(const double* s, double p[3], double v[3][3])
{
    /* s is a flattened stress vector, with the components {XX, YY, ZZ, XY, XZ, YZ}.
     * Returns the eigenvalues p and eignvectors v.
     * The eigenvalues are ordered such that p[0] <= p[1] <= p[2].
     */

    // unflatten s to a 3x3 tensor, only the upper part is needed.
    double a[3][3];
    a[0][0] = s[0];
    a[1][1] = s[1];
    a[2][2] = s[2];
    a[0][1] = s[3];
    a[0][2] = s[4];
    a[1][2] = s[5];

    dsyevh3(a, v, p);

    // reorder p and v
    if (p[0] > p[1]) {
        double tmp, b[3];
        tmp = p[0];
        p[0] = p[1];
        p[1] = tmp;
        for (int i=0; i<3; ++i)
            b[i] = v[i][0];
        for (int i=0; i<3; ++i)
            v[i][0] = v[i][1];
        for (int i=0; i<3; ++i)
            v[i][1] = b[i];
    }
    if (p[1] > p[2]) {
        double tmp, b[3];
        tmp = p[1];
        p[1] = p[2];
        p[2] = tmp;
        for (int i=0; i<3; ++i)
            b[i] = v[i][1];
        for (int i=0; i<3; ++i)
            v[i][1] = v[i][2];
        for (int i=0; i<3; ++i)
            v[i][2] = b[i];
    }
    if (p[0] > p[1]) {
        double tmp, b[3];
        tmp = p[0];
        p[0] = p[1];
        p[1] = tmp;
        for (int i=0; i<3; ++i)
            b[i] = v[i][0];
        for (int i=0; i<3; ++i)
            v[i][0] = v[i][1];
        for (int i=0; i<3; ++i)
            v[i][1] = b[i];
    }
}


static void principal_stresses2(const double* s, double p[2],
                                double& cos2t, double& sin2t)
{
    /* 's' is a flattened stress vector, with the components {XX, ZZ, XZ}.
     * Returns the eigenvalues 'p', and the direction cosine of the
     * eigenvectors in the X-Z plane.
     * The eigenvalues are ordered such that p[0] <= p[1].
     */

    // center and radius of Mohr circle
    double s0 = 0.5 * (s[0] + s[1]);
    double rad = second_invariant(s);

    // principal stresses in the X-Z plane
    p[0] = s0 - rad;
    p[1] = s0 + rad;

    {
        // direction cosine and sine of 2*theta
        const double eps = 1e-15;
        double a = 0.5 * (s[0] - s[1]);
        double b = - rad; // always negative
        if (b < -eps) {
            cos2t = a / b;
            sin2t = s[2] / b;
        }
        else {
            cos2t = 1;
            sin2t = 0;
        }
    }
}


static void elastic(double bulkm, double shearm, const double* de, double* s)
{
    /* increment the stress s according to the incremental strain de */
    double lambda = bulkm - 2. /3 * shearm;
    double dev = trace(de);

    for (int i=0; i<NDIMS; ++i)
        s[i] += 2 * shearm * de[i] + lambda * dev; //- P_fl !! 
    for (int i=NDIMS; i<NSTR; ++i)
        s[i] += 2 * shearm * de[i];
}

static void emt_elastic(double bulkm, double shearm, const double* a_n, double emt_rho, const double* de, double* s, double pf_z, double* s_iso) 
// TO DO: hydrostatic pore fluid pressure optional in cfg file
{    
    /* increment the stress s according to the incremental strain de */
    double lambda = bulkm - 2. /3 * shearm;
    double dev = trace(de);
    double E0 = shearm * ((3*lambda + 2*shearm)/(lambda + shearm)); // Young's Mod
    double v = lambda / (2*(lambda + shearm));          // poisson ratio

    // intact rock stiffness c_i
    /*MatrixXd c_i(6,6);
        c_i <<
        lambda + 2*shearm, lambda, lambda, 0.0, 0.0, 0.0,
        lambda, lambda + 2*shearm, lambda, 0.0, 0.0, 0.0,
        lambda, lambda, lambda + 2*shearm, 0.0, 0.0, 0.0,
        0.0, 0.0, 0.0, shearm, 0.0, 0.0,
        0.0, 0.0, 0.0, 0.0, shearm, 0.0,
        0.0, 0.0, 0.0, 0.0, 0.0, shearm;*/

    // ============ intact compliance S_i ==========================================
    MatrixXd S_i{
        {1/E0, -v/E0, -v/E0, 0.0, 0.0, 0.0},
        {-v/E0, 1/E0, -v/E0, 0.0, 0.0, 0.0},
        {-v/E0, -v/E0, 1/E0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 1/shearm, 0.0, 0.0},
        {0.0, 0.0, 0.0, 0.0, 1/shearm, 0.0},
        {0.0, 0.0, 0.0, 0.0, 0.0, 1/shearm}
    };

    // ================================================================


    //double th_rad = ((90-theta_normal)+90)*(M_PI/180); // degree in radian, Use cmath PI
    //double a_n[3] = {cos(th_rad), sin(th_rad), 0.0};
    // crack density tensor alpha
    double a_alpha[3][3] = {
        {0.0,0.0,0.0},
        {0.0,0.0,0.0},
        {0.0,0.0,0.0}
        };
    // ============ Solving Alpha Tensor ==========================================
    //auto beg4 = high_resolution_clock::now();
    for (int i=0; i<3; i++){
        for (int j=0; j<3; j++){
            a_alpha[i][j] = emt_rho*a_n[i]*a_n[j]; //tensor product of normal tensor
        }
    }

    //for (int i=0; i<3; i++){
    //    std::cout << std::setprecision (15) << a_n[i] << std::endl; // << " \n";
    //}

    // ================================================================


    // correction term (S_voigt): delta_s_a * delta_s_b
    double delta_s_a = (8.0*(1.0-pow(v,2)))/(3.0*E0*(2.0-v));
    // ============ Solving Correction term partial ==========================================
    //auto beg5 = high_resolution_clock::now();
    MatrixXd delta_s_b{
        {4*a_alpha[0][0], 0.0, 0.0, 0.0, 2*a_alpha[0][2], 2*a_alpha[0][1]}, 
        {0.0, 4*a_alpha[1][1], 0.0, 2*a_alpha[1][2], 0.0, 2*a_alpha[1][0]}, 
        {0.0, 0.0, 4*a_alpha[2][2], 2*a_alpha[2][1], 2*a_alpha[2][0], 0.0}, 
        {0.0, 2*a_alpha[2][1], 2*a_alpha[1][2], a_alpha[1][1] + a_alpha[2][2], a_alpha[1][0], a_alpha[2][0]}, 
        {2*a_alpha[2][0], 0.0, 2*a_alpha[0][2], a_alpha[0][1], a_alpha[0][0] + a_alpha[2][2], a_alpha[2][1]}, 
        {2*a_alpha[1][0], 2*a_alpha[0][1], 0.0, a_alpha[0][2], a_alpha[1][2], a_alpha[0][0]+a_alpha[1][1]}
};

    // ============ Solving S_voigt ==========================================
    //correction term
    MatrixXd S_voigt(6,6);
    S_voigt << delta_s_a * delta_s_b;

    // ============ Solving S_e ==========================================
    MatrixXd S_e(6,6);
    S_e << S_i + S_voigt;

    // New Cracked Stiffness c_e
    // ============ Solving S_e's inverse ==========================================
    MatrixXd c_e(6,6);
    c_e << S_e.inverse();
 

    MatrixXd S_klmm{
        {S_i(0,0)+S_i(0,1)+S_i(0,2), S_i(5,0)+S_i(5,1)+S_i(5,2), S_i(4,0)+S_i(4,1)+S_i(4,2)},
        {S_i(5,0)+S_i(5,1)+S_i(5,2), S_i(1,0)+S_i(1,1)+S_i(1,2), S_i(3,0)+S_i(3,1)+S_i(3,2)},
        {S_i(4,0)+S_i(4,1)+S_i(4,2), S_i(3,0)+S_i(3,1)+S_i(3,2), S_i(2,0)+S_i(2,1)+S_i(2,2)}
    };

    VectorXd S_klmm_V(6);
    S_klmm_V(0) = S_klmm(0,0);
    S_klmm_V(1) = S_klmm(1,1);
    S_klmm_V(2) = S_klmm(2,2);
    S_klmm_V(3) = S_klmm(1,2);
    S_klmm_V(4) = S_klmm(0,2);
    S_klmm_V(5) = S_klmm(0,1);

    
    // ============ CS_V calc ==========================================
    // dot product CS in voigt
    VectorXd CS_V(6);
    CS_V = c_e*S_klmm_V;
    // ================================================================

    // CS voigt to full
    MatrixXd CS{
        {CS_V(0), CS_V(5), CS_V(4)},
        {CS_V(5), CS_V(1), CS_V(3)},
        {CS_V(4), CS_V(3), CS_V(2)}
    };

    // solve for biot tensor
    // B_ij = Kronecker - CS
    MatrixXd Kronecker_delta{
        {1.0,0.0,0.0},
        {0.0,1.0,0.0},
        {0.0,0.0,1.0}
    };

    // ============ Biot calc ==========================================
    MatrixXd Biot(3,3);
    Biot << Kronecker_delta - CS;

    // ================================================================

    // ============ stress_corr[i][j] ==========================================
    // mult. B w (neg) P_fl = stress correction term. Convert to voigt and add to s[i] AFTER last s[i] math
    MatrixXd stress_corr(3,3);
    stress_corr = -pf_z * Biot;
    
    //     [s0 s3 s4]
    //   S=[s3 s1 s5]
    //     [s4 s5 s2]
    //
    double stress_corr_V[NSTR] = {0.0};
    for (int i = 0; i < NDIMS; i++)
        stress_corr_V[i] = stress_corr(i,i);
    if( NDIMS == 2)
        stress_corr_V[2] = stress_corr(0,1);
    else if( NDIMS == 3) {
        stress_corr_V[5] = stress_corr(1,2);
        stress_corr_V[4] = stress_corr(0,2);
        stress_corr_V[3] = stress_corr(0,1);
    }

// add correcting term to current intact stress (in voigt notation) to get effective stress
    // ============ incremental stress update ==========================================
    //auto beg14 = high_resolution_clock::now();
    //double strain_12_iso{}; // USED in simple shear benchmark to check s.strains

    for (int i=0; i<NDIMS; ++i){
        //std::cout << "\n i = " << i <<" diagonal: pre\n" << " s[i]: " << s[i] << " \n";
        //std::cout << " s_iso: " << s_iso[i] << " \n";
        //std::cout << " stress_corr[i]: " << stress_corr_V[i] << " \n";
        s_iso[i] += 2 * shearm * de[i] + lambda * dev;
        s[i] = s_iso[i] + stress_corr_V[i]; //
        //std::cout << " diagonal: post s_iso: " << s_iso[i] << " \n";
        //std::cout << "diagonal: updated normal stress \n"   << s[i] << " \n";
        //std::cout << s[i] << std::endl;// << " \n";
        
    }
    for (int i=NDIMS; i<NSTR; ++i){
        //std::cout << "\n\noff diagonal: pre\n"   << s[i] << " \n";
        s_iso[i] += 2 * shearm * de[i];
        //strain_12_iso = 0.5 * s_iso[i] / shearm;
        s[i] = s_iso[i] + stress_corr_V[i]; //isotropic, linear elastic stress update. Correction term should be added to this
        //std::cout << "off diagonal: updated shear stress\n" << s[i] << " \n";
        //std::cout << s[i] << std::endl;;// << " \n";
        //std::cout << "strain[0][1]\n" << strain_12_iso << " \n";
        //std::cout << strain_12_iso << " \n";
        //std::cout << "\n=============== END SECTION\n";

    }




    
}


static void maxwell(double bulkm, double shearm, double viscosity, double dt,
                    double dv, const double* de, double* s)
{
    // non-dimensional parameter: dt/ relaxation time
    double tmp = 0.5 * dt * shearm / viscosity;
    double f1 = 1 - tmp;
    double f2 = 1 / (1  + tmp);

    double dev = trace(de) / NDIMS;
    double s0 = trace(s) / NDIMS;

    // convert back to total stress
    for (int i=0; i<NDIMS; ++i)
        s[i] = ((s[i] - s0) * f1 + 2 * shearm * (de[i] - dev)) * f2 + s0 + bulkm * dv;
    for (int i=NDIMS; i<NSTR; ++i)
        s[i] = (s[i] * f1 + 2 * shearm * de[i]) * f2;
}


static void viscous(double bulkm, double viscosity, double total_dv,
                    const double* edot, double* s)
{
    /* Viscous Model + incompressibility enforced by bulk modulus */

    double dev = trace(edot) / NDIMS;

    for (int i=0; i<NDIMS; ++i)
        s[i] = 2 * viscosity * (edot[i] - dev) + bulkm * total_dv;
    for (int i=NDIMS; i<NSTR; ++i)
        s[i] = 2 * viscosity * edot[i];
}


static void elasto_plastic(double bulkm, double shearm,
                           double amc, double anphi, double anpsi,
                           double hardn, double ten_max,
                           const double* de, double& depls, double* s,
                           int &failure_mode)
{
    /* Elasto-plasticity (Mohr-Coulomb criterion)
     *
     * failure_mode --
     *   0: no failure
     *   1: tensile failure
     *  10: shear failure
     */

    // elastic trial stress
    elastic(bulkm, shearm, de, s);
    depls = 0;
    failure_mode = 0;

    //
    // transform to principal stress coordinate system
    //
    // eigenvalues (principal stresses)
    double p[NDIMS];
#ifdef THREED
    // eigenvectors
    double v[3][3];
    principal_stresses3(s, p, v);
#else
    // In 2D, we only construct the eigenvectors from
    // cos(2*theta) and sin(2*theta) of Mohr circle
    double cos2t, sin2t;
    principal_stresses2(s, p, cos2t, sin2t);
#endif

    // composite (shear and tensile) yield criterion
    double fs = p[0] - p[NDIMS-1] * anphi + amc;
    double ft = p[NDIMS-1] - ten_max;

    if (fs > 0 && ft < 0) {
        // no failure
        return;
    }



    // yield, shear or tensile?
    double pa = std::sqrt(1 + anphi*anphi) + anphi;
    double ps = ten_max * anphi - amc;
    double h = p[NDIMS-1] - ten_max + pa * (p[0] - ps);
    double a1 = bulkm + 4. / 3 * shearm;
    double a2 = bulkm - 2. / 3 * shearm;

    double alam;
    if (h < 0) {
        // shear failure
        failure_mode = 10;

        alam = fs / (a1 - a2*anpsi + a1*anphi*anpsi - a2*anphi + 2*std::sqrt(anphi)*hardn);
        p[0] -= alam * (a1 - a2 * anpsi);
#ifdef THREED
        p[1] -= alam * (a2 - a2 * anpsi);
#endif
        p[NDIMS-1] -= alam * (a2 - a1 * anpsi);

        // 2nd invariant of plastic strain
#ifdef THREED
        /* // plastic strain in the principal directions, depls2 is always 0
         * double depls1 = alam;
         * double depls3 = -alam * anpsi;
         * double deplsm = (depls1 + depls3) / 3;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (-deplsm)*(-deplsm) +
         *                    (depls3-deplsm)*(depls3-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        // the equations above can be reduce to:
        depls = std::fabs(alam) * std::sqrt((7 + 4*anpsi + 7*anpsi*anpsi) / 18);
#else
        /* // plastic strain in the principal directions
         * double depls1 = alam;
         * double depls2 = -alam * anpsi;
         * double deplsm = (depls1 + depls2) / 2;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (depls2-deplsm)*(depls2-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        // the equations above can be reduce to:
        depls = std::fabs(alam) * std::sqrt((3 + 2*anpsi + 3*anpsi*anpsi) / 8);
#endif
    }
    else {
        // tensile failure
        failure_mode = 1;

        alam = ft / a1;
        p[0] -= alam * a2;
#ifdef THREED
        p[1] -= alam * a2;
#endif
        p[NDIMS-1] -= alam * a1;

        // 2nd invariant of plastic strain
#ifdef THREED
        /* double depls1 = 0;
         * double depls3 = alam;
         * double deplsm = (depls1 + depls3) / 3;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (-deplsm)*(-deplsm) +
         *                    (depls3-deplsm)*(depls3-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        depls = std::fabs(alam) * std::sqrt(7. / 18);
#else
        /* double depls1 = 0;
         * double depls3 = alam;
         * double deplsm = (depls1 + depls3) / 2;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (depls2-deplsm)*(depls2-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        depls = std::fabs(alam) * std::sqrt(3. / 8);
#endif
    }

    // rotate the principal stresses back to global axes
    {
#ifdef THREED
        double ss[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
        for(int m=0; m<3; m++) {
            for(int n=m; n<3; n++) {
                for(int k=0; k<3; k++) {
                    ss[m][n] += v[m][k] * v[n][k] * p[k];
                }
            }
        }
        s[0] = ss[0][0];
        s[1] = ss[1][1];
        s[2] = ss[2][2];
        s[3] = ss[0][1];
        s[4] = ss[0][2];
        s[5] = ss[1][2];
#else
        double dc2 = (p[0] - p[1]) * cos2t;
        double dss = p[0] + p[1];
        s[0] = 0.5 * (dss + dc2);
        s[1] = 0.5 * (dss - dc2);
        s[2] = 0.5 * (p[0] - p[1]) * sin2t;
#endif
    }
}

static void emt(double bulkm, double shearm, const double* a_n, double emt_rho,
                           double amc, double anphi, double anpsi,
                           double hardn, double ten_max,
                           const double* de, double& depls, double* s,
                           int &failure_mode, double pf_z, double* s_iso)
{
    /* Elasto-plasticity (Mohr-Coulomb criterion)
     *
     * failure_mode --
     *   0: no failure
     *   1: tensile failure
     *  10: shear failure
     */

    // elastic trial stress
    emt_elastic(bulkm, shearm, a_n, emt_rho, de, s, pf_z, s_iso); // rename to emt_elastic *** include double P_fl=50e+06
    depls = 0;
    failure_mode = 0;

    //
    // transform to principal stress coordinate system
    //
    // eigenvalues (principal stresses)
    double p[NDIMS];
#ifdef THREED
    // eigenvectors
    double v[3][3];
    principal_stresses3(s, p, v);
#else
    // In 2D, we only construct the eigenvectors from
    // cos(2*theta) and sin(2*theta) of Mohr circle
    double cos2t, sin2t;
    principal_stresses2(s, p, cos2t, sin2t);
#endif

    // composite (shear and tensile) yield criterion
    double fs = p[0] - p[NDIMS-1] * anphi + amc;
    double ft = p[NDIMS-1] - ten_max;

    if (fs > 0 && ft < 0) {
        // no failure
        return;
    }



    // yield, shear or tensile?
    double pa = std::sqrt(1 + anphi*anphi) + anphi;
    double ps = ten_max * anphi - amc;
    double h = p[NDIMS-1] - ten_max + pa * (p[0] - ps);
    double a1 = bulkm + 4. / 3 * shearm;
    double a2 = bulkm - 2. / 3 * shearm;

    double alam;
    if (h < 0) {
        // shear failure
        failure_mode = 10;

        alam = fs / (a1 - a2*anpsi + a1*anphi*anpsi - a2*anphi + 2*std::sqrt(anphi)*hardn);
        p[0] -= alam * (a1 - a2 * anpsi);
#ifdef THREED
        p[1] -= alam * (a2 - a2 * anpsi);
#endif
        p[NDIMS-1] -= alam * (a2 - a1 * anpsi);

        // 2nd invariant of plastic strain
#ifdef THREED
        /* // plastic strain in the principal directions, depls2 is always 0
         * double depls1 = alam;
         * double depls3 = -alam * anpsi;
         * double deplsm = (depls1 + depls3) / 3;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (-deplsm)*(-deplsm) +
         *                    (depls3-deplsm)*(depls3-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        // the equations above can be reduce to:
        depls = std::fabs(alam) * std::sqrt((7 + 4*anpsi + 7*anpsi*anpsi) / 18);
#else
        /* // plastic strain in the principal directions
         * double depls1 = alam;
         * double depls2 = -alam * anpsi;
         * double deplsm = (depls1 + depls2) / 2;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (depls2-deplsm)*(depls2-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        // the equations above can be reduce to:
        depls = std::fabs(alam) * std::sqrt((3 + 2*anpsi + 3*anpsi*anpsi) / 8);
#endif
    }
    else {
        // tensile failure
        failure_mode = 1;

        alam = ft / a1;
        p[0] -= alam * a2;
#ifdef THREED
        p[1] -= alam * a2;
#endif
        p[NDIMS-1] -= alam * a1;

        // 2nd invariant of plastic strain
#ifdef THREED
        /* double depls1 = 0;
         * double depls3 = alam;
         * double deplsm = (depls1 + depls3) / 3;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (-deplsm)*(-deplsm) +
         *                    (depls3-deplsm)*(depls3-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        depls = std::fabs(alam) * std::sqrt(7. / 18);
#else
        /* double depls1 = 0;
         * double depls3 = alam;
         * double deplsm = (depls1 + depls3) / 2;
         * depls = std::sqrt(((depls1-deplsm)*(depls1-deplsm) +
         *                    (depls2-deplsm)*(depls2-deplsm) +
         *                    deplsm*deplsm) / 2);
         */
        depls = std::fabs(alam) * std::sqrt(3. / 8);
#endif
    }

    // rotate the principal stresses back to global axes
    {
#ifdef THREED
        double ss[3][3] = {{0,0,0},{0,0,0},{0,0,0}};
        for(int m=0; m<3; m++) {
            for(int n=m; n<3; n++) {
                for(int k=0; k<3; k++) {
                    ss[m][n] += v[m][k] * v[n][k] * p[k];
                }
            }
        }
        s[0] = ss[0][0];
        s[1] = ss[1][1];
        s[2] = ss[2][2];
        s[3] = ss[0][1];
        s[4] = ss[0][2];
        s[5] = ss[1][2];
#else
        double dc2 = (p[0] - p[1]) * cos2t;
        double dss = p[0] + p[1];
        s[0] = 0.5 * (dss + dc2);
        s[1] = 0.5 * (dss - dc2);
        s[2] = 0.5 * (p[0] - p[1]) * sin2t;
#endif
    }
}

static void elasto_plastic2d(double bulkm, double shearm,
                             double amc, double anphi, double anpsi,
                             double hardn, double ten_max,
                             const double* de, double& depls,
                             double* s, double &syy,
                             int &failure_mode)
{
    /* Elasto-plasticity (Mohr-Coulomb criterion) */

    /* This function is derived from geoFLAC.
     * The original code in geoFLAC assumes 2D plane strain formulation,
     * i.e. there are 3 principal stresses (PSs) and only 2 principal strains
     * (Strain_yy, Strain_xy, and Strain_yz all must be 0).
     * Here, the code is adopted to pure 2D or 3D plane strain.
     *
     * failure_mode --
     *   0: no failure
     *   1: tensile failure, all PSs exceed tensile limit
     *   2: tensile failure, 2 PSs exceed tensile limit
     *   3: tensile failure, 1 PS exceeds tensile limit
     *  10: pure shear failure
     *  11, 12, 13: tensile + shear failure
     *  20, 21, 22, 23: shear + tensile failure
     */

    depls = 0;
    failure_mode = 0;

    // elastic trial stress
    double a1 = bulkm + 4. / 3 * shearm;
    double a2 = bulkm - 2. / 3 * shearm;
    double sxx = s[0] + de[1]*a2 + de[0]*a1;
    double szz = s[1] + de[0]*a2 + de[1]*a1;
    double sxz = s[2] + de[2]*2*shearm;
    syy += (de[0] + de[1]) * a2; // Stress YY component, plane strain


    //
    // transform to principal stress coordinate system
    //
    // eigenvalues (principal stresses)
    double p[3];

    // In 2D, we only construct the eigenvectors from
    // cos(2*theta) and sin(2*theta) of Mohr circle
    double cos2t, sin2t;
    int n1, n2, n3;

    {
        // center and radius of Mohr circle
        double s0 = 0.5 * (sxx + szz);
        double rad = 0.5 * std::sqrt((sxx-szz)*(sxx-szz) + 4*sxz*sxz);

        // principal stresses in the X-Z plane
        double si = s0 - rad;
        double sii = s0 + rad;

        // direction cosine and sine of 2*theta
        const double eps = 1e-15;
        if (rad > eps) {
            cos2t = 0.5 * (szz - sxx) / rad;
            sin2t = -sxz / rad;
        }
        else {
            cos2t = 1;
            sin2t = 0;
        }

        // sort p.s.
#if 1
        //
        // 3d plane strain
        //
        if (syy > sii) {
            // syy is minor p.s.
            n1 = 0;
            n2 = 1;
            n3 = 2;
            p[0] = si;
            p[1] = sii;
            p[2] = syy;
        }
        else if (syy < si) {
            // syy is major p.s.
            n1 = 1;
            n2 = 2;
            n3 = 0;
            p[0] = syy;
            p[1] = si;
            p[2] = sii;
        }
        else {
            // syy is intermediate
            n1 = 0;
            n2 = 2;
            n3 = 1;
            p[0] = si;
            p[1] = syy;
            p[2] = sii;
        }
#else
        /* XXX: This case gives unreasonable result. Don't know why... */

        //
        // pure 2d case
        //
        n1 = 0;
        n2 = 2;
        p[0] = si;
        p[1] = syy;
        p[2] = sii;
#endif
    }

    // Possible tensile failure scenarios
    // 1. S1 (least tensional or greatest compressional principal stress) > ten_max:
    //    all three principal stresses must be greater than ten_max.
    //    Assign ten_max to all three and don't do anything further.
    if( p[0] >= ten_max ) {
        s[0] = s[1] = syy = ten_max;
        s[2] = 0.0;
        failure_mode = 1;
        return;
    }

    // 2. S2 (intermediate principal stress) > ten_max:
    //    S2 and S3 must be greater than ten_max.
    //    Assign ten_max to these two and continue to the shear failure block.
    if( p[1] >= ten_max ) {
        p[1] = p[2] = ten_max;
        failure_mode = 2;
    }

    // 3. S3 (most tensional or least compressional principal stress) > ten_max:
    //    Only this must be greater than ten_max.
    //    Assign ten_max to S3 and continue to the shear failure block.
    else if( p[2] >= ten_max ) {
        p[2] = ten_max;
        failure_mode = 3;
    }


    // shear yield criterion
    double fs = p[0] - p[2] * anphi + amc;
    if (fs >= 0.0) {
        // Tensile failure case S2 or S3 could have happened!!
        // XXX: Need to rationalize why exit without doing anything further.
        s[0] = sxx;
        s[1] = szz;
        s[2] = sxz;
        return;
    }

    failure_mode += 10;

    // shear failure
    const double alams = fs / (a1 - a2*anpsi + a1*anphi*anpsi - a2*anphi + hardn);
    p[0] -= alams * (a1 - a2 * anpsi);
    p[1] -= alams * (a2 - a2 * anpsi);
    p[2] -= alams * (a2 - a1 * anpsi);

    // 2nd invariant of plastic strain
    depls = 0.5 * std::fabs(alams + alams * anpsi);

    //***********************************
    // The following seems redundant but... this is how it goes in geoFLAC.
    //
    // Possible tensile failure scenarios
    // 1. S1 (least tensional or greatest compressional principal stress) > ten_max:
    //    all three principal stresses must be greater than ten_max.
    //    Assign ten_max to all three and don't do anything further.
    if( p[0] >= ten_max ) {
        s[0] = s[1] = syy = ten_max;
        s[2] = 0.0;
        failure_mode += 20;
        return;
    }

    // 2. S2 (intermediate principal stress) > ten_max:
    //    S2 and S3 must be greater than ten_max.
    //    Assign ten_max to these two and continue to the shear failure block.
    if( p[1] >= ten_max ) {
        p[1] = p[2] = ten_max;
        failure_mode += 20;
    }

    // 3. S3 (most tensional or least compressional principal stress) > ten_max:
    //    Only this must be greater than ten_max.
    //    Assign ten_max to S3 and continue to the shear failure block.
    else if( p[2] >= ten_max ) {
        p[2] = ten_max;
        failure_mode += 20;
    }
    //***********************************


    // rotate the principal stresses back to global axes
    {
        double dc2 = (p[n1] - p[n2]) * cos2t;
        double dss = p[n1] + p[n2];
        s[0] = 0.5 * (dss + dc2);
        s[1] = 0.5 * (dss - dc2);
        s[2] = 0.5 * (p[n1] - p[n2]) * sin2t;
        syy = p[n3];
    }
}


void update_stress(const Variables& var, tensor_t& stress,
                   double_vec& stressyy, double_vec& dpressure,
                   tensor_t& strain, double_vec& plstrain,
                   double_vec& delta_plstrain, tensor_t& strain_rate, tensor_t& emt_normal_array, tensor_t& emt_iso_stress)
{
    const int rheol_type = var.mat->rheol_type;

    #pragma omp parallel for default(none)                           \
        shared(var, stress, stressyy, dpressure, strain, plstrain, delta_plstrain, \
               strain_rate, rheol_type, emt_normal_array, emt_iso_stress, std::cerr)
    for (int e=0; e<var.nelem; ++e) {
        // stress, strain and strain_rate of this element
        double* s = stress[e];
        double* s_iso = emt_iso_stress[e];  // new
        double& syy = stressyy[e];
        double* es = strain[e];
        double* edot = strain_rate[e];
        
	double old_s = trace(s);

        // anti-mesh locking correction on strain rate
        if(1){
            double div = trace(edot);
            //double div2 = ((*var.volume)[e] / (*var.volume_old)[e] - 1) / var.dt;
            for (int i=0; i<NDIMS; ++i) {
                edot[i] += ((*var.edvoldt)[e] - div) / NDIMS;  // XXX: should NDIMS -> 3 in plane strain?
            }
        }

        // update strain with strain rate
        for (int i=0; i<NSTR; ++i) {
            //std::cerr << "\n element="<<e<<" i="<<i<<" es[i]: " << es[i] << "\n";
            es[i] += edot[i] * var.dt;
            /*std::cerr << "es[i]: " << es[i] <<" de="<< edot[i]*var.dt<< "\n";
            std::cerr << " edot[i]: " << edot[i] << "\n";*/
            /*std::cerr << " dt[i]: " << var.dt << "\n";
            std::cerr << " a_n[i]: " << emt_normal_array[e][i] << "\n";
            std::cerr << "\n";*/

        }

        // modified strain increment
        double de[NSTR];
        for (int i=0; i<NSTR; ++i) {
            de[i] = edot[i] * var.dt;
        }

        switch (rheol_type) {
        case MatProps::rh_elastic:
            {
                double bulkm = var.mat->bulkm(e);
                double shearm = var.mat->shearm(e);
                elastic(bulkm, shearm, de, s);
            }
            break;
        case MatProps::rh_viscous:
            {
                double bulkm = var.mat->bulkm(e);
                double viscosity = var.mat->visc(e);
                double total_dv = trace(es);
                viscous(bulkm, viscosity, total_dv, edot, s);
            }
            break;
        case MatProps::rh_maxwell:
            {
                double bulkm = var.mat->bulkm(e);
                double shearm = var.mat->shearm(e);
                double viscosity = var.mat->visc(e);
                double dv = (*var.volume)[e] / (*var.volume_old)[e] - 1;
                maxwell(bulkm, shearm, viscosity, var.dt, dv, de, s);
            }
            break;
        case MatProps::rh_ep:
            {
                double depls = 0;
                double bulkm = var.mat->bulkm(e);
                double shearm = var.mat->shearm(e);
                double amc, anphi, anpsi, hardn, ten_max;
                var.mat->plastic_props(e, plstrain[e],
                                       amc, anphi, anpsi, hardn, ten_max);
                int failure_mode;
                if (var.mat->is_plane_strain) {
                    elasto_plastic2d(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                     de, depls, s, syy, failure_mode);
                }
                else {
                    elasto_plastic(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, s, failure_mode);
                }
                plstrain[e] += depls;
                delta_plstrain[e] = depls;
            }
            break;
        case MatProps::rh_emt:
            {
                double* a_n = emt_normal_array[e];  //new!!
                double depls = 0;
                double bulkm = var.mat->bulkm(e);
                double shearm = var.mat->shearm(e);
                double emt_rho = var.mat->emt_rho(e);
                double amc, anphi, anpsi, hardn, ten_max;
                var.mat->plastic_props(e, plstrain[e],
                                       amc, anphi, anpsi, hardn, ten_max);
                int failure_mode;
                double pf_z = pore_fluid_pressure(var, e);
                
                /*if (var.mat->emt_rho(e)==0.0 && var.mat->is_plane_strain) {
                    // use ep when no imposed cracks
                    elasto_plastic2d(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                     de, depls, s, syy, failure_mode);
                }
                else if (var.mat->emt_rho(e)==0.0)  {
                    elasto_plastic(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, s, failure_mode);
                }
                else if (var.mat->emt_rho(e)!=0.0 && var.mat->is_plane_strain) {
                    // use emt when cracks imposed
                    emt(bulkm, shearm, a_n, emt_rho, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, s, failure_mode, pf_z, s_iso);
                }
                else if (var.mat->emt_rho(e)!=0.0 && var.mat->is_plane_strain) {
                    // use emt when cracks imposed
                    emt(bulkm, shearm, a_n, emt_rho, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, s, failure_mode, pf_z, s_iso);
                }
                else {
                    emt(bulkm, shearm, a_n, emt_rho, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, s, failure_mode, pf_z, s_iso);
                }*/
                if (var.mat->is_plane_strain) {
                    // use emt when cracks imposed
                    emt(bulkm, shearm, a_n, emt_rho, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, s, failure_mode, pf_z, s_iso);
                }
                else {
                    emt(bulkm, shearm, a_n, emt_rho, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, s, failure_mode, pf_z, s_iso);
                }
                plstrain[e] += depls;
                delta_plstrain[e] = depls;
            }
            break;
        case MatProps::rh_evp:
            {
                double depls = 0;
                double bulkm = var.mat->bulkm(e);
                double shearm = var.mat->shearm(e);
                double viscosity = var.mat->visc(e);
                double dv = (*var.volume)[e] / (*var.volume_old)[e] - 1;
                // stress due to maxwell rheology
                double sv[NSTR];
                for (int i=0; i<NSTR; ++i) sv[i] = s[i];
                maxwell(bulkm, shearm, viscosity, var.dt, dv, de, sv);
                double svII = second_invariant2(sv);

                double amc, anphi, anpsi, hardn, ten_max;
                var.mat->plastic_props(e, plstrain[e],
                                       amc, anphi, anpsi, hardn, ten_max);
                // stress due to elasto-plastic rheology
                double sp[NSTR], spyy;
                for (int i=0; i<NSTR; ++i) sp[i] = s[i];
                int failure_mode;
                if (var.mat->is_plane_strain) {
                    spyy = syy;
                    elasto_plastic2d(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                     de, depls, sp, spyy, failure_mode);
                }
                else {
                    elasto_plastic(bulkm, shearm, amc, anphi, anpsi, hardn, ten_max,
                                   de, depls, sp, failure_mode);
                }
                double spII = second_invariant2(sp);

                // use the smaller as the final stress
                if (svII < spII)
                    for (int i=0; i<NSTR; ++i) s[i] = sv[i];
                else {
                    for (int i=0; i<NSTR; ++i) s[i] = sp[i];
                    plstrain[e] += depls;
                    delta_plstrain[e] = depls;
                    syy = spyy;
                }
            }
            break;
        default:
            std::cerr << "Error: unknown rheology type: " << rheol_type << "\n";
            std::exit(1);
            break;
        }
	dpressure[e] = trace(s) - old_s;
        //std::cerr << "stress " << e << ": ";
        //print(std::cerr, s, NSTR);
        //std::cerr << '\n';
    }
}
