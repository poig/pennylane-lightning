// Copyright 2022 Xanadu Quantum Technologies Inc.

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <bitset>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <span>

#include "StateVectorLQubitManaged.hpp"

#include "CacheManager.hpp"
#include "Exception.hpp"
#include "LightningObsManager.hpp"
#include "QuantumDevice.hpp"
#include "QubitManager.hpp"

namespace Catalyst::Runtime::Simulator {
class LightningSimulator final : public Catalyst::Runtime::QuantumDevice {
  private:
    using StateVectorT =
        Pennylane::LightningQubit::StateVectorLQubitManaged<double>;

    // static constants for RESULT values
    static constexpr bool GLOBAL_RESULT_TRUE_CONST = true;
    static constexpr bool GLOBAL_RESULT_FALSE_CONST = false;

    static constexpr size_t default_num_burnin{
        100}; // tidy: readability-magic-numbers
    static constexpr std::string_view default_kernel_name{
        "Local"}; // tidy: readability-magic-numbers

    Catalyst::Runtime::QubitManager<QubitIdType, size_t> qubit_manager{};
    Catalyst::Runtime::CacheManager<std::complex<double>> cache_manager{};
    bool tape_recording{false};
    size_t device_shots{0};

    std::mt19937 *gen = nullptr;

    bool mcmc{false};
    size_t num_burnin{0};
    std::string kernel_name;

    std::unique_ptr<StateVectorT> device_sv = std::make_unique<StateVectorT>(0);
    LightningObsManager<double> obs_manager{};

    inline auto isValidQubit(QubitIdType wire) -> bool {
        return this->qubit_manager.isValidQubitId(wire);
    }

    inline auto isValidQubits(const std::vector<QubitIdType> &wires) -> bool {
        return std::all_of(wires.begin(), wires.end(), [this](QubitIdType w) {
            return this->isValidQubit(w);
        });
    }

    inline auto isValidQubits(size_t numWires, const QubitIdType *wires)
        -> bool {
        return std::all_of(wires, wires + numWires, [this](QubitIdType w) {
            return this->isValidQubit(w);
        });
    }

    inline auto getDeviceWires(const std::vector<QubitIdType> &wires)
        -> std::vector<size_t> {
        std::vector<size_t> res;
        res.reserve(wires.size());
        std::transform(
            wires.begin(), wires.end(), std::back_inserter(res),
            [this](auto w) { return this->qubit_manager.getDeviceId(w); });
        return res;
    }

    inline auto generateSeed() -> std::optional<std::size_t> {
        if (this->gen != nullptr) {
            return (*(this->gen))();
        }

        return std::nullopt;
    }

    auto GenerateSamplesMetropolis(size_t shots) -> std::vector<size_t>;
    auto GenerateSamples(size_t shots) -> std::vector<size_t>;

  public:
    explicit LightningSimulator(
        const std::string &kwargs = "{}") // NOLINT(hicpp-member-init)
    {
        auto &&args = Catalyst::Runtime::parse_kwargs(kwargs);
        mcmc = args.contains("mcmc") ? args["mcmc"] == "True" : false;
        num_burnin = args.contains("num_burnin")
                         ? static_cast<size_t>(std::stoll(args["num_burnin"]))
                         : default_num_burnin;
        kernel_name = args.contains("kernel_name") ? args["kernel_name"]
                                                   : default_kernel_name;
    }
    ~LightningSimulator() override = default;

    LightningSimulator(const LightningSimulator &) = delete;
    LightningSimulator &operator=(const LightningSimulator &) = delete;
    LightningSimulator(LightningSimulator &&) = delete;
    LightningSimulator &operator=(LightningSimulator &&) = delete;

    auto AllocateQubit() -> QubitIdType override;
    auto AllocateQubits(std::size_t num_qubits)
        -> std::vector<QubitIdType> override;
    void ReleaseQubit(QubitIdType q) override;
    void ReleaseAllQubits() override;
    [[nodiscard]] auto GetNumQubits() const -> std::size_t override;
    void StartTapeRecording() override;
    void StopTapeRecording() override;
    void SetDeviceShots(std::size_t shots) override;
    void SetDevicePRNG(std::mt19937 *gen) override;
    void SetState(DataView<std::complex<double>, 1> &state,
                  std::vector<QubitIdType> &wires) override;
    void SetBasisState(DataView<int8_t, 1> &n,
                       std::vector<QubitIdType> &wires) override;
    [[nodiscard]] auto GetDeviceShots() const -> std::size_t override;

    void
    NamedOperation(const std::string &name, const std::vector<double> &params,
                   const std::vector<QubitIdType> &wires, bool inverse = false,
                   const std::vector<QubitIdType> &controlled_wires = {},
                   const std::vector<bool> &controlled_values = {}) override;
    using Catalyst::Runtime::QuantumDevice::MatrixOperation;
    void
    MatrixOperation(const std::vector<std::complex<double>> &matrix,
                    const std::vector<QubitIdType> &wires, bool inverse = false,
                    const std::vector<QubitIdType> &controlled_wires = {},
                    const std::vector<bool> &controlled_values = {}) override;
    auto Observable(ObsId id, const std::vector<std::complex<double>> &matrix,
                    const std::vector<QubitIdType> &wires)
        -> ObsIdType override;
    auto TensorObservable(const std::vector<ObsIdType> &obs)
        -> ObsIdType override;
    auto HamiltonianObservable(const std::vector<double> &coeffs,
                               const std::vector<ObsIdType> &obs)
        -> ObsIdType override;
    auto Expval(ObsIdType obsKey) -> double override;
    auto Var(ObsIdType obsKey) -> double override;
    void State(DataView<std::complex<double>, 1> &state) override;
    void Probs(DataView<double, 1> &probs) override;
    void PartialProbs(DataView<double, 1> &probs,
                      const std::vector<QubitIdType> &wires) override;
    void Sample(DataView<double, 2> &samples) override;
    void PartialSample(DataView<double, 2> &samples,
                       const std::vector<QubitIdType> &wires) override;
    void Counts(DataView<double, 1> &eigvals,
                DataView<int64_t, 1> &counts) override;
    void PartialCounts(DataView<double, 1> &eigvals,
                       DataView<int64_t, 1> &counts,
                       const std::vector<QubitIdType> &wires) override;
    auto Measure(QubitIdType wire,
                 std::optional<int32_t> postselect = std::nullopt)
        -> Result override;
    void Gradient(std::vector<DataView<double, 1>> &gradients,
                  const std::vector<std::size_t> &trainParams) override;

    auto CacheManagerInfo()
        -> std::tuple<std::size_t, std::size_t, std::size_t,
                      std::vector<std::string>, std::vector<ObsIdType>>;
};
} // namespace Catalyst::Runtime::Simulator
