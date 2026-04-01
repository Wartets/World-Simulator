#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <stdexcept>

namespace ws {

struct StateBuffer {
    std::vector<float> data;
};

class TimeIntegrator {
public:
    virtual ~TimeIntegrator() = default;
    
    virtual std::string name() const = 0;
    virtual int order() const = 0;
    
    virtual void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) = 0;
};

class EulerExplicit : public TimeIntegrator {
public:
    std::string name() const override { return "Euler Explicit"; }
    int order() const override { return 1; }
    void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) override;
};

class RK4 : public TimeIntegrator {
public:
    std::string name() const override { return "RK4"; }
    int order() const override { return 4; }
    void step(StateBuffer& state, const std::vector<float>& derivatives, float dt) override;
};

class TimeIntegratorRegistry {
public:
    static TimeIntegratorRegistry& instance();
    
    void registerIntegrator(const std::string& id, std::unique_ptr<TimeIntegrator> integrator);
    std::shared_ptr<TimeIntegrator> get(const std::string& id) const;
    
private:
    TimeIntegratorRegistry();
    std::map<std::string, std::shared_ptr<TimeIntegrator>> registry;
};

} // namespace ws
