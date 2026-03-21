# Path: core/stochastic_eq.py
# Description:
#   Stochastic noise, dispersion, and perturbation models under DMT environment.
#   - Provides deterministic pseudo-random sequences via VSD-seeded quantum_rng.
#   - Generates correlated noise, Gaussian and Lorentzian dispersion fields, and
#     temporal jitter consistent with environment parameters.
#   - Feeds into higher-level modules for adaptive propagation and hashing validation.
#
#   Exports:
#     gaussian_noise(...)
#     lorentzian_noise(...)
#     correlated_noise_series(...)
#     temporal_jitter_profile(...)
#     diagnostic_stochastic_spectrum(...)
#
#   No arbitrary constants; parameters derived from environment and VSD policy data.
#
#   ASCII-only; numerically deterministic within the virtual environment.

from typing import List, Dict
from core import utils
from core import constants_eq
import math

def gaussian_noise(samples:int,
                   sigma_scale:float=1.0,
                   temperature_k:float=None,
                   seed:int=None)->List[float]:
    """
    Generates Gaussian noise with sigma scaled by stochastic_dispersion_factor.
    """
    T=float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    eff=constants_eq.get_effective(T,utils.env_velocity_fraction_c(),
                                   utils.env_flux_factor(),utils.env_strain_factor())
    sigma=sigma_scale*eff["stochastic_factor"]
    rng=utils.quantum_rng(utils.env_rng_seed() if seed is None else seed)
    out=[]
    for _ in range(max(1,int(samples))):
        # Box-Muller transform, ASCII-safe math
        u1=max(1e-9,rng.random())
        u2=rng.random()
        z=( -2.0*math.log(u1) )**0.5*math.cos(2.0*3.1415926535*u2)
        out.append(sigma*z)
    return out

def lorentzian_noise(samples:int,
                     gamma_scale:float=1.0,
                     temperature_k:float=None,
                     seed:int=None)->List[float]:
    """
    Generates Lorentzian (Cauchy) noise using environment dispersion factor.
    """
    T=float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    eff=constants_eq.get_effective(T,utils.env_velocity_fraction_c(),
                                   utils.env_flux_factor(),utils.env_strain_factor())
    gamma=gamma_scale*eff["stochastic_factor"]
    rng=utils.quantum_rng(utils.env_rng_seed() if seed is None else seed)
    out=[]
    for _ in range(max(1,int(samples))):
        u=rng.random()-0.5
        out.append(gamma*math.tan(3.1415926535*u))
    return out

def correlated_noise_series(samples:int,
                            correlation:float=0.5,
                            temperature_k:float=None,
                            seed:int=None)->List[float]:
    """
    Generates correlated Gaussian noise using exponential kernel.
    """
    base=gaussian_noise(samples,1.0,temperature_k,seed)
    out=[base[0]]
    a=max(0.0,min(0.9999,correlation))
    for i in range(1,len(base)):
        out.append(a*out[i-1]+(1.0-a)*base[i])
    return out

def temporal_jitter_profile(samples:int,
                            base_interval_s:float,
                            temperature_k:float=None,
                            seed:int=None)->List[float]:
    """
    Returns time offsets with deterministic jitter dependent on temperature and dispersion.
    """
    T=float(temperature_k) if temperature_k is not None else utils.env_temperature_k()
    eff=constants_eq.get_effective(T,utils.env_velocity_fraction_c(),
                                   utils.env_flux_factor(),utils.env_strain_factor())
    disp=eff["stochastic_factor"]
    rng=utils.quantum_rng(utils.env_rng_seed() if seed is None else seed)
    out=[]
    for i in range(max(1,int(samples))):
        jitter=(rng.random()-0.5)*base_interval_s*0.1*disp
        out.append(i*base_interval_s+jitter)
    return out

def diagnostic_stochastic_spectrum(samples:int=128)->Dict[str,float]:
    """
    Analyzes power spectral density proxy of correlated noise to ensure boundedness.
    """
    data=correlated_noise_series(samples,0.8)
    mean=sum(data)/len(data)
    var=sum((x-mean)*(x-mean) for x in data)/len(data)
    maxval=max(abs(x) for x in data)
    out={"samples":float(samples),"variance":float(var),"maxval":float(maxval)}
    utils.store("telemetry/stochastic_spectrum",out)
    return out
