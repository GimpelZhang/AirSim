#pragma once

#include "CoreMinimal.h"

#include "CarPawn.h"
#include "common/Common.hpp"
#include "api/VehicleSimApiBase.hpp"
#include <memory>
#include <vector>
#include "physics/FastPhysicsEngine.hpp"
#include "physics/World.hpp"
#include "physics/PhysicsWorld.hpp"
#include "common/StateReporterWrapper.hpp"
#include "api/ApiServerBase.hpp"
#include "SimMode/SimModeBase.h"
#include "SimModeCar.generated.h"


UCLASS()
class AIRSIM_API ASimModeCar : public ASimModeBase
{
    GENERATED_BODY()

public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Tick( float DeltaSeconds ) override;

    virtual void reset() override;
    virtual std::string getDebugReport() override;

    virtual bool isPaused() const override;
    virtual void pause(bool is_paused) override;
    virtual void continueForTime(double seconds) override;

    virtual void pause_physx(bool is_paused);

protected: //overrides
    void startAsyncUpdator();
    void stopAsyncUpdator();
    virtual void updateDebugReport(msr::airlib::StateReporterWrapper& debug_reporter) override;

    //should be called by derived class once all api_provider_ is ready to use
    void initializeForPlay();

    long long getPhysicsLoopPeriod() const;
    void setPhysicsLoopPeriod(long long  period);
private:
    typedef msr::airlib::UpdatableObject UpdatableObject;
    typedef msr::airlib::PhysicsEngineBase PhysicsEngineBase;
    typedef msr::airlib::ClockFactory ClockFactory;

    //create the physics engine as needed from settings
    std::unique_ptr<PhysicsEngineBase> createPhysicsEngine();

    virtual void setupClockSpeed() override;
    virtual std::unique_ptr<msr::airlib::ApiServerBase> createApiServer() const override;
    virtual void getExistingVehiclePawns(TArray<AActor*>& pawns) const override;
    virtual bool isVehicleTypeSupported(const std::string& vehicle_type) const override;
    virtual std::string getVehiclePawnPathName(const AirSimSettings::VehicleSetting& vehicle_setting) const override;
    virtual PawnEvents* getVehiclePawnEvents(APawn* pawn) const override;
    virtual const common_utils::UniqueValueMap<std::string, APIPCamera*> getVehiclePawnCameras(APawn* pawn) const override;
    virtual void initializeVehiclePawn(APawn* pawn) override;
    virtual std::unique_ptr<PawnSimApi> createVehicleSimApi(
        const PawnSimApi::Params& pawn_sim_api_params) const override;
    virtual msr::airlib::VehicleApiBase* getVehicleApi(const PawnSimApi::Params& pawn_sim_api_params,
        const PawnSimApi* sim_api) const override;

private:
    std::unique_ptr<msr::airlib::PhysicsWorld> physics_world_;
    PhysicsEngineBase* physics_engine_;

    /*
    300Hz seems to be minimum for non-aggressive flights
    400Hz is needed for moderately aggressive flights (such as
    high yaw rate with simultaneous back move)
    500Hz is recommended for more aggressive flights
    Lenovo P50 high-end config laptop seems to be topping out at 400Hz.
    HP Z840 desktop high-end config seems to be able to go up to 500Hz.
    To increase freq with limited CPU power, switch Barometer to constant ref mode.
    */
    long long physics_loop_period_ = 3000000LL; //3ms
    typedef common_utils::Utils Utils;
    typedef msr::airlib::TTimePoint TTimePoint;
    typedef msr::airlib::TTimeDelta TTimeDelta;
    typedef ACarPawn TVehiclePawn;
    typedef msr::airlib::VehicleSimApiBase VehicleSimApiBase;
    typedef msr::airlib::VectorMath VectorMath;
    typedef msr::airlib::Vector3r Vector3r;
};
