#ifndef DYNEARTHSOL3D_OUTPUT_HPP
#define DYNEARTHSOL3D_OUTPUT_HPP

#include "array2d.hpp"

class Output
{
private:
    const std::string &modelname;
    const double start_time;
    const int average_interval;
    const bool has_marker_output;
    int frame;
    const bool is_averaged;

    // stuffs for averging fields
    double time0;
    array_t coord0;
    tensor_t strain0;
    tensor_t stress_avg;
    tensor_t emt_iso_stress_avg;
    double_vec delta_plstrain_avg;

    void write_info(const Variables& var, double dt);
    void _write(const Variables& var, bool disable_averaging=false);

public:
    Output(const Param& param, double start_time, int start_frame);
    ~Output();
    void write(const Variables& var);
    void write_exact(const Variables& var);
    void write_checkpoint(const Param& param, const Variables& var);
    void average_fields(Variables& var);

};


#endif
