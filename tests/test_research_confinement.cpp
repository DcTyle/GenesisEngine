#include "GE_research_confinement.hpp"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

int main() {
    const genesis::GeResearchConfinementArchive archive =
        genesis::ge_load_research_confinement_archive();
    const genesis::GeNistSiliconReference nist =
        genesis::ge_load_nist_silicon_reference();

    auto fail = [](const std::string& message) -> int {
        std::cerr << "FAIL: " << message << "\n";
        return 1;
    };

    std::string validation_message;
    if (!genesis::ge_validate_research_confinement_archive(archive, validation_message)) {
        return fail(validation_message);
    }

    if (!archive.loaded) return fail("archive did not report loaded=true");
    if (!nist.loaded) return fail("NIST silicon reference did not load");
    if (!(nist.lattice_constant_m > 5.0e-10 && nist.lattice_constant_m < 5.9e-10)) {
        return fail("NIST silicon lattice constant is outside the expected range");
    }
    if (!(nist.mean_excitation_energy_ev > 100.0 && nist.mean_excitation_energy_ev < 250.0)) {
        return fail("NIST silicon mean excitation energy is outside the expected range");
    }
    if (!(archive.silicon_lattice_constant_m > 5.0e-10 && archive.silicon_lattice_constant_m < 5.9e-10)) {
        return fail("silicon lattice constant is outside the expected range");
    }
    if (archive.nist_surrogate.verdict_utf8 != "not_within_nist_uncertainty_scale") {
        return fail("unexpected NIST surrogate verdict");
    }
    if (archive.nist_surrogate.step_to_linewidth_ratio <= 100.0) {
        return fail("NIST step-to-linewidth ratio is weaker than expected");
    }
    if (archive.run041_d_track.size() < 6u || archive.run041_i_accum.size() < 6u || archive.run041_l_smooth.size() < 6u) {
        return fail("sampled Run41 trajectories are too short");
    }

    std::vector<genesis::GeResearchParticleVizPoint> points;
    genesis::ge_build_research_particle_viz(archive, 24u, 64u, 0.5f, 0.75f, points);
    if (points.empty()) return fail("research replay emitted no points");

    bool saw_photon = false;
    bool saw_weighted = false;
    bool saw_flavor = false;
    bool saw_charged = false;
    for (const genesis::GeResearchParticleVizPoint& point : points) {
        if (!std::isfinite((double)point.x) ||
            !std::isfinite((double)point.y) ||
            !std::isfinite((double)point.z)) {
            return fail("non-finite research point position");
        }
        if (!(point.density >= 0.0f && point.density <= 1.0f)) return fail("point density out of range");
        if (!(point.specularity >= 0.0f && point.specularity <= 1.0f)) return fail("point specularity out of range");
        if (!(point.roughness >= 0.0f && point.roughness <= 1.0f)) return fail("point roughness out of range");
        if (!(point.occlusion >= 0.0f && point.occlusion <= 1.0f)) return fail("point occlusion out of range");
        if (!(point.phase_bias >= -1.0f && point.phase_bias <= 1.0f)) return fail("point phase bias out of range");
        if (!(point.amplitude >= 0.0f && point.amplitude <= 1.0f)) return fail("point amplitude out of range");
        switch (point.particle_class) {
            case genesis::GeResearchParticleClass::Photon:   saw_photon = true; break;
            case genesis::GeResearchParticleClass::Weighted: saw_weighted = true; break;
            case genesis::GeResearchParticleClass::Flavor:   saw_flavor = true; break;
            case genesis::GeResearchParticleClass::Charged:  saw_charged = true; break;
        }
    }

    if (!saw_photon || !saw_weighted || !saw_flavor || !saw_charged) {
        return fail("replay did not emit all particle classes");
    }

    std::cout << "PASS: research confinement archive parsed and replayed\n";
    return 0;
}
