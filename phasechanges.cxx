#include "constants.hpp"
#include "parameters.hpp"
#include "ic.hpp"
#include "markerset.hpp"
#include "utils.hpp"

#include "phasechanges.hpp"

namespace {

    class PhaseChange
    {
    protected:
        const Param& param;
        const Variables& var;
        const MarkerSet& ms;
    public:
        PhaseChange(const Param& param_, const Variables& var_, const MarkerSet& ms_) :
            param(param_), var(var_), ms(ms_)
        {}
        virtual ~PhaseChange() {};
        virtual int operator()(int mattype) = 0;
        void get_ZPT(int m, double &Z, double &P, double &T)
        {
            int e = ms.get_elem(m);

            // Get depth and temperature at the marker
            Z = T = 0;
            const double* eta = ms.get_eta(m);
            const int *conn = (*var.connectivity)[e];
            for (int i=0; i<NODES_PER_ELEM; ++i) {
                Z += (*var.coord)[conn[i]][NDIMS-1] * eta[i];
                T += (*var.temperature)[conn[i]] * eta[i];
            }

            // Get pressure, which is constant in the element
            // P = - trace((*var.stress)[e]) / NDIMS;

            // Get pressure at this depth from PREM
            P = get_prem_pressure(-Z);
        }
    };


    class SimpleSubduction : public PhaseChange
    {
        MarkerSet &hydms;
        Array2D<int,1> &hydem;

        // mattype --> lithology
        enum {
            mt_mantle = 0,
            mt_serpentinized_mantle = 1,
            mt_oceanic_crust = 2,
            mt_eclogite = 3,
            mt_sediment = 4,
            mt_schist = 5,
            mt_upper_continental_crust = 6,
            mt_lower_continental_crust = 7
        };

    public:
        SimpleSubduction(const Param& param, const Variables& var, const MarkerSet& ms,
                         MarkerSet& hydms_, Array2D<int,1>& hydem_) :
            PhaseChange(param, var, ms), hydms(hydms_), hydem(hydem_)
        {}

        int operator()(int m)
        {
            double Z, P, T;
            get_ZPT(m, Z, P, T);

            int current_mt = ms.get_mattype(m);

            // Set new mattype the same as current mattype for now
            int new_mt = current_mt;
            int hyd_inc = 0;

            switch (current_mt) {
            case mt_oceanic_crust:
                {
                    // basalt -> eclogite
                    // Phase diagram from Hacker, 1996, Subduction: Top to Bottom
                    const double min_eclogite_T = 500 + 273;
                    const double transition_pressure = -0.3e9 + 2.2e6*T;
                    if (T > min_eclogite_T && P > transition_pressure) {
                        new_mt = mt_eclogite;
                        hyd_inc = 1;
                    }
                }
                break;
            case mt_sediment:
                {
                    // sediment -> schist/gneiss
                    // from sediment solidus in Nichols et al, 1994, Nature
                    const double min_schist_T = 650 + 273;
                    const double min_schist_Z = -20e3;
                    if (T > min_schist_T && Z < min_schist_Z) {
                        new_mt = mt_schist;
                        hyd_inc = 1;
                    }
                }
                break;
            case mt_serpentinized_mantle:
                {
                    // serpentinite -> normal mantle
                    // Phase diagram taken from Ulmer and Trommsdorff, 1995, Nature
                    // Fixed points (730 C, 2.1 GPa) (500 C, 7.5 GPa)
                    const double transition_pressure = 2.1e9 + (7.5e9 - 2.1e9) * (T - (730+273)) / (500 - 730);
                    const double min_serpentine_T = 550 + 273;
                    if (T > min_serpentine_T && P > transition_pressure) {
                        new_mt = mt_mantle;
                        hyd_inc = 1;
                    }
                }
                break;
            case mt_mantle:
                {
                    const double min_serpentine_T = 550 + 273;
                    const int el = ms.get_elem(m);
                    if (T <= min_serpentine_T && hydem[el][0]) {
                        new_mt = mt_serpentinized_mantle;
                        //hyd_inc = -1;
                    }
                }
                break;
            }

            if (hyd_inc == 1) {
                // Dehydration metamorphism, hydrous marker is released.
                const int el = ms.get_elem(m);
                const double *eta = ms.get_eta(m);
                #pragma omp critical(phase_change_simple_subduction)
                {
                    // Add new marker, which has the same coordinate as the dehydrated marker
                    hydms.append_marker(eta, el, 0);
                    ++hydem[el][0];
                }
            }

            /*** Disable hyd marker deletion
            else if (hyd_inc == -1) {
                const int el = ms.get_elem(m);
                // Find the hydrous marker belong to el
                int mh;
                for (mh=0; mh<hydms.get_nmarkers(); mh++) {
                    if (hydms.get_elem(mh) == el) break;
                }
                if (mh >= hydms.get_nmarkers()) {
                    std::cerr << "Error: hydrous marker phase change\n";
                    std::exit(12);
                }
                #pragma omp critical(phase_change_simple_subduction)
                {
                    // delete the marker
                    hydms.remove_marker(mh);
                    --hydem[el][0];
                }
            }
            */

            return new_mt;
        }
    };


    // A template to phase change function
    class Custom_PhCh : public PhaseChange
    {
    public:
        Custom_PhCh(const Param& param, const Variables& var, const MarkerSet& ms) :
            PhaseChange(param, var, ms)
        {}

        int operator()(int m)
        {
            double Z, P, T;
            get_ZPT(m, Z, P, T);

            int current_mt = ms.get_mattype(m);
            // Set new mattype the same as current mattype for now
            int new_mt = current_mt;

            switch (current_mt) {
            case 0:
                break;
            case 1:
                break;
        }

        return new_mt;
        }
    };


} // anonymous namespace


void phase_changes(const Param& param, Variables& var)
{
    if (param.mat.nmat == 1 || param.mat.phase_change_option == 0) return;

    MarkerSet& ms = *(var.markersets[0]);
    int_vec2D& elemmarkers = *var.elemmarkers;

    PhaseChange *phch = NULL;
    switch (param.mat.phase_change_option) {
    case 1:
        phch = new SimpleSubduction(param, var, ms,
                                    *var.markersets[var.hydrous_marker_index],
                                    *var.hydrous_elemmarkers);
        break;
    case 101:
        phch = new Custom_PhCh(param, var, ms);
        break;
    default:
        std::cerr << "Error: unknown phase_change_option: " << param.mat.phase_change_option << '\n';
        std::exit(1);
    }

    #pragma omp parallel for default(none)          \
        shared(ms, elemmarkers, phch)
    for (int m=0; m<ms.get_nmarkers(); m++) {
        int current_mt = ms.get_mattype(m);
        int new_mt = (*phch)(m);

        if (new_mt != current_mt) {
            ms.set_mattype(m, new_mt);

            // update marker count
            int e = ms.get_elem(m);
            #pragma omp atomic  // prevent concurrent modification on elemmarkers
            --elemmarkers[e][current_mt];
            #pragma omp atomic  // prevent concurrent modification on elemmarkers
            ++elemmarkers[e][new_mt];
        }

    }
}
