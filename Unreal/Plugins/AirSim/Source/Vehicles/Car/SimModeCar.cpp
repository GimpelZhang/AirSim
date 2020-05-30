// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "SimModeCar.h"
#include "UObject/ConstructorHelpers.h"
#include "Logging/MessageLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include <exception>
#include "AirBlueprintLib.h"
#include "common/AirSimSettings.hpp"
//#include "vehicles/multirotor/api/MultirotorApiBase.hpp"
#include "CarPawnSimApi.h"
#include "physics/PhysicsBody.hpp"
#include "common/ClockFactory.hpp"
#include "common/EarthUtils.hpp"
#include "common/Common.hpp"
#include <memory>
#include "vehicles/car/api/CarRpcLibServer.hpp"


void ASimModeCar::BeginPlay()
{
    Super::BeginPlay();

    //let base class setup physics world
    initializeForPlay();
}
void ASimModeCar::initializeForPlay()
{
    std::vector<msr::airlib::UpdatableObject*> vehicles;
    for (auto& api : getApiProvider()->getVehicleSimApis())
        vehicles.push_back(api);
    //TODO: directly accept getVehicleSimApis() using generic container

    std::unique_ptr<PhysicsEngineBase> physics_engine = createPhysicsEngine();
    physics_engine_ = physics_engine.get();
    physics_world_.reset(new msr::airlib::PhysicsWorld(std::move(physics_engine),
        vehicles, getPhysicsLoopPeriod()));
}
void ASimModeCar::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    //remove everything that we created in BeginPlay
    physics_world_.reset();
    //stop physics thread before we dismantle
    //stopAsyncUpdator();

    Super::EndPlay(EndPlayReason);
}
void ASimModeCar::startAsyncUpdator()
{
    physics_world_->startAsyncUpdator();
}

void ASimModeCar::stopAsyncUpdator()
{
    physics_world_->stopAsyncUpdator();
}

long long ASimModeCar::getPhysicsLoopPeriod() const //nanoseconds
{
    return physics_loop_period_;
}
void ASimModeCar::setPhysicsLoopPeriod(long long  period)
{
    physics_loop_period_ = period;
}
std::unique_ptr<ASimModeCar::PhysicsEngineBase> ASimModeCar::createPhysicsEngine()
{
    std::unique_ptr<PhysicsEngineBase> physics_engine;
    std::string physics_engine_name = getSettings().physics_engine_name;
    if (physics_engine_name == "")
        physics_engine.reset(); //no physics engine
    else if (physics_engine_name == "FastPhysicsEngine") {
        msr::airlib::Settings fast_phys_settings;
        if (msr::airlib::Settings::singleton().getChild("FastPhysicsEngine", fast_phys_settings)) {
            physics_engine.reset(new msr::airlib::FastPhysicsEngine(fast_phys_settings.getBool("EnableGroundLock", true)));
        }
        else {
            physics_engine.reset(new msr::airlib::FastPhysicsEngine());
        }
    }
    else {
        physics_engine.reset();
        UAirBlueprintLib::LogMessageString("Unrecognized physics engine name: ",  physics_engine_name, LogDebugLevel::Failure);
    }

    return physics_engine;
}

bool ASimModeCar::isPaused() const
{
    return physics_world_->isPaused();
}

void ASimModeCar::pause(bool is_paused)
{
    physics_world_->pause(is_paused);
    UGameplayStatics::SetGamePaused(this->GetWorld(), is_paused);
}

void ASimModeCar::continueForTime(double seconds)
{
    if(physics_world_->isPaused())
    {
        physics_world_->pause(false);
        UGameplayStatics::SetGamePaused(this->GetWorld(), false);        
    }

    physics_world_->continueForTime(seconds);
    while(!physics_world_->isPaused())
    {
        continue; 
    }
    UGameplayStatics::SetGamePaused(this->GetWorld(), true);
}

void ASimModeCar::updateDebugReport(msr::airlib::StateReporterWrapper& debug_reporter)
{
    unused(debug_reporter);
    //we use custom debug reporting for this class
}

void ASimModeCar::Tick(float DeltaSeconds)
{
    { //keep this lock as short as possible
        physics_world_->lock();

        physics_world_->enableStateReport(EnableReport);
        physics_world_->updateStateReport();

        for (auto& api : getApiProvider()->getVehicleSimApis())
            api->updateRenderedState(DeltaSeconds);

        physics_world_->unlock();
    }

    //perform any expensive rendering update outside of lock region
    for (auto& api : getApiProvider()->getVehicleSimApis())
        api->updateRendering(DeltaSeconds);

    Super::Tick(DeltaSeconds);
}

void ASimModeCar::reset()
{
    UAirBlueprintLib::RunCommandOnGameThread([this]() {
        physics_world_->reset();
    }, true);
    
    //no need to call base reset because of our custom implementation
}

