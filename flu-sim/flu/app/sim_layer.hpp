#pragma once

#include "onyx/app/user_layer.hpp"
#include "onyx/app/app.hpp"
#include "onyx/rendering/render_context.hpp"
#include "flu/simulation/solver.hpp"
#include "flu/app/inspector.hpp"

namespace Flu
{
template <Dimension D> class SimLayer final : public Onyx::UserLayer
{
  public:
    SimLayer(Onyx::Application *p_Application, const SimulationSettings &p_Settings, const uvec<D> &p_StartingLayout,
             const BoundingBox<D> &p_BoundingBox) noexcept;

  private:
    void OnUpdate() noexcept override;
    void OnRender(VkCommandBuffer) noexcept override;
    bool OnEvent(const Onyx::Event &p_Event) noexcept override;

    void step(bool p_Dummy = false) noexcept;
    void renderVisualizationSettings() noexcept;

    Onyx::Application *m_Application;
    Onyx::Window *m_Window;

    Solver<D> m_Solver;
#ifdef FLU_ENABLE_INSPECTOR
    Inspector<D> m_Inspector{&m_Solver};
#endif
    Onyx::RenderContext<D> *m_Context;

    f32 m_Timestep = 1.f / 60.f;
    bool m_DummyStep = false;
    bool m_Pause = false;
};
} // namespace Flu