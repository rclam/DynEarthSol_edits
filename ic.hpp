#ifndef DYNEARTHSOL3D_IC_HPP
#define DYNEARTHSOL3D_IC_HPP

double get_prem_pressure(double depth);
double get_prem_pressure_modified(double depth);
void initial_stress_state(const Param &param, const Variables &var,
                          tensor_t &stress, double_vec &stressyy, tensor_t &strain,
                          double &compensation_pressure);
void initial_emt_iso_stress(const Param &param, const Variables &var,
                          tensor_t &emt_iso_stress, double_vec &stressyy, tensor_t &strain,
                          double &compensation_pressure);
void initial_emt_normal_array(const Param &param, const Variables &var,
                          tensor_t &emt_normal_array);
double pore_fluid_pressure(const Variables &var, const int e);
void initial_weak_zone(const Param &param, const Variables &var,
                       double_vec &plstrain);
void initial_temperature(const Param &param, const Variables &var,
                         double_vec &temperature);


#endif
