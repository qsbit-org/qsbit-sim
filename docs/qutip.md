# QuTiP pulse models

Install the [optional `qutip` extra](backends.md#install-an-optional-backend) and select `"backend": "qutip"` in the run
configuration. The [pulse experiments](../examples/qutip/README.md) provide a
complete model, qsbit control-program generation and result plots.

```sh
build-python/qsbit-sim --backend qutip --help-backend
build-python/qsbit-sim --backend qutip --generate-config > qutip.json
build-python/qsbit-sim --backend qutip --backend-schema > qutip.schema.json
```

Fill in the required model and solver parameters before running the generated
configuration. `backend_options` owns the physical model; profile mappings
select waveforms by `operation` and supply their target, axis, amplitude and duration.

## Hamiltonian and dissipation

Each entry in `subsystems` defines a truncated oscillator with `levels` states.
Its index is the event target. Set `profile.qubits` to the number of entries,
including any explicitly modeled coupler. The total Hilbert-space dimension is
the product of the level counts and must not exceed `max_dimension`.

With $\hbar=1$, the undriven Hamiltonian is

$$
H_0=\sum_j\left[\Delta_j n_j+\frac{\alpha_j}{2}n_j(n_j-1)\right]
 +\sum_{(j,k)}g_{jk}(a_j^\dagger a_k+a_k^\dagger a_j).
$$

`detuning_rad_ns`, `anharmonicity_rad_ns` and coupling `strength_rad_ns` specify
$\Delta_j$, $\alpha_j$ and $g_{jk}$ in radians per nanosecond. Detunings use one
common rotating frame. `initial_populations` supplies one nonnegative probability
per level, summing to one. Reset initializes the tensor product of these diagonal
density matrices.

`t1_ns`, `thermal_occupation` and `tphi_ns` define the Lindblad operators

$$
L_{j,-}=\sqrt{(\bar n_j+1)/T_{1,j}}\,a_j,\qquad
L_{j,+}=\sqrt{\bar n_j/T_{1,j}}\,a_j^\dagger,\qquad
L_{j,\phi}=\sqrt{2/T_{\phi,j}}\,n_j.
$$

A null lifetime disables that channel. For two levels at zero temperature,
$P_1(t)=P_1(0)e^{-t/T_1}$ and $1/T_2=1/(2T_1)+1/T_\phi$. At nonzero thermal
occupation, the two-level population decay rate is $(2\bar n+1)/T_1$.

QuTiP `mesolve` advances the persistent density matrix through every elapsed
interval, including idle time and acquisition. `solver` sets `rtol`, `atol`,
`max_step_ns` and `max_steps`. Choose a maximum step smaller than the narrowest
waveform feature and verify convergence by tightening these settings. Numerical
integration does not advance CPU or TCU time.

## Waveforms

Each `waveforms` entry has a unique `operation`, `shape`, `phase_rad` and
`frequency_rad_ns`. For an event starting at $s$ with duration $D$, its envelope
uses elapsed time $u=t-s$:

| Shape | Envelope |
| --- | --- |
| `square` | Constant unit envelope. |
| `gaussian` | $G(u)=\exp[-(u-D/2)^2/(2\sigma^2)]$ with `sigma_ns`. |
| `drag` | $G(u)+i\beta G'(u)$ with `sigma_ns` and `beta_ns`. |
| `samples` | Linear interpolation of the configured `iq` pairs at `times_ns`. |

Sample times must strictly increase from zero to the event duration. Gaussian
envelopes are truncated at the event boundaries without subtracting their endpoints.

For X and Y events, the complex drive is

$$
\Omega(t)=A f(t-s)e^{i(\phi-\omega t+\theta)},\qquad
H_d(t)=\frac{\Re\Omega(t)}2(a+a^\dagger)
       +\frac{\Im\Omega(t)}2 i(a^\dagger-a),
$$

where $A$ is the event amplitude, $\phi$ is `phase_rad`, $\omega$ is
`frequency_rad_ns`, and $\theta$ is zero for X and $\pi/2$ for Y. Carrier phase
uses absolute simulation time. Splitting a pulse at another device event preserves
its envelope origin and carrier phase.

A Z event changes the oscillator frequency by adding $A f(t-s)n$ to the
Hamiltonian. It requires a real envelope with zero carrier frequency and phase.
All active drives and fixed couplings evolve jointly. Ideal `gate` and
`gate_output` operations are unsupported; two-subsystem interactions arise from
the configured Hamiltonian.

## Measurement and readout

At acquisition end, the backend samples an oscillator level and projects the
joint density matrix onto that level. Without a `readout` entry, level zero
returns false and every excited level returns true. The projected state persists
for subsequent operations. Result delivery follows the profile's
`discriminator_delay` and receiver clocks.

A `readout` entry configures a classical IQ assignment model for one `target`:

| Field | Meaning |
| --- | --- |
| `means` | Steady-state I and Q values for each level. |
| `ringup_ns` | Exponential response time. The response starts at zero for each acquisition. |
| `noise_std_sqrt_ns` | Independent white-noise strength in each quadrature. A bin of width $\delta t$ has standard deviation $\sigma/\sqrt{\delta t}$. |
| `sample_interval_ns` | Maximum bin width; equal-width bins cover the complete acquisition. |
| `rotation_rad` | Rotation applied to the integrated IQ value before classification. |
| `threshold` | Return true when the rotated I component exceeds this value. |

For the sampled level $l$, the mean record is $\mu_l(1-e^{-u/\tau})$.
Each bin contains its exact average response plus Gaussian noise. Classification
uses the arithmetic mean of the bins. The model does not simulate a resonator
quantum state, continuous measurement backaction, or state transitions within
the readout record. Quantum relaxation still acts through the acquisition interval
before the final projective sample.

`measurements` optionally names a JSONL diagnostics file, resolved from the process
working directory. Reset clears the file. Each measurement records its tick,
reference, pre-projection level probabilities, sampled level and returned bit.
Configured readout adds integrated IQ, bin-end timestamps and IQ samples.

## State output and reproducibility

The backend exports a density matrix. Subsystem zero is the least significant
mixed-radix digit: for level counts $d_0,d_1,\ldots$, basis index
$l_0+d_0l_1+d_0d_1l_2+\cdots$ represents those oscillator levels.
The summary includes the resolved backend options. Reset seeds level sampling and
readout noise from the profile seed. Changing the execution-batch limit does not
consume additional random draws.

The model uses a rotating-wave Hamiltonian, truncated oscillator levels and
Markovian dissipation. Hardware comparisons require calibrated frequencies,
couplings, delivered waveforms and readout parameters.

The solver interface follows QuTiP's
[time-dependent dynamics](https://qutip.readthedocs.io/en/stable/guide/dynamics/dynamics-time.html)
and [Lindblad master equation](https://qutip.readthedocs.io/en/stable/guide/dynamics/dynamics-master.html)
documentation.