std::string ASimModeCar::getDebugReport()
{
    return physics_world_->getDebugReport();
}
void ASimModeCar::setupClockSpeed()
{
    typedef msr::airlib::ClockFactory ClockFactory;

    float clock_speed = getSettings().clock_speed;

    //setup clock in ClockFactory
    std::string clock_type = getSettings().clock_type;

    if (clock_type == "ScalableClock") {
        //scalable clock returns interval same as wall clock but multiplied by a scale factor
        ClockFactory::get(std::make_shared<msr::airlib::ScalableClock>(clock_speed == 1 ? 1 : 1 / clock_speed));
        UE_LOG(LogTemp,Display,TEXT("*******check point 3_1: %f"),clock_speed);
    }
    else if (clock_type == "SteppableClock") {
        //steppable clock returns interval that is a constant number irrespective of wall clock
        //we can either multiply this fixed interval by scale factor to speed up/down the clock
        //but that would cause vehicles like quadrotors to become unstable
        //so alternative we use here is instead to scale control loop frequency. The downside is that
        //depending on compute power available, we will max out control loop frequency and therefore can no longer
        //get increase in clock speed

        //Approach 1: scale clock period, no longer used now due to quadrotor instability
        //ClockFactory::get(std::make_shared<msr::airlib::SteppableClock>(
        //static_cast<msr::airlib::TTimeDelta>(getPhysicsLoopPeriod() * 1E-9 * clock_speed)));

        //Approach 2: scale control loop frequency if clock is speeded up
        UE_LOG(LogTemp,Display,TEXT("*******check point 3_2: %f"),clock_speed);
        if (clock_speed >= 1) {
            ClockFactory::get(std::make_shared<msr::airlib::SteppableClock>(
                static_cast<msr::airlib::TTimeDelta>(getPhysicsLoopPeriod() * 1E-9))); //no clock_speed multiplier

            setPhysicsLoopPeriod(getPhysicsLoopPeriod() / static_cast<long long>(clock_speed));
        }
        else {
            //for slowing down, this don't generate instability
            ClockFactory::get(std::make_shared<msr::airlib::SteppableClock>(
                static_cast<msr::airlib::TTimeDelta>(getPhysicsLoopPeriod() * 1E-9 * clock_speed)));
        }
    }
    else
        throw std::invalid_argument(common_utils::Utils::stringf(
            "clock_type %s is not recognized", clock_type.c_str()));
}

//-------------------------------- overrides -----------------------------------------------//

std::unique_ptr<msr::airlib::ApiServerBase> ASimModeCar::createApiServer() const
{
#ifdef AIRLIB_NO_RPC
    return ASimModeBase::createApiServer();
#else
    return std::unique_ptr<msr::airlib::ApiServerBase>(new msr::airlib::CarRpcLibServer(
        getApiProvider(), getSettings().api_server_address, getSettings().api_port));
#endif
}

void ASimModeCar::getExistingVehiclePawns(TArray<AActor*>& pawns) const
{
    UAirBlueprintLib::FindAllActor<TVehiclePawn>(this, pawns);
}

bool ASimModeCar::isVehicleTypeSupported(const std::string& vehicle_type) const
{
    return ((vehicle_type == AirSimSettings::kVehicleTypePhysXCar) ||
            (vehicle_type == AirSimSettings::kVehicleTypeArduRover));
}

std::string ASimModeCar::getVehiclePawnPathName(const AirSimSettings::VehicleSetting& vehicle_setting) const
{
    //decide which derived BP to use
    std::string pawn_path = vehicle_setting.pawn_path;
    if (pawn_path == "")
        pawn_path = "DefaultCar";

    return pawn_path;
}

PawnEvents* ASimModeCar::getVehiclePawnEvents(APawn* pawn) const
{
    return static_cast<TVehiclePawn*>(pawn)->getPawnEvents();
}
const common_utils::UniqueValueMap<std::string, APIPCamera*> ASimModeCar::getVehiclePawnCameras(
    APawn* pawn) const
{
    return (static_cast<const TVehiclePawn*>(pawn))->getCameras();
}
void ASimModeCar::initializeVehiclePawn(APawn* pawn)
{
    auto vehicle_pawn = static_cast<TVehiclePawn*>(pawn);
    vehicle_pawn->initializeForBeginPlay(getSettings().engine_sound);
}
std::unique_ptr<PawnSimApi> ASimModeCar::createVehicleSimApi(
    const PawnSimApi::Params& pawn_sim_api_params) const
{
    auto vehicle_pawn = static_cast<TVehiclePawn*>(pawn_sim_api_params.pawn);
    auto vehicle_sim_api = std::unique_ptr<PawnSimApi>(new CarPawnSimApi(pawn_sim_api_params, 
        vehicle_pawn->getKeyBoardControls(), vehicle_pawn->getVehicleMovementComponent()));
    vehicle_sim_api->initialize();
    //For multirotors the vehicle_sim_api are in PhysicsWOrld container and then get reseted when world gets reseted
    //vehicle_sim_api->reset();
    return vehicle_sim_api;
}
msr::airlib::VehicleApiBase* ASimModeCar::getVehicleApi(const PawnSimApi::Params& pawn_sim_api_params,
    const PawnSimApi* sim_api) const
{
    const auto car_sim_api = static_cast<const CarPawnSimApi*>(sim_api);
    return car_sim_api->getVehicleApi();
}