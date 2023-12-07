#ifndef DYNEARTHSOL3D_RHEOLOGY_HPP
#define DYNEARTHSOL3D_RHEOLOGY_HPP

void update_stress(const Variables& var, tensor_t& stress, tensor_t& emt_iso_stress, double_vec& stressyy,
                   double_vec& dpressure, tensor_t& strain, double_vec& plstrain,
                   double_vec& delta_plstrain, tensor_t& strain_rate, tensor_t& emt_normal_array);

#endif
