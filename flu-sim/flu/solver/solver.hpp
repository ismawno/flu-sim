#pragma once

#include "flu/solver/kernel.hpp"
#include "flu/core/glm.hpp"
#include "onyx/rendering/render_context.hpp"

namespace Flu
{
enum class NeighborSearch
{
    BruteForce = 0,
    Grid
};

struct SimulationSettings
{
    f32 ParticleRadius = 0.1f;
    f32 ParticleMass = 1.f;

    f32 TargetDensity = 10.f;
    f32 PressureStiffness = 100.f;
    f32 NearPressureStiffness = 25.f;
    f32 SmoothingRadius = 1.f;

    f32 FastSpeed = 35.f;
    f32 Gravity = -4.f;
    f32 EncaseFriction = 0.8f;

    f32 ViscLinearTerm = 0.0f;
    f32 ViscQuadraticTerm = 0.0f;
    KernelType ViscosityKType = KernelType::Poly6;

    f32 MouseRadius = 6.f;
    f32 MouseForce = -30.f;

    std::array<Onyx::Color, 3> Gradient = {Onyx::Color::CYAN, Onyx::Color::YELLOW, Onyx::Color::RED};

    NeighborSearch SearchMethod = NeighborSearch::Grid;
    KernelType KType = KernelType::Spiky3;
    KernelType NearKType = KernelType::Spiky5;
};

template <Dimension D> class Solver
{
  public:
    void BeginStep(f32 p_DeltaTime) noexcept;
    void EndStep(f32 p_DeltaTime) noexcept;
    void ApplyMouseForce(const fvec<D> &p_MousePos, f32 p_Timestep) noexcept;

    void UpdateGrid() noexcept;

    std::pair<f32, f32> ComputeDensitiesAtPoint(const fvec<D> &p_Point) const noexcept;

    fvec<D> ComputeViscosityTerm(u32 p_Index) const noexcept;
    fvec<D> ComputePressureGradient(u32 p_Index) const noexcept;

    std::pair<f32, f32> GetPressureFromDensity(f32 p_Density, f32 p_NearDensity) const noexcept;

    template <typename F>
    void ForEachParticleWithinSmoothingRadius(const fvec<D> &p_Point, F &&p_Function) const noexcept
    {
        if (Settings.SearchMethod == NeighborSearch::BruteForce)
            forEachBruteForce(p_Point, std::forward<F>(p_Function));
        else
            forEachGrid(p_Point, std::forward<F>(p_Function));
    }

    void AddParticle(const fvec<D> &p_Position) noexcept;

    void DrawBoundingBox(Onyx::RenderContext<D> *p_Context) const noexcept;
    void DrawParticles(Onyx::RenderContext<D> *p_Context) const noexcept;

    SimulationSettings Settings{};

    DynamicArray<fvec<D>> Positions;
    DynamicArray<fvec<D>> Velocities;

    struct
    {
        fvec<D> Min{-15.f};
        fvec<D> Max{15.f};
    } BoundingBox;

  private:
    struct IndexPair
    {
        u32 ParticleIndex;
        u32 CellIndex;
    };
    template <typename F> void forEachBruteForce(const fvec<D> &p_Point, F &&p_Function) const noexcept
    {
        const f32 r2 = Settings.SmoothingRadius * Settings.SmoothingRadius;
        for (u32 i = 0; i < Positions.size(); ++i)
        {
            const f32 distance = glm::distance2(p_Point, Positions[i]);
            if (distance < r2)
                std::forward<F>(p_Function)(i, glm::sqrt(distance));
        }
    }
    template <typename F> void forEachGrid(const fvec<D> &p_Point, F &&p_Function) const noexcept
    {
        if (Positions.empty())
            return;
        const f32 r2 = Settings.SmoothingRadius * Settings.SmoothingRadius;
        const auto processParticle = [this, &p_Point, r2](const ivec<D> &p_Offset, F &&p_Function) {
            const ivec<D> cellPosition = getCellPosition(p_Point) + p_Offset;
            const u32 cellIndex = getCellIndex(cellPosition);
            const u32 startIndex = m_StartIndices[cellIndex];
            for (u32 i = startIndex; i < m_SpatialLookup.size() && m_SpatialLookup[i].CellIndex == cellIndex; ++i)
            {
                const u32 particleIndex = m_SpatialLookup[i].ParticleIndex;
                const f32 distance = glm::distance2(p_Point, Positions[particleIndex]);
                if (distance < r2)
                    std::forward<F>(p_Function)(particleIndex, glm::sqrt(distance));
            }
        };

        for (i32 offsetX = -1; offsetX <= 1; ++offsetX)
            for (i32 offsetY = -1; offsetY <= 1; ++offsetY)
                if constexpr (D == D2)
                {
                    const ivec<D> offset = {offsetX, offsetY};
                    processParticle(offset, std::forward<F>(p_Function));
                }
                else
                    for (i32 offsetZ = -1; offsetZ <= 1; ++offsetZ)
                    {
                        const ivec<D> offset = {offsetX, offsetY, offsetZ};
                        processParticle(offset, std::forward<F>(p_Function));
                    }
    }

    void encase(usize p_Index) noexcept;

    f32 getInfluence(f32 p_Distance) const noexcept;
    f32 getInfluenceSlope(f32 p_Distance) const noexcept;

    f32 getNearInfluence(f32 p_Distance) const noexcept;
    f32 getNearInfluenceSlope(f32 p_Distance) const noexcept;

    f32 getViscosityInfluence(f32 p_Distance) const noexcept;

    ivec<D> getCellPosition(const fvec<D> &p_Position) const noexcept;
    u32 getCellIndex(const ivec<D> &p_CellPosition) const noexcept;

    DynamicArray<fvec<D>> m_PredictedPositions;

    DynamicArray<f32> m_Densities;
    DynamicArray<f32> m_NearDensities;

    DynamicArray<IndexPair> m_SpatialLookup;
    DynamicArray<u32> m_StartIndices;
};
} // namespace Flu