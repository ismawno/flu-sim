#include "flu/solver/solver.hpp"
#include "flu/app/visualization.hpp"
#include "onyx/app/input.hpp"
#include "tkit/container/hashable_tuple.hpp"
#include "tkit/utilities/math.hpp"

namespace Flu
{
template <Dimension D> f32 Solver<D>::getInfluence(const f32 p_Distance) const noexcept
{
    if (Settings.KernelType == KernelType::Spiky)
        return Kernel<D>::Spiky(Settings.SmoothingRadius, p_Distance);
    return Kernel<D>::CubicSpline(Settings.SmoothingRadius, p_Distance);
}
template <Dimension D> f32 Solver<D>::getInfluenceSlope(const f32 p_Distance) const noexcept
{
    if (Settings.KernelType == KernelType::Spiky)
        return Kernel<D>::SpikySlope(Settings.SmoothingRadius, p_Distance);
    return Kernel<D>::CubicSplineSlope(Settings.SmoothingRadius, p_Distance);
}

template <Dimension D> ivec<D> Solver<D>::getCellPosition(const fvec<D> &p_Position) const noexcept
{
    ivec<D> cellPosition{0};
    for (u32 i = 0; i < D; ++i)
        cellPosition[i] = static_cast<i32>(p_Position[i] / Settings.SmoothingRadius);
    return cellPosition;
}
template <Dimension D> u32 Solver<D>::getCellIndex(const ivec<D> &p_CellPosition) const noexcept
{
    const u32 particles = static_cast<u32>(Positions.size());
    if constexpr (D == D2)
    {
        TKit::HashableTuple<u32, u32> key{p_CellPosition.x, p_CellPosition.y};
        return static_cast<u32>(key() % particles);
    }
    else
    {
        TKit::HashableTuple<u32, u32, u32> key{p_CellPosition.x, p_CellPosition.y, p_CellPosition.z};
        return static_cast<u32>(key() % particles);
    }
}

template <Dimension D> void Solver<D>::BeginStep(const f32 p_DeltaTime) noexcept
{
    m_PredictedPositions.resize(Positions.size());
    for (usize i = 0; i < Positions.size(); ++i)
    {
        Velocities[i].y += Settings.Gravity * p_DeltaTime / Settings.ParticleMass;
        m_PredictedPositions[i] = Positions[i] + Velocities[i] * p_DeltaTime;
    }
    std::swap(Positions, m_PredictedPositions);

    UpdateGrid();
    for (usize i = 0; i < Positions.size(); ++i)
        m_Densities[i] = ComputeDensityAtPoint(Positions[i]);

    for (u32 i = 0; i < Positions.size(); ++i)
    {
        const fvec<D> pressureGradient = ComputePressureGradient(i);
        Velocities[i] -= pressureGradient * p_DeltaTime / m_Densities[i];
    }
}
template <Dimension D> void Solver<D>::EndStep(const f32 p_DeltaTime) noexcept
{
    std::swap(Positions, m_PredictedPositions);
    for (usize i = 0; i < Positions.size(); ++i)
    {
        Positions[i] += Velocities[i] * p_DeltaTime;
        encase(i);
    }
}
template <Dimension D> void Solver<D>::ApplyMouseForce(const fvec<D> &p_MousePos, const f32 p_Timestep) noexcept
{
    for (u32 i = 0; i < Positions.size(); ++i)
    {
        const fvec<D> diff = Positions[i] - p_MousePos;
        const f32 distance = glm::length(diff);
        if (distance < Settings.MouseRadius)
        {
            const f32 factor = 1.f - distance / Settings.MouseRadius;
            Velocities[i] += (factor * Settings.MouseForce * p_Timestep / distance) * diff;
        }
    }
}

template <Dimension D> void Solver<D>::UpdateGrid() noexcept
{
    m_SpatialLookup.clear();
    for (u32 i = 0; i < Positions.size(); ++i)
    {
        const ivec<D> cellPosition = getCellPosition(Positions[i]);
        const u32 index = getCellIndex(cellPosition);
        m_SpatialLookup.push_back({i, index});
    }
    std::sort(m_SpatialLookup.begin(), m_SpatialLookup.end(),
              [](const IndexPair &a, const IndexPair &b) { return a.CellIndex < b.CellIndex; });

    u32 prevIndex = 0;
    m_StartIndices.resize(Positions.size(), UINT32_MAX);
    for (u32 i = 0; i < m_SpatialLookup.size(); ++i)
    {
        if (i == 0 || m_SpatialLookup[i].CellIndex != prevIndex)
            m_StartIndices[m_SpatialLookup[i].CellIndex] = i;

        prevIndex = m_SpatialLookup[i].CellIndex;
    }
}

template <Dimension D> f32 Solver<D>::ComputeDensityAtPoint(const fvec<D> &p_Point) const noexcept
{
    f32 density = 0.f;
    ForEachParticleWithinSmoothingRadius(p_Point, [this, &density](const u32, const f32 p_Distance) {
        density += Settings.ParticleMass * getInfluence(p_Distance);
    });
    return density;
}
template <Dimension D>
static fvec<D> getDirection(const fvec<D> &p_P1, const fvec<D> &p_P2, const f32 p_Distance) noexcept
{
    fvec<D> dir;
    if constexpr (D == D2)
        dir = {1.f, 0.f};
    else
        dir = {1.f, 0.f, 0.f};
    if (TKit::ApproachesZero(p_Distance))
        return dir;
    return (p_P1 - p_P2) / p_Distance;
}
template <Dimension D> fvec<D> Solver<D>::ComputePressureGradient(const u32 p_Index1) const noexcept
{
    fvec<D> gradient{0.f};
    ForEachParticleWithinSmoothingRadius(
        Positions[p_Index1], [this, &gradient, p_Index1](const u32 p_Index2, const f32 distance) {
            if (p_Index2 == p_Index1)
                return;
            const fvec<D> dir = getDirection<D>(Positions[p_Index1], Positions[p_Index2], distance);
            const f32 kernelGradient = getInfluenceSlope(distance);
            const f32 p1 = GetPressureFromDensity(m_Densities[p_Index1]);
            const f32 p2 = GetPressureFromDensity(m_Densities[p_Index2]);
            gradient += (0.5f * (p1 + p2) * Settings.ParticleMass * kernelGradient / m_Densities[p_Index2]) * dir;
        });
    return gradient;
}

template <Dimension D> f32 Solver<D>::GetPressureFromDensity(const f32 p_Density) const noexcept
{
    return Settings.PressureStiffness * (p_Density - Settings.TargetDensity);
}

template <Dimension D> void Solver<D>::AddParticle(const fvec<D> &p_Position) noexcept
{
    Positions.push_back(p_Position);
    Velocities.push_back(fvec<D>{0.f});
    m_Densities.resize(Positions.size(), 0.f);
}

template <Dimension D> void Solver<D>::encase(const usize p_Index) noexcept
{
    const f32 factor = 1.f - Settings.EncaseFriction;
    for (u32 j = 0; j < D; ++j)
    {
        if (Positions[p_Index][j] - Settings.ParticleRadius < BoundingBox.Min[j])
        {
            Positions[p_Index][j] = BoundingBox.Min[j] + Settings.ParticleRadius;
            Velocities[p_Index][j] = -factor * Velocities[p_Index][j];
        }
        else if (Positions[p_Index][j] + Settings.ParticleRadius > BoundingBox.Max[j])
        {
            Positions[p_Index][j] = BoundingBox.Max[j] - Settings.ParticleRadius;
            Velocities[p_Index][j] = -factor * Velocities[p_Index][j];
        }
    }
}

template <Dimension D> void Solver<D>::DrawBoundingBox(Onyx::RenderContext<D> *p_Context) const noexcept
{
    Visualization<D>::DrawBoundingBox(p_Context, Onyx::Color::WHITE, BoundingBox.Min, BoundingBox.Max);
}
template <Dimension D> void Solver<D>::DrawParticles(Onyx::RenderContext<D> *p_Context) const noexcept
{
    const f32 particleSize = 2.f * Settings.ParticleRadius;
    const Onyx::Gradient gradient{Settings.Gradient};
    for (usize i = 0; i < Positions.size(); ++i)
    {
        const fvec<D> &pos = Positions[i];
        const fvec<D> &vel = Velocities[i];

        const f32 speed = glm::min(Settings.FastSpeed, glm::length(vel));
        const Onyx::Color color = gradient.Evaluate(speed / Settings.FastSpeed);
        Visualization<D>::DrawParticle(p_Context, pos, color, particleSize);
    }
}

template class Solver<Dimension::D2>;
template class Solver<Dimension::D3>;

} // namespace Flu