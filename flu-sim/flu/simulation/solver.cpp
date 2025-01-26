#include "flu/simulation/solver.hpp"
#include "flu/app/visualization.hpp"
#include "onyx/app/input.hpp"
#include "tkit/profiling/macros.hpp"

namespace Flu
{
template <Dimension D>
static f32 computeKernel(const KernelType p_Kernel, const f32 p_Radius, const f32 p_Distance) noexcept
{
    switch (p_Kernel)
    {
    case KernelType::Spiky2:
        return Kernel<D>::Spiky2(p_Radius, p_Distance);
    case KernelType::Spiky3:
        return Kernel<D>::Spiky3(p_Radius, p_Distance);
    case KernelType::Spiky5:
        return Kernel<D>::Spiky5(p_Radius, p_Distance);
    case KernelType::Poly6:
        return Kernel<D>::Poly6(p_Radius, p_Distance);
    case KernelType::CubicSpline:
        return Kernel<D>::CubicSpline(p_Radius, p_Distance);
    case KernelType::WendlandC2:
        return Kernel<D>::WendlandC2(p_Radius, p_Distance);
    case KernelType::WendlandC4:
        return Kernel<D>::WendlandC4(p_Radius, p_Distance);
    }
}
template <Dimension D>
static f32 computeKernelSlope(const KernelType p_Kernel, const f32 p_Radius, const f32 p_Distance) noexcept
{
    switch (p_Kernel)
    {
    case KernelType::Spiky2:
        return Kernel<D>::Spiky2Slope(p_Radius, p_Distance);
    case KernelType::Spiky3:
        return Kernel<D>::Spiky3Slope(p_Radius, p_Distance);
    case KernelType::Spiky5:
        return Kernel<D>::Spiky5Slope(p_Radius, p_Distance);
    case KernelType::Poly6:
        return Kernel<D>::Poly6Slope(p_Radius, p_Distance);
    case KernelType::CubicSpline:
        return Kernel<D>::CubicSplineSlope(p_Radius, p_Distance);
    case KernelType::WendlandC2:
        return Kernel<D>::WendlandC2Slope(p_Radius, p_Distance);
    case KernelType::WendlandC4:
        return Kernel<D>::WendlandC4Slope(p_Radius, p_Distance);
    }
}

template <Dimension D> f32 Solver<D>::getInfluence(const f32 p_Distance) const noexcept
{
    return computeKernel<D>(Settings.KType, Settings.SmoothingRadius, p_Distance);
}
template <Dimension D> f32 Solver<D>::getInfluenceSlope(const f32 p_Distance) const noexcept
{
    return computeKernelSlope<D>(Settings.KType, Settings.SmoothingRadius, p_Distance);
}

template <Dimension D> f32 Solver<D>::getNearInfluence(const f32 p_Distance) const noexcept
{
    return computeKernel<D>(Settings.NearKType, Settings.SmoothingRadius, p_Distance);
}
template <Dimension D> f32 Solver<D>::getNearInfluenceSlope(const f32 p_Distance) const noexcept
{
    return computeKernelSlope<D>(Settings.NearKType, Settings.SmoothingRadius, p_Distance);
}

template <Dimension D> f32 Solver<D>::getViscosityInfluence(const f32 p_Distance) const noexcept
{
    return computeKernel<D>(Settings.ViscosityKType, Settings.SmoothingRadius, p_Distance);
}

template <Dimension D>
fvec<D> Solver<D>::computePairwisePressureGradient(const u32 p_Index1, const u32 p_Index2,
                                                   const f32 p_Distance) const noexcept
{
    const fvec<D> dir = (m_Data.Positions[p_Index1] - m_Data.Positions[p_Index2]) / p_Distance;

    const f32 kernelGradient = getInfluenceSlope(p_Distance);
    const f32 nearKernelGradient = getNearInfluenceSlope(p_Distance);
    const auto [p1, np1] = GetPressureFromDensity(m_Data.Densities[p_Index1], m_Data.NearDensities[p_Index1]);
    const auto [p2, np2] = GetPressureFromDensity(m_Data.Densities[p_Index2], m_Data.NearDensities[p_Index2]);

    const f32 density = 0.5f * (m_Data.Densities[p_Index1] + m_Data.Densities[p_Index2]);
    const f32 ndensity = 0.5f * (m_Data.NearDensities[p_Index1] + m_Data.NearDensities[p_Index2]);

    const f32 dg1 = 0.5f * (p1 + p2) * kernelGradient / density;
    const f32 dg2 = 0.5f * (np1 + np2) * nearKernelGradient / ndensity;
    return (Settings.ParticleMass * (dg1 + dg2)) * dir;
}

template <Dimension D>
fvec<D> Solver<D>::computePairwiseViscosityTerm(const u32 p_Index1, const u32 p_Index2,
                                                const f32 p_Distance) const noexcept
{
    const fvec<D> diff = m_Data.Velocities[p_Index2] - m_Data.Velocities[p_Index1];
    const f32 kernel = getViscosityInfluence(p_Distance);

    const f32 u = glm::length(diff);
    return ((Settings.ViscLinearTerm + Settings.ViscQuadraticTerm * u) * kernel) * diff;
}

template <Dimension D> void Solver<D>::BeginStep(const f32 p_DeltaTime) noexcept
{
    TKIT_PROFILE_NSCOPE("Flu::Solver::BeginStep");
    m_Data.StagedPositions.resize(m_Data.Positions.size());

    std::swap(m_Data.Positions, m_Data.StagedPositions);
    for (u32 i = 0; i < m_Data.Positions.size(); ++i)
    {
        m_Data.Positions[i] = m_Data.StagedPositions[i] + m_Data.Velocities[i] * p_DeltaTime;
        m_Data.Densities[i] = Settings.ParticleMass;
        m_Data.NearDensities[i] = Settings.ParticleMass;
        m_Data.Accelerations[i] = fvec<D>{0.f};
    }
}
template <Dimension D> void Solver<D>::EndStep() noexcept
{
    std::swap(m_Data.Positions, m_Data.StagedPositions);
}
template <Dimension D> void Solver<D>::ApplyComputedForces(const f32 p_DeltaTime) noexcept
{
    TKIT_PROFILE_NSCOPE("Flu::Solver::ApplyComputedForces");
    for (u32 i = 0; i < m_Data.Positions.size(); ++i)
    {
        m_Data.Velocities[i].y += Settings.Gravity * p_DeltaTime / Settings.ParticleMass;
        m_Data.Velocities[i] += m_Data.Accelerations[i] * p_DeltaTime;
        m_Data.StagedPositions[i] += m_Data.Velocities[i] * p_DeltaTime;
        encase(i);
    }
}
template <Dimension D> void Solver<D>::AddMouseForce(const fvec<D> &p_MousePos) noexcept
{
    for (u32 i = 0; i < m_Data.Positions.size(); ++i)
    {
        const fvec<D> diff = m_Data.Positions[i] - p_MousePos;
        const f32 distance2 = glm::length2(diff);
        if (distance2 < Settings.MouseRadius * Settings.MouseRadius)
        {
            const f32 distance = glm::sqrt(distance2);
            const f32 factor = 1.f - distance / Settings.MouseRadius;
            m_Data.Accelerations[i] += (factor * Settings.MouseForce / distance) * diff;
        }
    }
}
template <Dimension D> void Solver<D>::ComputeDensities() noexcept
{
    TKIT_PROFILE_NSCOPE("Flu::Solver::ComputeDensities");
    ForEachPairWithinSmoothingRadiusST([this](const u32 p_Index1, const u32 p_Index2, const f32 p_Distance) {
        const f32 density = Settings.ParticleMass * getInfluence(p_Distance);
        const f32 nearDensity = Settings.ParticleMass * getNearInfluence(p_Distance);

        m_Data.Densities[p_Index1] += density;
        m_Data.NearDensities[p_Index1] += nearDensity;

        m_Data.Densities[p_Index2] += density;
        m_Data.NearDensities[p_Index2] += nearDensity;
    });
}
template <Dimension D> void Solver<D>::AddPressureAndViscosity() noexcept
{
    TKIT_PROFILE_NSCOPE("Flu::Solver::PressureAndViscosity");
    ForEachPairWithinSmoothingRadiusST([this](const u32 p_Index1, const u32 p_Index2, const f32 p_Distance) {
        const fvec<D> gradient = computePairwisePressureGradient(p_Index1, p_Index2, p_Distance);
        const fvec<D> term = computePairwiseViscosityTerm(p_Index1, p_Index2, p_Distance);

        const fvec<D> dv1 = term - gradient / m_Data.Densities[p_Index1];
        const fvec<D> dv2 = term - gradient / m_Data.Densities[p_Index2];

        m_Data.Accelerations[p_Index1] += dv1;
        m_Data.Accelerations[p_Index2] -= dv2;
    });
}

template <Dimension D>
std::pair<f32, f32> Solver<D>::GetPressureFromDensity(const f32 p_Density, const f32 p_NearDensity) const noexcept
{
    const f32 p1 = Settings.PressureStiffness * (p_Density - Settings.TargetDensity);
    const f32 p2 = Settings.NearPressureStiffness * p_NearDensity;
    return {p1, p2};
}

template <Dimension D> void Solver<D>::UpdateLookup() noexcept
{
    m_Lookup.SetPositions(&m_Data.Positions);
    switch (Settings.SearchMethod)
    {
    case NeighborSearch::BruteForce:
        m_Lookup.UpdateBruteForceLookup(Settings.SmoothingRadius);
        break;
    case NeighborSearch::Grid:
        m_Lookup.UpdateGridLookup(Settings.SmoothingRadius);
        break;
    }
}

template <Dimension D> void Solver<D>::AddParticle(const fvec<D> &p_Position) noexcept
{
    m_Data.Positions.push_back(p_Position);
    m_Data.Velocities.push_back(fvec<D>{0.f});
    m_Data.Accelerations.push_back(fvec<D>{0.f});
    m_Data.Densities.push_back(Settings.ParticleMass);
    m_Data.NearDensities.push_back(Settings.ParticleMass);
}

template <Dimension D> void Solver<D>::encase(const u32 p_Index) noexcept
{
    const f32 factor = 1.f - Settings.EncaseFriction;
    for (u32 j = 0; j < D; ++j)
    {
        if (m_Data.StagedPositions[p_Index][j] - Settings.ParticleRadius < BoundingBox.Min[j])
        {
            m_Data.StagedPositions[p_Index][j] = BoundingBox.Min[j] + Settings.ParticleRadius;
            m_Data.Velocities[p_Index][j] = -factor * m_Data.Velocities[p_Index][j];
        }
        else if (m_Data.StagedPositions[p_Index][j] + Settings.ParticleRadius > BoundingBox.Max[j])
        {
            m_Data.StagedPositions[p_Index][j] = BoundingBox.Max[j] - Settings.ParticleRadius;
            m_Data.Velocities[p_Index][j] = -factor * m_Data.Velocities[p_Index][j];
        }
    }
}

template <Dimension D> void Solver<D>::DrawBoundingBox(Onyx::RenderContext<D> *p_Context) const noexcept
{
    Visualization<D>::DrawBoundingBox(p_Context, BoundingBox.Min, BoundingBox.Max,
                                      Onyx::Color::FromHexadecimal("A6B1E1", false));
}
template <Dimension D> void Solver<D>::DrawParticles(Onyx::RenderContext<D> *p_Context) const noexcept
{
    const f32 particleSize = 2.f * Settings.ParticleRadius;

    const Onyx::Gradient gradient{Settings.Gradient};
    for (u32 i = 0; i < m_Data.Positions.size(); ++i)
    {
        const fvec<D> &pos = m_Data.Positions[i];
        const fvec<D> &vel = m_Data.Velocities[i];

        const f32 speed = glm::min(Settings.FastSpeed, glm::length(vel));
        const Onyx::Color color = gradient.Evaluate(speed / Settings.FastSpeed);
        Visualization<D>::DrawParticle(p_Context, pos, particleSize, color);
    }
}

template <Dimension D> u32 Solver<D>::GetParticleCount() const noexcept
{
    return m_Data.Positions.size();
}

template <Dimension D> const Lookup<D> &Solver<D>::GetLookup() const noexcept
{
    return m_Lookup;
}
template <Dimension D> const SimulationData<D> &Solver<D>::GetData() const noexcept
{
    return m_Data;
}

template class Solver<Dimension::D2>;
template class Solver<Dimension::D3>;

} // namespace Flu